; ============================================================================
;  ATM TURBO 710 - CP/M SYSTEM PAGE  (page 3 of atm2.rom)
; ============================================================================
;
;  Source   : data/rom/atm2.rom, bytes $C000-$FFFF (page 3), 16 KiB
;  Content  : page-zero stubs, service monitor core, cold-boot config
;             shell, DRI CCP 2.2, BDOS 2.2, XVR BIOS V1.07.13 and the
;             driver/WBOOT tail (all addresses below are image-relative).
;  Method   : z80dasm 1.2.0, code/data block map from a dedicated
;             Z80 length-decoder + reachability analysis (scripts/);
;             labels and comments added after studying the ROM bytes,
;             the atmturbo BIOS notes and live emulator traces.
;  Style    : educational, after Logan & O'Hara, "The Complete Spectrum
;             ROM Disassembly".
;
; ----------------------------------------------------------------------------
;  THE ROM BUNDLE
; ----------------------------------------------------------------------------
;  atm2.rom is a 64 KiB bundle of four 16 KiB pages.  core/src/emulator/
;  memory/rom.cpp maps the MM_ATM710 slots as:
;
;    sos  = page 0 ($0000-)  boot monitor / 128K start-up
;    dos  = page 1 ($4000-)  TR-DOS
;    128  = page 2 ($8000-)  128K BASIC
;    sys  = page 3 ($C000-)  THIS CP/M system image
;
;  Memory::SetROMSystem() maps the sys page at $0000-$3FFF whenever the
;  machine enters a service session (#7FFD bit 4 set) - the mapping the
;  page is primarily assembled for.
;
; ----------------------------------------------------------------------------
;  THE ADDRESS MAPPINGS (read this first)
; ----------------------------------------------------------------------------
;  The same 16 KiB image is visible under FOUR bases at runtime; which
;  one a branch target means depends on the target window:
;
;  1. $0000-$3FFF -> 1:1.  The service session (ROM at $0000, #7FFD bit 4).
;     Page-zero stubs, monitor core and the WBOOT code's own local jumps
;     (e.g. JP WbootContinue $38FA inside Wboot) use this view.
;
;  2. $C000-$D3FF -> +$C000.  Alias window onto the service code: the ROM
;     page banked high.  Branches here from this page land back on image
;     $0000-$13FF monitor code.
;
;  3. $D400-$EFFF -> DUAL, two copies at different times:
;       a) cold start: the config shell (image $1400-$1BC5) copied to
;          $D400 - its internal absolute targets use +$C000;
;       b) after warm boot: WbootReloadImage ($3866) LDIRs the CCP+BDOS
;          image $1BC6-$36E3 to $D400-$EF1E - internal targets use
;          image = runtime - $B83A.
;     Proofs for (b): the CCP trampolines at CcpBase JP $D75C/$D758 =
;     +$35C/+$358 inside the copy; the 20-entry BdosDispatch at $31C6
;     lands exactly on the traced BDOS entry $EA00; the traced BDOS
;     bodies $E9DD/$EA9F map to image $31A3/$3265.
;
;  4. $F000-$FFFF -> +$C000.  The driver/WBOOT tail: BiosJumpTable at
;     image $02CA dispatches JP $F8xx into image $38xx; the traced
;     WBOOT entry $F87E = image $387E.
;
; ----------------------------------------------------------------------------
;  REGION MAP (image-relative)
; ----------------------------------------------------------------------------
;    $0000-$00FF  page zero: WbootStub, BDOS marker (ED 59), RST stubs
;    $0100-$13FF  service monitor/menu core, FDC helpers, config code
;    $1400-$1BC5  cold-boot config shell (runs from the $D400 copy)
;    $1BC6-$23FF  DRI CCP (serial banner, command keywords, messages)
;    $2400-$2BFF  BDOS data: error strings, FCB/record utilities
;    $2C00-$36E3  BDOS dispatch ($31C6) + function bodies + XVR BIOS
;    $36E4-$37FF  BIOS tail (fault messages)
;    $3800-$3FFF  driver/WBOOT: RAM-size menu, Wboot ($387E), keyboard
;                 and charset tables, driver sub-dispatch ($3E08)
;
; ----------------------------------------------------------------------------
;  RUNTIME ANCHORS (traced on the emulator)
; ----------------------------------------------------------------------------
;    image $0965 = service LDIR copy   (repeated-M1 "wait loop" signature)
;    image $1BC6 = $D400 CCP warm entry (end of every transient)
;    image $31C6 = $EA00 BDOS function entry (CALL $0005 lands here)
;    image $3265 = $EA9F BDOS open/directory-search core
;    image $387E = $F87E WBOOT (+$C000 driver window)
;
;  Open question: BiosJumpTable entries $F833/$F835/$F857 fall inside
;  the 'Memory '/'size is ' string bytes of THIS page's driver mirror;
;  on the monitor page the same addresses are real handlers (see
;  ../fdc-driver.md - the two pages carry sibling builds of one driver).
;
;  Companion documents: ../README.md (index), ../monitor-rom.md,
;  ../bios-ram.md, ../analysis.md, ../disk-monitor.md, ../pr2-loader.md.

	org 00000h
BdosEntry:	equ 0x0005
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Page zero, service view - WBOOT stub.
; DI then JP Wboot ($387E, image 1:1).  In the CP/M RAM view the same
; cell is the standard warm-boot vector installed at boot; the byte
; pair at $0004/$0005 (ED 59 = "OUT (C),E") doubles as the ATM BDOS
; entry marker - see BdosEntry below.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

WbootStub:
	di			;0000	f3		.
l0001h:
	jp Wboot		;0001	c3 7e 38	. ~ 8
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BDOS entry marker (ATM-specific).
; The RAM page zero carries the BDOS entry at $0005: callers do
; CALL $0005 with the function in C, parameters in DE, and read the
; result from A.  These two bytes (ED 59 = "OUT (C),E)") sit at
; $0004/$0005 and double as the ATM BDOS marker - the service BDOS
; recognises entry here and dispatches into the $EAxx bodies.
; Traced protocol notes: the pushed return address observed at (SP)
; is one less than the instruction after the CALL - match
; pc == retPc || pc == retPc+1 when sampling the result code
; (../bios-ram.md).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
l0004h:
	out (c),e		;0004	ed 59		. Y
l0006h:
	nop			;0006	00		.
l0007h:
	nop			;0007	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 08h vector - JP Rst08Handler ($110B).
; The config shell calls this (RST 08 at ColdShellEntry) for its
; menu/dialog service layer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst08Vector:
	jp Rst08Handler		;0008	c3 0b 11	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ZeroPageRst10Pad - NOP padding after the RST 08 stub; RST 10h is
; unused and falls through to the RST 18h stub.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'ZeroPageRst10Pad' (start 0x000b end 0x0010)
ZeroPageRst10Pad_start:
	defb 000h		;000b	00		.
l000ch:
	defb 000h		;000c	00		.
l000dh:
	defb 000h		;000d	00		.
l000eh:
	defb 000h		;000e	00		.
l000fh:
	defb 000h		;000f	00		.
Rst10Vector:
	nop			;0010	00		.
l0011h:
	nop			;0011	00		.
l0012h:
	nop			;0012	00		.
	nop			;0013	00		.
	nop			;0014	00		.
	nop			;0015	00		.
	nop			;0016	00		.
l0017h:
	nop			;0017	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 18h vector - JP Rst18Handler ($004E).
; RST 10h (slot above) is unused: its NOP padding falls through to
; this stub, so both vectors share $004E.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst18Vector:
	jp Rst18Handler		;0018	c3 4e 00	. N .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ZeroPageRst20Pad - NOP padding inside the RST 20h vector slot.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'ZeroPageRst20Pad' (start 0x001b end 0x0020)
ZeroPageRst20Pad_start:
	defb 000h		;001b	00		.
l001ch:
	defb 000h		;001c	00		.
	defb 000h		;001d	00		.
	defb 000h		;001e	00		.
	defb 000h		;001f	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 20h vector - JP Rst20Handler ($0B9D).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst20Vector:
	jp Rst20Handler	;0020	c3 9d 0b	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ZeroPageRst30Pad - NOP padding inside the RST 30h vector slot.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'ZeroPageRst30Pad' (start 0x0023 end 0x0028)
ZeroPageRst30Pad_start:
	defb 000h		;0023	00		.
	defb 000h		;0024	00		.
	defb 000h		;0025	00		.
	defb 000h		;0026	00		.
	defb 000h		;0027	00		.
Rst28Vector:
	nop			;0028	00		.
	nop			;0029	00		.
	nop			;002a	00		.
	nop			;002b	00		.
	nop			;002c	00		.
	nop			;002d	00		.
	nop			;002e	00		.
l002fh:
	nop			;002f	00		.
Rst30Vector:
	nop			;0030	00		.
	nop			;0031	00		.
	nop			;0032	00		.
	nop			;0033	00		.
	nop			;0034	00		.
l0035h:
	nop			;0035	00		.
	nop			;0036	00		.
	nop			;0037	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; INT vector - JP IntHandler ($0155).
; The launch traces saw ~160 INT hits during a PR2 load with the
; game running IM2 (../bios-ram.md, "Interrupts During a Launch").
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
IntVector:
	jp IntHandler		;0038	c3 55 01	. U .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PageZeroReserved - reserved page-zero cells $003B-$004A.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'PageZeroReserved' (start 0x003b end 0x004b)
PageZeroReserved_start:
	defb 0ffh		;003b	ff		.
	defb 0ech		;003c	ec		.
	defb 0bdh		;003d	bd		.
	defb 08dh		;003e	8d		.
	defb 00ch		;003f	0c		.
l0040h:
	defb 0ffh		;0040	ff		.
l0041h:
	defb 0ech		;0041	ec		.
	defb 0cfh		;0042	cf		.
	defb 04ch		;0043	4c		L
	defb 0cch		;0044	cc		.
	defb 06fh		;0045	6f		o
	defb 04eh		;0046	4e		N
	defb 0bdh		;0047	bd		.
	defb 09ch		;0048	9c		.
	defb 02dh		;0049	2d		-
	defb 00ch		;004a	0c		.
PageZeroReserved_end:
	add a,006h		;004b	c6 06		. .
	ret			;004d	c9		.
Rst18Handler:
	di			;004e	f3		.
	push bc			;004f	c5		.
	ld b,(ix-06eh)		;0050	dd 46 92	. F .
	ld (05f51h),a		;0053	32 51 5f	2 Q _
	and 03fh		;0056	e6 3f		. ?
	call sub_005dh		;0058	cd 5d 00	. ] .
	pop bc			;005b	c1		.
l005ch:
	ret			;005c	c9		.
sub_005dh:
	di			;005d	f3		.
	or 080h			;005e	f6 80		. .
	push bc			;0060	c5		.
	cpl			;0061	2f		/
	ld bc,0fc77h		;0062	01 77 fc	. w .
	push de			;0065	d5		.
NmiVector:
	ld e,(ix-062h)		;0066	dd 5e 9e	. ^ .
	out (c),e		;0069	ed 59		. Y
	ld bc,0fff7h		;006b	01 f7 ff	. . .
	out (c),a		;006e	ed 79		. y
	ld bc,0fd77h		;0070	01 77 fd	. w .
	out (c),e		;0073	ed 59		. Y
	pop de			;0075	d1		.
	pop af			;0076	f1		.
l0077h:
	ret			;0077	c9		.
	ld sp,08000h		;0078	31 00 80	1 . .
	im 1			;007b	ed 56		. V
	ld hl,(05fa3h)		;007d	2a a3 5f	* . _
l0080h:
	ld de,055aah		;0080	11 aa 55	. . U
	xor a			;0083	af		.
	sbc hl,de		;0084	ed 52		. R
	ld (05fa3h),de		;0086	ed 53 a3 5f	. S . _
	jr z,l0090h		;008a	28 04		( .
	ld (ix-01ah),0ffh	;008c	dd 36 e6 ff	. 6 . .
l0090h:
	ld a,011h		;0090	3e 11		> .
	call sub_3729h		;0092	cd 29 37	. ) 7
	call sub_375ch		;0095	cd 5c 37	. \ 7
	ld a,011h		;0098	3e 11		> .
	call sub_3729h		;009a	cd 29 37	. ) 7
	call sub_375ch		;009d	cd 5c 37	. \ 7
	ld a,055h		;00a0	3e 55		> U
	in a,(0feh)		;00a2	db fe		. .
	ld a,001h		;00a4	3e 01		> .
	in a,(0feh)		;00a6	db fe		. .
	ld a,055h		;00a8	3e 55		> U
	in a,(0feh)		;00aa	db fe		. .
	ld a,008h		;00ac	3e 08		> .
	in a,(0feh)		;00ae	db fe		. .
	xor a			;00b0	af		.
	in a,(0feh)		;00b1	db fe		. .
	ei			;00b3	fb		.
	ld hl,PageZeroReserved_start	;00b4	21 3b 00	! ; .
	call sub_0aach		;00b7	cd ac 0a	. . .
	ld sp,0b000h		;00ba	31 00 b0	1 . .
	ld a,065h		;00bd	3e 65		> e
	call sub_005dh		;00bf	cd 5d 00	. ] .
	ld de,08000h		;00c2	11 00 80	. . .
	ld hl,0c805h		;00c5	21 05 c8	! . .
	ld bc,007fbh		;00c8	01 fb 07	. . .
	ldir			;00cb	ed b0		. .
	ld hl,0f1fdh		;00cd	21 fd f1	! . .
	ld bc,00a04h		;00d0	01 04 0a	. . .
	ldir			;00d3	ed b0		. .
	call sub_01e0h		;00d5	cd e0 01	. . .
	ld a,003h		;00d8	3e 03		> .
	rst 18h			;00da	df		.
	ld e,(ix-03ah)		;00db	dd 5e c6	. ^ .
	ld a,001h		;00de	3e 01		> .
	di			;00e0	f3		.
	call 08003h		;00e1	cd 03 80	. . .
	push af			;00e4	f5		.
	ld ix,05fbfh		;00e5	dd 21 bf 5f	. ! . _
	ld (ix-03ah),e		;00e9	dd 73 c6	. s .
	dec d			;00ec	15		.
	jr nz,l00f3h		;00ed	20 04		  .
l00efh:
	set 3,(ix-062h)		;00ef	dd cb 9e de	. . . .
l00f3h:
	ld a,055h		;00f3	3e 55		> U
	in a,(0feh)		;00f5	db fe		. .
l00f7h:
	ld a,008h		;00f7	3e 08		> .
	in a,(0feh)		;00f9	db fe		. .
	ld a,08ah		;00fb	3e 8a		> .
	in a,(0feh)		;00fd	db fe		. .
l00ffh:
	ld a,055h		;00ff	3e 55		> U
	in a,(0feh)		;0101	db fe		. .
	ld a,001h		;0103	3e 01		> .
	in a,(0feh)		;0105	db fe		. .
l0107h:
	ex af,af'		;0107	08		.
	ld a,055h		;0108	3e 55		> U
	in a,(0feh)		;010a	db fe		. .
	ld a,008h		;010c	3e 08		> .
	in a,(0feh)		;010e	db fe		. .
	xor a			;0110	af		.
	in a,(0feh)		;0111	db fe		. .
	ex af,af'		;0113	08		.
	cp 00ah			;0114	fe 0a		. .
	jr nc,l0125h		;0116	30 0d		0 .
	ld a,055h		;0118	3e 55		> U
	in a,(0feh)		;011a	db fe		. .
	ld a,007h		;011c	3e 07		> .
	in a,(0feh)		;011e	db fe		. .
l0120h:
	ld hl,l3e0bh		;0120	21 0b 3e	! . >
	jr l0128h		;0123	18 03		. .
l0125h:
	ld hl,l0e96h		;0125	21 96 0e	! . .
l0128h:
	ld de,05f72h		;0128	11 72 5f	. r _
	ld bc,l0012h		;012b	01 12 00	. . .
	di			;012e	f3		.
	ldir			;012f	ed b0		. .
	ei			;0131	fb		.
	ld a,003h		;0132	3e 03		> .
	rst 18h			;0134	df		.
	call sub_0aa4h		;0135	cd a4 0a	. . .
	pop af			;0138	f1		.
	or a			;0139	b7		.
	jp nz,08000h		;013a	c2 00 80	. . .
	call sub_3ac1h		;013d	cd c1 3a	. . :
	ld sp,08000h		;0140	31 00 80	1 . .
	call MonitorPrint	;0143	cd 06 12	. . .
	dec de			;0146	1b		.
	ld e,a			;0147	5f		_
	nop			;0148	00		.
	call sub_382ch		;0149	cd 2c 38	. , 8
	call WbootReloadImage	;014c	cd 66 38	. f 8
	call CcpTable1A64_end	;014f	cd 6b 1a	. k .
	jp 0f85ch		;0152	c3 5c f8	. \ .
IntHandler:
	push af			;0155	f5		.
	push bc			;0156	c5		.
	push de			;0157	d5		.
	push hl			;0158	e5		.
	ex af,af'		;0159	08		.
	exx			;015a	d9		.
	push af			;015b	f5		.
	push bc			;015c	c5		.
	push de			;015d	d5		.
	push hl			;015e	e5		.
	push ix			;015f	dd e5		. .
	ld ix,05fbfh		;0161	dd 21 bf 5f	. ! . _
	ld a,001h		;0165	3e 01		> .
	rst 18h			;0167	df		.
	push af			;0168	f5		.
	ld hl,05f63h		;0169	21 63 5f	! c _
	ld b,004h		;016c	06 04		. .
l016eh:
	inc (hl)		;016e	34		4
	inc hl			;016f	23		#
	jr nz,l0174h		;0170	20 02		  .
	djnz l016eh		;0172	10 fa		. .
l0174h:
	call sub_0562h		;0174	cd 62 05	. b .
	call 05f72h		;0177	cd 72 5f	. r _
	pop af			;017a	f1		.
	rst 18h			;017b	df		.
	pop ix			;017c	dd e1		. .
	pop hl			;017e	e1		.
	pop de			;017f	d1		.
	pop bc			;0180	c1		.
	pop af			;0181	f1		.
	exx			;0182	d9		.
	ex af,af'		;0183	08		.
	pop hl			;0184	e1		.
	pop de			;0185	d1		.
	pop bc			;0186	c1		.
	pop af			;0187	f1		.
	ei			;0188	fb		.
	ret			;0189	c9		.
sub_018ah:
	ei			;018a	fb		.
	push hl			;018b	e5		.
	push de			;018c	d5		.
	push af			;018d	f5		.
	ld a,00ah		;018e	3e 0a		> .
	cp c			;0190	b9		.
	jr nc,l0199h		;0191	30 06		0 .
	pop af			;0193	f1		.
	ld a,0ffh		;0194	3e ff		> .
	pop de			;0196	d1		.
	pop hl			;0197	e1		.
	ret			;0198	c9		.
l0199h:
	ld a,c			;0199	79		y
	ld hl,l01a4h		;019a	21 a4 01	! . .
	call sub_143eh		;019d	cd 3e 14	. > .
	pop af			;01a0	f1		.
	pop de			;01a1	d1		.
	ex (sp),hl		;01a2	e3		.
	ret			;01a3	c9		.
l01a4h:
	cp d			;01a4	ba		.
	ld bc,l01c2h		;01a5	01 c2 01	. . .
	jp z,07b01h		;01a8	ca 01 7b	. . {
	ld e,a			;01ab	5f		_
	ld a,(hl)		;01ac	7e		~
	ld e,a			;01ad	5f		_
	call nc,0dd01h		;01ae	d4 01 dd	. . .
	ld bc,l01ddh		;01b1	01 dd 01	. . .
	defb 0ddh,001h,0ddh ;illegal sequence	;01b4	dd 01 dd	. . .
	ld bc,l01f1h		;01b7	01 f1 01	. . .
	ld hl,l0107h		;01ba	21 07 01	! . .
	ld de,0ffffh		;01bd	11 ff ff	. . .
	xor a			;01c0	af		.
	ret			;01c1	c9		.
l01c2h:
	ld bc,l1600h		;01c2	01 00 16	. . .
	call sub_3869h		;01c5	cd 69 38	. i 8
	xor a			;01c8	af		.
	ret			;01c9	c9		.
	ld de,(0f86ah)		;01ca	ed 5b 6a f8	. [ j .
	ld (0f86ah),hl		;01ce	22 6a f8	" j .
	ex de,hl		;01d1	eb		.
	xor a			;01d2	af		.
	ret			;01d3	c9		.
	ld de,(05f63h)		;01d4	ed 5b 63 5f	. [ c _
	ld hl,(05f65h)		;01d8	2a 65 5f	* e _
	xor a			;01db	af		.
	ret			;01dc	c9		.
l01ddh:
	ld a,0ffh		;01dd	3e ff		> .
	ret			;01df	c9		.
sub_01e0h:
	ret			;01e0	c9		.
	nop			;01e1	00		.
	ret nz			;01e2	c0		.
	xor a			;01e3	af		.
l01e4h:
	add a,(hl)		;01e4	86		.
	adc a,000h		;01e5	ce 00		. .
	inc hl			;01e7	23		#
	bit 7,h			;01e8	cb 7c		. |
	jr nz,l01e4h		;01ea	20 f8		  .
	dec a			;01ec	3d		=
	ret z			;01ed	c8		.
	jp l05f2h		;01ee	c3 f2 05	. . .
l01f1h:
	ld a,e			;01f1	7b		{
	cp 007h			;01f2	fe 07		. .
	jr z,l0224h		;01f4	28 2e		( .
	jr nc,l022ah		;01f6	30 32		0 2
l01f8h:
	ld (05f56h),hl		;01f8	22 56 5f	" V _
	ld hl,03f01h		;01fb	21 01 3f	! . ?
	cp 004h			;01fe	fe 04		. .
l0200h:
	jr nc,l020eh		;0200	30 0c		0 .
l0202h:
	ld hl,l3f0dh		;0202	21 0d 3f	! . ?
	bit 0,(ix-07fh)		;0205	dd cb 81 46	. . . F
	jr nz,l020eh		;0209	20 03		  .
	ld hl,l3f19h		;020b	21 19 3f	! . ?
l020eh:
	bit 0,a			;020e	cb 47		. G
	jr z,l0217h		;0210	28 05		( .
	ld a,006h		;0212	3e 06		> .
	call sub_1106h		;0214	cd 06 11	. . .
l0217h:
	ld a,b			;0217	78		x
	call sub_11efh		;0218	cd ef 11	. . .
	inc b			;021b	04		.
	defb 0ddh,001h,033h ;illegal sequence	;021c	dd 01 33	. . 3
	ld (bc),a		;021f	02		.
	dec (hl)		;0220	35		5
	ld sp,hl		;0221	f9		.
	ld a,002h		;0222	3e 02		> .
l0224h:
	bit 0,(ix-07fh)		;0224	dd cb 81 46	. . . F
	jr z,l01f8h		;0228	28 ce		( .
l022ah:
	ld a,001h		;022a	3e 01		> .
	ret			;022c	c9		.
l022dh:
	ld a,002h		;022d	3e 02		> .
	ret			;022f	c9		.
l0230h:
	ld a,003h		;0230	3e 03		> .
	ret			;0232	c9		.
	ld a,d			;0233	7a		z
	cp 006h			;0234	fe 06		. .
	jr nc,l022dh		;0236	30 f5		0 .
	call sub_1106h		;0238	cd 06 11	. . .
	ld d,(hl)		;023b	56		V
	xor a			;023c	af		.
	ret			;023d	c9		.
	ld a,d			;023e	7a		z
	push hl			;023f	e5		.
	pop iy			;0240	fd e1		. .
	inc hl			;0242	23		#
	exx			;0243	d9		.
	ld hl,03f25h		;0244	21 25 3f	! % ?
	cp 003h			;0247	fe 03		. .
	jr nc,l0230h		;0249	30 e5		0 .
	call sub_143eh		;024b	cd 3e 14	. > .
	ld de,04004h		;024e	11 04 40	. . @
	ld bc,l001ch		;0251	01 1c 00	. . .
	ldir			;0254	ed b0		. .
	ld e,(ix-062h)		;0256	dd 5e 9e	. ^ .
	di			;0259	f3		.
	im 0			;025a	ed 46		. F
	ld bc,0c077h		;025c	01 77 c0	. w .
	xor a			;025f	af		.
	out (c),a		;0260	ed 79		. y
	ld bc,07ffdh		;0262	01 fd 7f	. . .
	out (c),a		;0265	ed 79		. y
	ld a,(iy+004h)		;0267	fd 7e 04	. ~ .
	ld sp,l0270h		;026a	31 70 02	1 p .
	jp l02b3h		;026d	c3 b3 02	. . .
l0270h:
	ld (hl),d		;0270	72		r
	ld (bc),a		;0271	02		.
	ld a,010h		;0272	3e 10		> .
	ld bc,07ffdh		;0274	01 fd 7f	. . .
	out (c),a		;0277	ed 79		. y
	ld a,(iy+000h)		;0279	fd 7e 00	. ~ .
	ld sp,l0282h		;027c	31 82 02	1 . .
	jp l02b3h		;027f	c3 b3 02	. . .
l0282h:
	add a,h			;0282	84		.
	ld (bc),a		;0283	02		.
	ld bc,0c077h		;0284	01 77 c0	. w .
	ld a,006h		;0287	3e 06		> .
	out (c),a		;0289	ed 79		. y
	ld a,(iy+005h)		;028b	fd 7e 05	. ~ .
	ld sp,l0294h		;028e	31 94 02	1 . .
	jp l02b3h		;0291	c3 b3 02	. . .
l0294h:
	sub (hl)		;0294	96		.
	ld (bc),a		;0295	02		.
	xor a			;0296	af		.
	ld bc,07ffdh		;0297	01 fd 7f	. . .
	out (c),a		;029a	ed 79		. y
	ld a,(iy+005h)		;029c	fd 7e 05	. ~ .
	ld sp,l02a5h		;029f	31 a5 02	1 . .
	jp l02b3h		;02a2	c3 b3 02	. . .
l02a5h:
	and a			;02a5	a7		.
	ld (bc),a		;02a6	02		.
	ld bc,0ff77h		;02a7	01 77 ff	. w .
	ld a,e			;02aa	7b		{
	and 0f8h		;02ab	e6 f8		. .
	or 003h			;02ad	f6 03		. .
	ld e,a			;02af	5f		_
	jp l0004h		;02b0	c3 04 00	. . .
l02b3h:
	exx			;02b3	d9		.
	ld e,l			;02b4	5d		]
	ld d,h			;02b5	54		T
	ld bc,l00f7h		;02b6	01 f7 00	. . .
	jr l02bdh		;02b9	18 02		. .
l02bbh:
	ld a,(de)		;02bb	1a		.
	inc de			;02bc	13		.
l02bdh:
	xor 080h		;02bd	ee 80		. .
	cpl			;02bf	2f		/
	out (c),a		;02c0	ed 79		. y
	ld a,b			;02c2	78		x
	add a,040h		;02c3	c6 40		. @
	ld b,a			;02c5	47		G
	jr nc,l02bbh		;02c6	30 f3		0 .
	exx			;02c8	d9		.
	ret			;02c9	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; THE CP/M BIOS JUMP TABLE - 17 entries, JP $F8xx each.
; Executed 1:1 in the service view; every target translates +$C000
; into this page's driver mirror at image $38xx:
;   $02CA JP $F835  boot entry        $02DC JP $F8B5
;   $02CD JP $F8A9  console input     $02DF JP $F8C5
;   $02D0 JP $F833  (nop/ret)         $02E2 JP $F8C0
;   $02D3 JP $F89A  console output    $02E5 JP $F857  home disk
;   $02D6 JP $F833  (nop/ret)         $02E8 JP $F833  (nop/ret)
;   $02D9 JP $F84A  list output       $02EB JP $F83B  system reset
;   $02EE JP $F88F  read sector       $02F1 JP $F872  write sector
;   $02F4 JP $F87E  WBOOT (traced)    $02F7 JP $F882
;   $02FA JP $F83F
; Same target set as the monitor page's $F800 table (../monitor-rom.md).
; NOTE: $F833/$F835/$F857 land inside the 'Memory '/'size is ' string
; bytes here - this page's driver mirror is a sibling build, not the
; byte-identical monitor driver (open question, HEADER above).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BiosJumpTable:
	jp 0f835h		;02ca	c3 35 f8	. 5 .
	jp 0f8a9h		;02cd	c3 a9 f8	. . .
	jp 0f833h		;02d0	c3 33 f8	. 3 .
	jp 0f89ah		;02d3	c3 9a f8	. . .
	jp 0f833h		;02d6	c3 33 f8	. 3 .
	jp 0f84ah		;02d9	c3 4a f8	. J .
	jp 0f8b5h		;02dc	c3 b5 f8	. . .
	jp 0f8c5h		;02df	c3 c5 f8	. . .
	jp 0f8c0h		;02e2	c3 c0 f8	. . .
	jp 0f857h		;02e5	c3 57 f8	. W .
	jp 0f833h		;02e8	c3 33 f8	. 3 .
	jp 0f83bh		;02eb	c3 3b f8	. ; .
	jp 0f88fh		;02ee	c3 8f f8	. . .
	jp 0f872h		;02f1	c3 72 f8	. r .
	jp 0f87eh		;02f4	c3 7e f8	. ~ .
	jp 0f882h		;02f7	c3 82 f8	. . .
	jp 0f83fh		;02fa	c3 3f f8	. ? .
	nop			;02fd	00		.
	ret			;02fe	c9		.
	call 0f8c9h		;02ff	cd c9 f8	. . .
	jp l0090h		;0302	c3 90 00	. . .
	call 0f8c9h		;0305	cd c9 f8	. . .
	rst 0			;0308	c7		.
	call 0f8dbh		;0309	cd db f8	. . .
	call sub_018ah		;030c	cd 8a 01	. . .
	call 0f8feh		;030f	cd fe f8	. . .
	ei			;0312	fb		.
	ret			;0313	c9		.
l0314h:
	in a,(0fbh)		;0314	db fb		. .
l0316h:
	rlca			;0316	07		.
	jr c,l0314h		;0317	38 fb		8 .
	ld a,c			;0319	79		y
	out (0fbh),a		;031a	d3 fb		. .
	out (07bh),a		;031c	d3 7b		. {
	out (0fbh),a		;031e	d3 fb		. .
	ret			;0320	c9		.
	ld b,0f8h		;0321	06 f8		. .
	ld a,000h		;0323	3e 00		> .
	ret			;0325	c9		.
	call 0f8f6h		;0326	cd f6 f8	. . .
	im 2			;0329	ed 5e		. ^
	jp 0ea00h		;032b	c3 00 ea	. . .
	call 0f8f6h		;032e	cd f6 f8	. . .
	im 2			;0331	ed 5e		. ^
	call 0ea03h		;0333	cd 03 ea	. . .
	call 0f8c9h		;0336	cd c9 f8	. . .
	im 1			;0339	ed 56		. V
	ret			;033b	c9		.
	call 0f8dbh		;033c	cd db f8	. . .
	ld (ix-005h),0ffh	;033f	dd 36 fb ff	. 6 . .
	call 0f8feh		;0343	cd fe f8	. . .
	ei			;0346	fb		.
	ret			;0347	c9		.
	ld a,007h		;0348	3e 07		> .
	jr l034eh		;034a	18 02		. .
	ld a,008h		;034c	3e 08		> .
l034eh:
	call 0f8dbh		;034e	cd db f8	. . .
	call sub_1245h		;0351	cd 45 12	. E .
	call 0f8feh		;0354	cd fe f8	. . .
	ei			;0357	fb		.
	ret			;0358	c9		.
	call 0f8dbh		;0359	cd db f8	. . .
	call sub_122eh		;035c	cd 2e 12	. . .
	call 0f8feh		;035f	cd fe f8	. . .
	ei			;0362	fb		.
	ret			;0363	c9		.
	call 0f8dbh		;0364	cd db f8	. . .
	ei			;0367	fb		.
	push af			;0368	f5		.
	ld a,c			;0369	79		y
	call Rst20Handler	;036a	cd 9d 0b	. . .
	pop af			;036d	f1		.
	call 0f8feh		;036e	cd fe f8	. . .
	ei			;0371	fb		.
	ret			;0372	c9		.
	call 0f8dbh		;0373	cd db f8	. . .
	ei			;0376	fb		.
	call 05f75h		;0377	cd 75 5f	. u _
	call 0f8feh		;037a	cd fe f8	. . .
	ei			;037d	fb		.
	ret			;037e	c9		.
	call 0f8dbh		;037f	cd db f8	. . .
	call 05f78h		;0382	cd 78 5f	. x _
	call 0f8feh		;0385	cd fe f8	. . .
	ei			;0388	fb		.
	ret			;0389	c9		.
	ld a,c			;038a	79		y
	ld (l0001h+2),a		;038b	32 03 00	2 . .
	ret			;038e	c9		.
	ld a,(l0001h+2)		;038f	3a 03 00	: . .
	ret			;0392	c9		.
	di			;0393	f3		.
	ld (0f8d7h),a		;0394	32 d7 f8	2 . .
	ld a,052h		;0397	3e 52		> R
l0399h:
	ex (sp),hl		;0399	e3		.
	ld (0f8d9h),hl		;039a	22 d9 f8	" . .
	pop hl			;039d	e1		.
	out (0fdh),a		;039e	d3 fd		. .
	ld a,000h		;03a0	3e 00		> .
	jp WbootStub		;03a2	c3 00 00	. . .
	di			;03a5	f3		.
	ex (sp),hl		;03a6	e3		.
	ld (0f8f4h),hl		;03a7	22 f4 f8	" . .
	pop hl			;03aa	e1		.
	call 0f8c9h		;03ab	cd c9 f8	. . .
	ld (0f975h),sp		;03ae	ed 73 75 f9	. s u .
	ld sp,08000h		;03b2	31 00 80	1 . .
	im 1			;03b5	ed 56		. V
	push ix			;03b7	dd e5		. .
	ld ix,05fbfh		;03b9	dd 21 bf 5f	. ! . _
	jp WbootStub		;03bd	c3 00 00	. . .
	di			;03c0	f3		.
	ld (0f8d7h),a		;03c1	32 d7 f8	2 . .
	ld a,042h		;03c4	3e 42		> B
	jr l0399h		;03c6	18 d1		. .
	di			;03c8	f3		.
	ex (sp),hl		;03c9	e3		.
	ld (0f910h),hl		;03ca	22 10 f9	" . .
	pop hl			;03cd	e1		.
	pop ix			;03ce	dd e1		. .
	call 0f8f6h		;03d0	cd f6 f8	. . .
	ld sp,(0f975h)		;03d3	ed 7b 75 f9	. { u .
	im 2			;03d7	ed 5e		. ^
	jp WbootStub		;03d9	c3 00 00	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Video-mode save/restore through port $FD.
; LD (F975),SP / LD SP,$8000 / IM 1 ... writes attribute bytes 'R'
; and 'B' (52h/42h) to port $FD, restores SP from $F975, IM 2, EI.
; Reached from the video-mode switch paths of the monitor core.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
VideoSaveRestore:
	di			;03dc	f3		.
	ld (0f975h),sp		;03dd	ed 73 75 f9	. s u .
	ld sp,08000h		;03e1	31 00 80	1 . .
	im 1			;03e4	ed 56		. V
	ld (0f92bh),a		;03e6	32 2b f9	2 + .
	ld a,052h		;03e9	3e 52		> R
	out (0fdh),a		;03eb	d3 fd		. .
	call IntHandler		;03ed	cd 55 01	. U .
	ld a,042h		;03f0	3e 42		> B
	out (0fdh),a		;03f2	d3 fd		. .
	ld a,000h		;03f4	3e 00		> .
l03f6h:
	di			;03f6	f3		.
	ld sp,(0f975h)		;03f7	ed 7b 75 f9	. { u .
	im 2			;03fb	ed 5e		. ^
	ei			;03fd	fb		.
	ret			;03fe	c9		.
	ld a,(05f56h)		;03ff	3a 56 5f	: V _
	cp 003h			;0402	fe 03		. .
	jp nc,l0230h		;0404	d2 30 02	. 0 .
	pop bc			;0407	c1		.
	call 0f8feh		;0408	cd fe f8	. . .
	rrca			;040b	0f		.
	rrca			;040c	0f		.
	and 0c0h		;040d	e6 c0		. .
	ld b,a			;040f	47		G
	ld a,d			;0410	7a		z
	xor 080h		;0411	ee 80		. .
	cpl			;0413	2f		/
	ld c,0f7h		;0414	0e f7		. .
	out (c),a		;0416	ed 79		. y
	xor a			;0418	af		.
	ei			;0419	fb		.
	ret			;041a	c9		.
	ld bc,047fah		;041b	01 fa 47	. . G
	jp m,0fa24h		;041e	fa 24 fa	. $ .
	ld h,l			;0421	65		e
	jp m,0fa88h		;0422	fa 88 fa	. . .
	xor e			;0425	ab		.
	jp m,0faceh		;0426	fa ce fa	. . .
	pop af			;0429	f1		.
	jp m,0fb14h		;042a	fa 14 fb	. . .
	scf			;042d	37		7
	ei			;042e	fb		.
	nop			;042f	00		.
	nop			;0430	00		.
	nop			;0431	00		.
	nop			;0432	00		.
	inc d			;0433	14		.
	jr MonitorTable0436_end	;0434	18 46		. F
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MonitorTable0436 - service monitor lookup table (70 bytes).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MonitorTable0436' (start 0x0436 end 0x047c)
MonitorTable0436_start:
	defb 014h		;0436	14		.
	defb 0abh		;0437	ab		.
	defb 018h		;0438	18		.
	defb 000h		;0439	00		.
	defb 000h		;043a	00		.
	defb 000h		;043b	00		.
	defb 000h		;043c	00		.
	defb 000h		;043d	00		.
	defb 000h		;043e	00		.
	defb 000h		;043f	00		.
	defb 000h		;0440	00		.
	defb 000h		;0441	00		.
	defb 000h		;0442	00		.
	defb 000h		;0443	00		.
	defb 000h		;0444	00		.
	defb 000h		;0445	00		.
	defb 000h		;0446	00		.
	defb 000h		;0447	00		.
	defb 000h		;0448	00		.
	defb 000h		;0449	00		.
	defb 000h		;044a	00		.
	defb 000h		;044b	00		.
	defb 000h		;044c	00		.
	defb 000h		;044d	00		.
	defb 000h		;044e	00		.
	defb 000h		;044f	00		.
	defb 000h		;0450	00		.
	defb 000h		;0451	00		.
	defb 000h		;0452	00		.
	defb 000h		;0453	00		.
	defb 000h		;0454	00		.
	defb 000h		;0455	00		.
	defb 000h		;0456	00		.
	defb 000h		;0457	00		.
	defb 000h		;0458	00		.
	defb 000h		;0459	00		.
	defb 000h		;045a	00		.
	defb 000h		;045b	00		.
	defb 000h		;045c	00		.
	defb 000h		;045d	00		.
	defb 000h		;045e	00		.
	defb 000h		;045f	00		.
	defb 000h		;0460	00		.
	defb 000h		;0461	00		.
	defb 000h		;0462	00		.
	defb 000h		;0463	00		.
	defb 000h		;0464	00		.
	defb 000h		;0465	00		.
	defb 000h		;0466	00		.
	defb 000h		;0467	00		.
	defb 000h		;0468	00		.
	defb 000h		;0469	00		.
	defb 000h		;046a	00		.
	defb 000h		;046b	00		.
	defb 000h		;046c	00		.
	defb 000h		;046d	00		.
	defb 000h		;046e	00		.
	defb 000h		;046f	00		.
	defb 000h		;0470	00		.
	defb 000h		;0471	00		.
	defb 000h		;0472	00		.
	defb 000h		;0473	00		.
	defb 000h		;0474	00		.
	defb 000h		;0475	00		.
	defb 000h		;0476	00		.
	defb 000h		;0477	00		.
	defb 000h		;0478	00		.
	defb 000h		;0479	00		.
	defb 000h		;047a	00		.
	defb 000h		;047b	00		.
MonitorTable0436_end:
	nop			;047c	00		.
	nop			;047d	00		.
	nop			;047e	00		.
	nop			;047f	00		.
	nop			;0480	00		.
	nop			;0481	00		.
	nop			;0482	00		.
	nop			;0483	00		.
	nop			;0484	00		.
	nop			;0485	00		.
	nop			;0486	00		.
	nop			;0487	00		.
	nop			;0488	00		.
	nop			;0489	00		.
	nop			;048a	00		.
	nop			;048b	00		.
	nop			;048c	00		.
	nop			;048d	00		.
	nop			;048e	00		.
	nop			;048f	00		.
	nop			;0490	00		.
	nop			;0491	00		.
	nop			;0492	00		.
	nop			;0493	00		.
	nop			;0494	00		.
	nop			;0495	00		.
	nop			;0496	00		.
	nop			;0497	00		.
	nop			;0498	00		.
	nop			;0499	00		.
	nop			;049a	00		.
	nop			;049b	00		.
	nop			;049c	00		.
	nop			;049d	00		.
	nop			;049e	00		.
	nop			;049f	00		.
	nop			;04a0	00		.
	nop			;04a1	00		.
	nop			;04a2	00		.
	nop			;04a3	00		.
	nop			;04a4	00		.
	nop			;04a5	00		.
	nop			;04a6	00		.
	nop			;04a7	00		.
	nop			;04a8	00		.
	nop			;04a9	00		.
	nop			;04aa	00		.
	nop			;04ab	00		.
	nop			;04ac	00		.
	nop			;04ad	00		.
	nop			;04ae	00		.
	nop			;04af	00		.
	nop			;04b0	00		.
	nop			;04b1	00		.
	nop			;04b2	00		.
	nop			;04b3	00		.
	nop			;04b4	00		.
	nop			;04b5	00		.
	nop			;04b6	00		.
	nop			;04b7	00		.
	nop			;04b8	00		.
	nop			;04b9	00		.
	nop			;04ba	00		.
	nop			;04bb	00		.
	nop			;04bc	00		.
	nop			;04bd	00		.
	nop			;04be	00		.
	nop			;04bf	00		.
	nop			;04c0	00		.
	nop			;04c1	00		.
	nop			;04c2	00		.
	nop			;04c3	00		.
	nop			;04c4	00		.
	nop			;04c5	00		.
	nop			;04c6	00		.
	nop			;04c7	00		.
	nop			;04c8	00		.
	ld (de),a		;04c9	12		.
	ld sp,hl		;04ca	f9		.
	rst 38h			;04cb	ff		.
	inc bc			;04cc	03		.
	nop			;04cd	00		.
	nop			;04ce	00		.
	nop			;04cf	00		.
	ld (bc),a		;04d0	02		.
	ld d,b			;04d1	50		P
	nop			;04d2	00		.
	djnz l04d5h		;04d3	10 00		. .
l04d5h:
	ld bc,l0001h+1		;04d5	01 02 00	. . .
	nop			;04d8	00		.
	nop			;04d9	00		.
	inc d			;04da	14		.
	jr nz,l04ddh		;04db	20 00		  .
l04ddh:
	and b			;04dd	a0		.
	nop			;04de	00		.
	ex af,af'		;04df	08		.
	add a,b			;04e0	80		.
	nop			;04e1	00		.
	ld (bc),a		;04e2	02		.
	ld (bc),a		;04e3	02		.
	ld (bc),a		;04e4	02		.
	nop			;04e5	00		.
	nop			;04e6	00		.
	rst 38h			;04e7	ff		.
	ld bc,05050h		;04e8	01 50 50	. P P
	nop			;04eb	00		.
	nop			;04ec	00		.
	nop			;04ed	00		.
	rst 38h			;04ee	ff		.
	inc b			;04ef	04		.
	nop			;04f0	00		.
	nop			;04f1	00		.
	dec b			;04f2	05		.
	nop			;04f3	00		.
	sub (hl)		;04f4	96		.
	nop			;04f5	00		.
	ld de,l0200h		;04f6	11 00 02	. . .
	ld bc,WbootStub		;04f9	01 00 00	. . .
	call po,044c5h		;04fc	e4 c5 44	. . D
	nop			;04ff	00		.
	xor 002h		;0500	ee 02		. .
	ex af,af'		;0502	08		.
	add a,b			;0503	80		.
	nop			;0504	00		.
	ld (bc),a		;0505	02		.
	ld (bc),a		;0506	02		.
	ld (bc),a		;0507	02		.
	nop			;0508	00		.
	nop			;0509	00		.
	rst 38h			;050a	ff		.
	ld (bc),a		;050b	02		.
	ld d,b			;050c	50		P
	ld d,b			;050d	50		P
	nop			;050e	00		.
	nop			;050f	00		.
	nop			;0510	00		.
	rst 38h			;0511	ff		.
	ld (bc),a		;0512	02		.
	nop			;0513	00		.
	ld bc,l0001h		;0514	01 01 00	. . .
	nop			;0517	00		.
	nop			;0518	00		.
	add a,b			;0519	80		.
	add a,b			;051a	80		.
	nop			;051b	00		.
	ld bc,WbootStub		;051c	01 00 00	. . .
	nop			;051f	00		.
	nop			;0520	00		.
	add a,b			;0521	80		.
	nop			;0522	00		.
	nop			;0523	00		.
	nop			;0524	00		.
	ex af,af'		;0525	08		.
	add a,b			;0526	80		.
	nop			;0527	00		.
sub_0528h:
	xor a			;0528	af		.
	ret			;0529	c9		.
sub_052ah:
	ld hl,(05f44h)		;052a	2a 44 5f	* D _
	add hl,bc		;052d	09		.
	ld b,h			;052e	44		D
	ld c,l			;052f	4d		M
sub_0530h:
	or a			;0530	b7		.
	ex af,af'		;0531	08		.
	ld a,(05f44h)		;0532	3a 44 5f	: D _
	cp c			;0535	b9		.
	jr z,l053ah		;0536	28 02		( .
	jr nc,l0540h		;0538	30 06		0 .
l053ah:
	ld a,(05f46h)		;053a	3a 46 5f	: F _
	cp c			;053d	b9		.
	jr nc,l0544h		;053e	30 04		0 .
l0540h:
	ld c,a			;0540	4f		O
	ex af,af'		;0541	08		.
	scf			;0542	37		7
	ex af,af'		;0543	08		.
l0544h:
	ld a,(05f45h)		;0544	3a 45 5f	: E _
	cp b			;0547	b8		.
	jr z,l054ch		;0548	28 02		( .
	jr nc,l0552h		;054a	30 06		0 .
l054ch:
	ld a,(05f47h)		;054c	3a 47 5f	: G _
	cp b			;054f	b8		.
	jr nc,l0555h		;0550	30 03		0 .
l0552h:
	ld b,a			;0552	47		G
	scf			;0553	37		7
	ret			;0554	c9		.
l0555h:
	ex af,af'		;0555	08		.
	ret			;0556	c9		.
l0557h:
	call sub_052ah		;0557	cd 2a 05	. * .
l055ah:
	ld (05f42h),bc		;055a	ed 43 42 5f	. C B _
	call sub_0efch		;055e	cd fc 0e	. . .
	ret			;0561	c9		.
sub_0562h:
	dec (ix-06dh)		;0562	dd 35 93	. 5 .
	ret nz			;0565	c0		.
	ld a,(05f53h)		;0566	3a 53 5f	: S _
	ld (05f52h),a		;0569	32 52 5f	2 R _
	ld a,(05f40h)		;056c	3a 40 5f	: @ _
	xor 002h		;056f	ee 02		. .
	ld (05f40h),a		;0571	32 40 5f	2 @ _
	ex af,af'		;0574	08		.
	ld a,(05f58h)		;0575	3a 58 5f	: X _
	or a			;0578	b7		.
	ret nz			;0579	c0		.
	ex af,af'		;057a	08		.
	rra			;057b	1f		.
	and 001h		;057c	e6 01		. .
	call sub_0f1bh		;057e	cd 1b 0f	. . .
	ret			;0581	c9		.
sub_0582h:
	ld (ix-067h),001h	;0582	dd 36 99 01	. 6 . .
l0586h:
	dec (ix-067h)		;0586	dd 35 99	. 5 .
	jr z,l0591h		;0589	28 06		( .
	ret p			;058b	f0		.
	ld (ix-067h),000h	;058c	dd 36 99 00	. 6 . .
	ret			;0590	c9		.
l0591h:
	bit 1,(ix-07fh)		;0591	dd cb 81 4e	. . . N
	ret z			;0595	c8		.
	ld a,001h		;0596	3e 01		> .
	call sub_0f1bh		;0598	cd 1b 0f	. . .
	set 1,(ix-07fh)		;059b	dd cb 81 ce	. . . .
	ret			;059f	c9		.
sub_05a0h:
	ld a,(05f58h)		;05a0	3a 58 5f	: X _
	inc a			;05a3	3c		<
	ld (05f58h),a		;05a4	32 58 5f	2 X _
	dec a			;05a7	3d		=
	ret nz			;05a8	c0		.
	bit 1,(ix-07fh)		;05a9	dd cb 81 4e	. . . N
	ret z			;05ad	c8		.
	xor a			;05ae	af		.
	call sub_0f1bh		;05af	cd 1b 0f	. . .
	res 1,(ix-07fh)		;05b2	dd cb 81 8e	. . . .
	ret			;05b6	c9		.
	ld b,a			;05b7	47		G
	rlca			;05b8	07		.
	rlca			;05b9	07		.
	rlca			;05ba	07		.
	and 007h		;05bb	e6 07		. .
	ld hl,0df58h		;05bd	21 58 df	! X .
	call sub_1106h		;05c0	cd 06 11	. . .
	ld a,(hl)		;05c3	7e		~
	bit 4,b			;05c4	cb 60		. `
	jr z,l05cch		;05c6	28 04		( .
	rlca			;05c8	07		.
	rlca			;05c9	07		.
	rlca			;05ca	07		.
	rlca			;05cb	07		.
l05cch:
	and 0f0h		;05cc	e6 f0		. .
	xor b			;05ce	a8		.
	and 0f0h		;05cf	e6 f0		. .
	xor b			;05d1	a8		.
	ret			;05d2	c9		.
sub_05d3h:
	ret			;05d3	c9		.
	rst 38h			;05d4	ff		.
	ccf			;05d5	3f		?
	xor a			;05d6	af		.
	ld h,a			;05d7	67		g
	ld l,a			;05d8	6f		o
l05d9h:
	add a,(hl)		;05d9	86		.
	adc a,000h		;05da	ce 00		. .
	inc hl			;05dc	23		#
	rrca			;05dd	0f		.
	xor 0b9h		;05de	ee b9		. .
	dec bc			;05e0	0b		.
	ld d,a			;05e1	57		W
	ld a,b			;05e2	78		x
	or c			;05e3	b1		.
	ld a,d			;05e4	7a		z
	jr nz,l05d9h		;05e5	20 f2		  .
	dec a			;05e7	3d		=
	ret z			;05e8	c8		.
	ld a,r			;05e9	ed 5f		. _
	ld l,a			;05eb	6f		o
	ld h,0dch		;05ec	26 dc		& .
	inc (hl)		;05ee	34		4
	and 07fh		;05ef	e6 7f		. .
	ret nz			;05f1	c0		.
l05f2h:
	ld hl,l0614h		;05f2	21 14 06	! . .
	xor a			;05f5	af		.
l05f6h:
	xor (hl)		;05f6	ae		.
	inc hl			;05f7	23		#
	call nz,Rst20Handler	;05f8	c4 9d 0b	. . .
	jr nz,l05f6h		;05fb	20 f9		  .
	ld a,001h		;05fd	3e 01		> .
	rst 18h			;05ff	df		.
	ld hl,0df64h		;0600	21 64 df	! d .
	ld b,010h		;0603	06 10		. .
l0605h:
	inc (hl)		;0605	34		4
	inc hl			;0606	23		#
	djnz l0605h		;0607	10 fc		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MonitorTable0609 - service monitor lookup table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MonitorTable0609' (start 0x0609 end 0x062a)
MonitorTable0609_start:
	defb 0cdh		;0609	cd		.
	defb 0f3h		;060a	f3		.
	defb 00ah		;060b	0a		.
	defb 006h		;060c	06		.
	defb 019h		;060d	19		.
	defb 0fbh		;060e	fb		.
	defb 076h		;060f	76		v
l0610h:
	defb 010h		;0610	10		.
	defb 0fdh		;0611	fd		.
	defb 018h		;0612	18		.
	defb 0ech		;0613	ec		.
l0614h:
	defb 00dh		;0614	0d		.
	defb 016h		;0615	16		.
	defb 00ah		;0616	0a		.
	defb 058h		;0617	58		X
	defb 069h		;0618	69		i
	defb 043h		;0619	43		C
	defb 011h		;061a	11		.
	defb 013h		;061b	13		.
	defb 002h		;061c	02		.
	defb 008h		;061d	08		.
	defb 00eh		;061e	0e		.
	defb 001h		;061f	01		.
	defb 04ah		;0620	4a		J
	defb 00eh		;0621	0e		.
	defb 069h		;0622	69		i
	defb 069h		;0623	69		i
	defb 048h		;0624	48		H
	defb 009h		;0625	09		.
	defb 00dh		;0626	0d		.
	defb 018h		;0627	18		.
	defb 011h		;0628	11		.
	defb 001h		;0629	01		.
MonitorTable0609_end:
	ld c,d			;062a	4a		J
	ld c,070h		;062b	0e 70		. p
	ld (l1617h),hl		;062d	22 17 16	" . .
	nop			;0630	00		.
	ld d,e			;0631	53		S
	ld (hl),d		;0632	72		r
	rla			;0633	17		.
	ld d,016h		;0634	16 16		. .
	ld de,05474h		;0636	11 74 54	. t T
	dec de			;0639	1b		.
	ld c,a			;063a	4f		O
	ld b,e			;063b	43		C
	inc c			;063c	0c		.
	ld bc,01d1ah		;063d	01 1a 1d	. . .
	rlca			;0640	07		.
	dec de			;0641	1b		.
	djnz $+77		;0642	10 4b		. K
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Data0644 - three stray bytes between monitor code streams.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Data0644' (start 0x0644 end 0x0647)
Data0644_start:
	defb 000h		;0644	00		.
	defb 000h		;0645	00		.
	defb 02eh		;0646	2e		.
Data0644_end:
	bit 4,(ix-07eh)		;0647	dd cb 82 66	. . . f
	ret nz			;064b	c0		.
	xor a			;064c	af		.
	jr l0655h		;064d	18 06		. .
	call sub_10a5h		;064f	cd a5 10	. . .
	ld b,004h		;0652	06 04		. .
	ret c			;0654	d8		.
l0655h:
	ld bc,(05f44h)		;0655	ed 4b 44 5f	. K D _
l0659h:
	ld hl,(05f46h)		;0659	2a 46 5f	* F _
	sbc hl,bc		;065c	ed 42		. B
	inc h			;065e	24		$
	inc l			;065f	2c		,
	ex de,hl		;0660	eb		.
	jp l1008h		;0661	c3 08 10	. . .
	ld d,002h		;0664	16 02		. .
	ld hl,l0316h		;0666	21 16 03	! . .
	call sub_066fh		;0669	cd 6f 06	. o .
	jp l1008h		;066c	c3 08 10	. . .
sub_066fh:
	ld a,(05f46h)		;066f	3a 46 5f	: F _
	sub c			;0672	91		.
	ld e,a			;0673	5f		_
	inc e			;0674	1c		.
	ld a,d			;0675	7a		z
	ld d,001h		;0676	16 01		. .
	ret			;0678	c9		.
	call sub_066fh		;0679	cd 6f 06	. o .
	ld a,020h		;067c	3e 20		>  
	jp l0fddh		;067e	c3 dd 0f	. . .
sub_0681h:
	ld hl,l068dh		;0681	21 8d 06	! . .
	ld de,0df58h		;0684	11 58 df	. X .
	ld bc,l000ch		;0687	01 0c 00	. . .
	ldir			;068a	ed b0		. .
	ret			;068c	c9		.
l068dh:
	ld bc,04523h		;068d	01 23 45	. # E
	ld h,a			;0690	67		g
	ld bc,0ef23h		;0691	01 23 ef	. # .
	call sub_2301h		;0694	cd 01 23	. . #
	ld b,l			;0697	45		E
	ld h,a			;0698	67		g
	call sub_05d3h		;0699	cd d3 05	. . .
l069ch:
	ld bc,WbootStub		;069c	01 00 00	. . .
	jp l0557h		;069f	c3 57 05	. W .
l06a2h:
	call KbdSymMap0E5C_end	;06a2	cd a6 0e	. . .
	ld de,l3037h		;06a5	11 37 30	. 7 0
	call sub_08bch		;06a8	cd bc 08	. . .
	call sub_0681h		;06ab	cd 81 06	. . .
	ld a,(05f58h)		;06ae	3a 58 5f	: X _
	or a			;06b1	b7		.
	call nz,sub_0582h	;06b2	c4 82 05	. . .
	res 5,(ix-07fh)		;06b5	dd cb 81 ae	. . . .
	call sub_0991h		;06b9	cd 91 09	. . .
	ld (ix-07eh),000h	;06bc	dd 36 82 00	. 6 . .
	ld (ix-07bh),000h	;06c0	dd 36 85 00	. 6 . .
	ld (ix-07ah),000h	;06c4	dd 36 86 00	. 6 . .
	ld (ix-079h),04fh	;06c8	dd 36 87 4f	. 6 . O
	ld (ix-078h),018h	;06cc	dd 36 88 18	. 6 . .
	defb 0cdh		;06d0	cd		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MonitorTable06D1 - service monitor lookup table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MonitorTable06D1' (start 0x06d1 end 0x06f1)
MonitorTable06D1_start:
	defb 09ch		;06d1	9c		.
	defb 006h		;06d2	06		.
	defb 0edh		;06d3	ed		.
	defb 04bh		;06d4	4b		K
	defb 044h		;06d5	44		D
	defb 05fh		;06d6	5f		_
	defb 0cdh		;06d7	cd		.
	defb 079h		;06d8	79		y
	defb 006h		;06d9	06		.
	defb 0ddh		;06da	dd		.
	defb 046h		;06db	46		F
	defb 084h		;06dc	84		.
	defb 004h		;06dd	04		.
	defb 0ddh		;06de	dd		.
	defb 04eh		;06df	4e		N
	defb 085h		;06e0	85		.
	defb 02ah		;06e1	2a		*
	defb 046h		;06e2	46		F
	defb 05fh		;06e3	5f		_
	defb 0b7h		;06e4	b7		.
	defb 0edh		;06e5	ed		.
	defb 042h		;06e6	42		B
	defb 024h		;06e7	24		$
	defb 0c8h		;06e8	c8		.
	defb 02ch		;06e9	2c		,
	defb 0ebh		;06ea	eb		.
	defb 0c3h		;06eb	c3		.
	defb 07ch		;06ec	7c		|
	defb 006h		;06ed	06		.
	defb 0cdh		;06ee	cd		.
	defb 0b7h		;06ef	b7		.
	defb 005h		;06f0	05		.
MonitorTable06D1_end:
	call sub_0eb8h		;06f1	cd b8 0e	. . .
	ld a,(05f42h)		;06f4	3a 42 5f	: B _
	cp (ix-079h)		;06f7	dd be 87	. . .
	jr nc,l0703h		;06fa	30 07		0 .
	inc a			;06fc	3c		<
	ld (05f42h),a		;06fd	32 42 5f	2 B _
	jp l0ee8h		;0700	c3 e8 0e	. . .
l0703h:
	ld b,(ix-07ch)		;0703	dd 46 84	. F .
	ld c,(ix-07bh)		;0706	dd 4e 85	. N .
	inc b			;0709	04		.
l070ah:
	call sub_0530h		;070a	cd 30 05	. 0 .
	push bc			;070d	c5		.
	call c,Data0644_end	;070e	dc 47 06	. G .
	pop bc			;0711	c1		.
l0712h:
	jp l055ah		;0712	c3 5a 05	. Z .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MonitorTable0715 - service monitor lookup table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MonitorTable0715' (start 0x0715 end 0x073a)
MonitorTable0715_start:
	defb 0feh		;0715	fe		.
	defb 020h		;0716	20		 
	defb 0d2h		;0717	d2		.
	defb 0eeh		;0718	ee		.
	defb 006h		;0719	06		.
	defb 0edh		;071a	ed		.
	defb 04bh		;071b	4b		K
	defb 042h		;071c	42		B
	defb 05fh		;071d	5f		_
	defb 0cdh		;071e	cd		.
	defb 0a7h		;071f	a7		.
	defb 010h		;0720	10		.
	defb 040h		;0721	40		@
	defb 00dh		;0722	0d		.
	defb 00ah		;0723	0a		.
	defb 00eh		;0724	0e		.
	defb 00fh		;0725	0f		.
	defb 01bh		;0726	1b		.
	defb 007h		;0727	07		.
	defb 008h		;0728	08		.
	defb 009h		;0729	09		.
	defb 0c9h		;072a	c9		.
	defb 000h		;072b	00		.
	defb 018h		;072c	18		.
	defb 03ch		;072d	3c		<
	defb 018h		;072e	18		.
	defb 047h		;072f	47		G
	defb 018h		;0730	18		.
	defb 059h		;0731	59		Y
	defb 018h		;0732	18		.
	defb 02fh		;0733	2f		/
	defb 018h		;0734	18		.
	defb 009h		;0735	09		.
	defb 018h		;0736	18		.
	defb 01eh		;0737	1e		.
	defb 018h		;0738	18		.
	defb 0cfh		;0739	cf		.
MonitorTable0715_end:
	ld c,(ix-07bh)		;073a	dd 4e 85	. N .
	jr l0712h		;073d	18 d3		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MonitorTable073F - service monitor lookup table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MonitorTable073F' (start 0x073f end 0x0766)
MonitorTable073F_start:
	defb 0ddh		;073f	dd		.
	defb 0cbh		;0740	cb		.
	defb 081h		;0741	81		.
	defb 0eeh		;0742	ee		.
	defb 03eh		;0743	3e		>
	defb 001h		;0744	01		.
	defb 0cdh		;0745	cd		.
	defb 03bh		;0746	3b		;
	defb 039h		;0747	39		9
	defb 021h		;0748	21		!
	defb 05ch		;0749	5c		\
	defb 0dfh		;074a	df		.
	defb 011h		;074b	11		.
	defb 058h		;074c	58		X
	defb 0dfh		;074d	df		.
	defb 001h		;074e	01		.
	defb 004h		;074f	04		.
	defb 000h		;0750	00		.
	defb 0edh		;0751	ed		.
	defb 0b0h		;0752	b0		.
	defb 0c3h		;0753	c3		.
	defb 091h		;0754	91		.
	defb 009h		;0755	09		.
	defb 0ddh		;0756	dd		.
	defb 0cbh		;0757	cb		.
	defb 081h		;0758	81		.
	defb 0aeh		;0759	ae		.
	defb 0afh		;075a	af		.
	defb 0cdh		;075b	cd		.
	defb 03bh		;075c	3b		;
	defb 039h		;075d	39		9
	defb 021h		;075e	21		!
	defb 060h		;075f	60		`
	defb 0dfh		;0760	df		.
	defb 018h		;0761	18		.
	defb 0e8h		;0762	e8		.
	defb 021h		;0763	21		!
	defb 0a1h		;0764	a1		.
	defb 007h		;0765	07		.
MonitorTable073F_end:
	ld (0df44h),hl		;0766	22 44 df	" D .
	ret			;0769	c9		.
l076ah:
	ld a,020h		;076a	3e 20		>  
	call MonitorTable06D1_end	;076c	cd f1 06	. . .
	ld a,(05f42h)		;076f	3a 42 5f	: B _
	and 007h		;0772	e6 07		. .
	jr nz,l076ah		;0774	20 f4		  .
	ret			;0776	c9		.
	dec c			;0777	0d		.
	call sub_0530h		;0778	cd 30 05	. 0 .
	jr nc,l0712h		;077b	30 95		0 .
	ld c,(ix-079h)		;077d	dd 4e 87	. N .
	dec b			;0780	05		.
	call sub_0530h		;0781	cd 30 05	. 0 .
	jr nc,l0712h		;0784	30 8c		0 .
	ld b,(ix-078h)		;0786	dd 46 88	. F .
	jr l0712h		;0789	18 87		. .
	ld hl,(05f59h)		;078b	2a 59 5f	* Y _
l078eh:
	ld a,(05f5ch)		;078e	3a 5c 5f	: \ _
	ld c,(ix-064h)		;0791	dd 4e 9c	. N .
	di			;0794	f3		.
l0795h:
	ld b,h			;0795	44		D
	out (c),a		;0796	ed 79		. y
l0798h:
	djnz l0798h		;0798	10 fe		. .
	xor 010h		;079a	ee 10		. .
	dec l			;079c	2d		-
	jr nz,l0795h		;079d	20 f6		  .
	ei			;079f	fb		.
	ret			;07a0	c9		.
	sub 040h		;07a1	d6 40		. @
	jr nc,l07aeh		;07a3	30 09		0 .
l07a5h:
	ld hl,MonitorTable0715_start	;07a5	21 15 07	! . .
	ld (ix-075h),080h	;07a8	dd 36 8b 80	. 6 . .
	jr MonitorTable073F_end	;07ac	18 b8		. .
l07aeh:
	cp 024h			;07ae	fe 24		. $
	jr nc,l07a5h		;07b0	30 f3		0 .
	inc a			;07b2	3c		<
	ld (05f5eh),a		;07b3	32 5e 5f	2 ^ _
	dec a			;07b6	3d		=
	ld hl,l0851h		;07b7	21 51 08	! Q .
	call sub_1106h		;07ba	cd 06 11	. . .
	ld a,(hl)		;07bd	7e		~
	or a			;07be	b7		.
	jr z,l07e7h		;07bf	28 26		( &
l07c1h:
	ld hl,05f92h		;07c1	21 92 5f	! . _
	ld (05f5fh),a		;07c4	32 5f 5f	2 _ _
	ld (05f60h),hl		;07c7	22 60 5f	" ` _
	ld hl,l07cfh		;07ca	21 cf 07	! . .
	jr MonitorTable073F_end	;07cd	18 97		. .
l07cfh:
	ld hl,(05f60h)		;07cf	2a 60 5f	* ` _
	sub 020h		;07d2	d6 20		.  
	jr nc,l07deh		;07d4	30 08		0 .
	ld (ix-061h),0ffh	;07d6	dd 36 9f ff	. 6 . .
	ld (ix-075h),081h	;07da	dd 36 8b 81	. 6 . .
l07deh:
	ld (hl),a		;07de	77		w
	inc hl			;07df	23		#
	ld (05f60h),hl		;07e0	22 60 5f	" ` _
	dec (ix-060h)		;07e3	dd 35 a0	. 5 .
	ret nz			;07e6	c0		.
l07e7h:
	ld hl,MonitorTable0715_start	;07e7	21 15 07	! . .
	ld (0df44h),hl		;07ea	22 44 df	" D .
	ld a,(05f5eh)		;07ed	3a 5e 5f	: ^ _
	inc a			;07f0	3c		<
	ret z			;07f1	c8		.
	dec a			;07f2	3d		=
	call sub_0528h		;07f3	cd 28 05	. ( .
	ld bc,(05f42h)		;07f6	ed 4b 42 5f	. K B _
	ld a,(05f92h)		;07fa	3a 92 5f	: . _
	ex af,af'		;07fd	08		.
	ld a,(05f5eh)		;07fe	3a 5e 5f	: ^ _
	call sub_11efh		;0801	cd ef 11	. . .
	dec h			;0804	25		%
	and l			;0805	a5		.
	rlca			;0806	07		.
	or (hl)			;0807	b6		.
	ld a,(bc)		;0808	0a		.
	xor 006h		;0809	ee 06		. .
	ld a,b			;080b	78		x
	ex af,af'		;080c	08		.
	add a,h			;080d	84		.
	ex af,af'		;080e	08		.
	adc a,a			;080f	8f		.
	ex af,af'		;0810	08		.
	sbc a,d			;0811	9a		.
	ex af,af'		;0812	08		.
	ret nc			;0813	d0		.
	ld b,0b8h		;0814	06 b8		. .
	ex af,af'		;0816	08		.
	ld (hl),l		;0817	75		u
	ex af,af'		;0818	08		.
	sbc a,c			;0819	99		.
	ld b,0a5h		;081a	06 a5		. .
	ex af,af'		;081c	08		.
	rst 10h			;081d	d7		.
	ld b,079h		;081e	06 79		. y
	ld b,0b8h		;0820	06 b8		. .
	ex af,af'		;0822	08		.
	jr nc,l082eh		;0823	30 09		0 .
	adc a,h			;0825	8c		.
	ld a,(bc)		;0826	0a		.
	ld (hl),l		;0827	75		u
	ex af,af'		;0828	08		.
	ld h,h			;0829	64		d
	ld b,067h		;082a	06 67		. g
	ld b,064h		;082c	06 64		. d
l082eh:
	ld a,(bc)		;082e	0a		.
	ret pe			;082f	e8		.
	add hl,bc		;0830	09		.
	ex de,hl		;0831	eb		.
	add hl,bc		;0832	09		.
	ld a,b			;0833	78		x
	add hl,bc		;0834	09		.
	ld (hl),l		;0835	75		u
	ex af,af'		;0836	08		.
	add a,009h		;0837	c6 09		. .
	ld c,a			;0839	4f		O
	ld b,0bfh		;083a	06 bf		. .
	add hl,bc		;083c	09		.
	jp p,00c09h		;083d	f2 09 0c	. . .
	ld a,(bc)		;0840	0a		.
	ld hl,04e0ah		;0841	21 0a 4e	! . N
	ld a,(bc)		;0844	0a		.
	dec b			;0845	05		.
	add hl,bc		;0846	09		.
	ret nz			;0847	c0		.
	ld b,0a9h		;0848	06 a9		. .
	ld a,(bc)		;084a	0a		.
	dec d			;084b	15		.
	ld a,(bc)		;084c	0a		.
	add hl,de		;084d	19		.
	ld a,(bc)		;084e	0a		.
	add a,a			;084f	87		.
	ld a,(bc)		;0850	0a		.
l0851h:
	ld bc,WbootStub		;0851	01 00 00	. . .
	nop			;0854	00		.
	nop			;0855	00		.
	nop			;0856	00		.
	ld (bc),a		;0857	02		.
	nop			;0858	00		.
	nop			;0859	00		.
	nop			;085a	00		.
	nop			;085b	00		.
	nop			;085c	00		.
	djnz $+3		;085d	10 01		. .
	ld bc,l0001h		;085f	01 01 00	. . .
	nop			;0862	00		.
	add hl,bc		;0863	09		.
	nop			;0864	00		.
	nop			;0865	00		.
	ld (bc),a		;0866	02		.
	nop			;0867	00		.
	inc b			;0868	04		.
	ld bc,00102h		;0869	01 02 01	. . .
	ld bc,l0202h		;086c	01 02 02	. . .
	ld (bc),a		;086f	02		.
	nop			;0870	00		.
	djnz l0874h		;0871	10 01		. .
	ld (bc),a		;0873	02		.
l0874h:
	djnz l08b4h		;0874	10 3e		. >
	rst 38h			;0876	ff		.
	ret			;0877	c9		.
	dec b			;0878	05		.
	call sub_0530h		;0879	cd 30 05	. 0 .
	jr nc,l0881h		;087c	30 03		0 .
	ld b,(ix-078h)		;087e	dd 46 88	. F .
l0881h:
	jp l055ah		;0881	c3 5a 05	. Z .
	inc b			;0884	04		.
	call sub_0530h		;0885	cd 30 05	. 0 .
	jr nc,l0881h		;0888	30 f7		0 .
	ld b,(ix-07ah)		;088a	dd 46 86	. F .
	jr l0881h		;088d	18 f2		. .
	inc c			;088f	0c		.
	call sub_0530h		;0890	cd 30 05	. 0 .
	jr nc,l0881h		;0893	30 ec		0 .
	ld c,(ix-07bh)		;0895	dd 4e 85	. N .
	jr l0881h		;0898	18 e7		. .
	dec c			;089a	0d		.
	call sub_0530h		;089b	cd 30 05	. 0 .
	jr nc,l0881h		;089e	30 e1		0 .
	ld c,(ix-079h)		;08a0	dd 4e 87	. N .
	jr l0881h		;08a3	18 dc		. .
	inc b			;08a5	04		.
	dec b			;08a6	05		.
	scf			;08a7	37		7
	jr z,l08aeh		;08a8	28 04		( .
	dec b			;08aa	05		.
	call sub_0530h		;08ab	cd 30 05	. 0 .
l08aeh:
	push bc			;08ae	c5		.
	ld a,001h		;08af	3e 01		> .
	call c,l0655h		;08b1	dc 55 06	. U .
l08b4h:
	pop bc			;08b4	c1		.
	jp l055ah		;08b5	c3 5a 05	. Z .
	ld de,(05f92h)		;08b8	ed 5b 92 5f	. [ . _
sub_08bch:
	ld a,e			;08bc	7b		{
	ld bc,(05f48h)		;08bd	ed 4b 48 5f	. K H _
	call sub_10a5h		;08c1	cd a5 10	. . .
	dec d			;08c4	15		.
	ld c,(hl)		;08c5	4e		N
	ld c,c			;08c6	49		I
	ret c			;08c7	d8		.
	nop			;08c8	00		.
	ld b,a			;08c9	47		G
	nop			;08ca	00		.
	ld a,d			;08cb	7a		z
	call sub_10a5h		;08cc	cd a5 10	. . .
	dec d			;08cf	15		.
	ld c,(hl)		;08d0	4e		N
	ld c,c			;08d1	49		I
	ret c			;08d2	d8		.
	nop			;08d3	00		.
	ld c,a			;08d4	4f		O
	nop			;08d5	00		.
	ld (05f48h),bc		;08d6	ed 43 48 5f	. C H _
	bit 7,b			;08da	cb 78		. x
	jr z,l08e3h		;08dc	28 05		( .
	ld a,c			;08de	79		y
	call FdcTable0B63_end	;08df	cd 6b 0b	. k .
	ld b,a			;08e2	47		G
l08e3h:
	bit 7,c			;08e3	cb 79		. y
	jr z,l08ech		;08e5	28 05		( .
	ld a,b			;08e7	78		x
	call FdcTable0B63_end	;08e8	cd 6b 0b	. k .
	ld c,a			;08eb	4f		O
l08ech:
	call sub_08f2h		;08ec	cd f2 08	. . .
	jp l0f3fh		;08ef	c3 3f 0f	. ? .
sub_08f2h:
	ld a,b			;08f2	78		x
	xor c			;08f3	a9		.
	and 0f8h		;08f4	e6 f8		. .
	xor c			;08f6	a9		.
	rl c			;08f7	cb 11		. .
	xor c			;08f9	a9		.
	and 0efh		;08fa	e6 ef		. .
	xor c			;08fc	a9		.
	rla			;08fd	17		.
	rla			;08fe	17		.
	rla			;08ff	17		.
	xor b			;0900	a8		.
	and 0f8h		;0901	e6 f8		. .
	xor b			;0903	a8		.
	ret			;0904	c9		.
	call sub_10a5h		;0905	cd a5 10	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MonitorTable0908 - "1IRBFTO" eight-byte table fragment.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MonitorTable0908' (start 0x0908 end 0x0910)
MonitorTable0908_start:
	defb 031h		;0908	31		1
	defb 049h		;0909	49		I
	defb 052h		;090a	52		R
	defb 042h		;090b	42		B
	defb 046h		;090c	46		F
	defb 054h		;090d	54		T
	defb 04fh		;090e	4f		O
	defb 000h		;090f	00		.
MonitorTable0908_end:
	ret c			;0910	d8		.
	and 07fh		;0911	e6 7f		. .
	ld b,a			;0913	47		G
	inc b			;0914	04		.
	xor a			;0915	af		.
	scf			;0916	37		7
l0917h:
	rla			;0917	17		.
	djnz l0917h		;0918	10 fd		. .
	ld c,a			;091a	4f		O
	ld a,(05f93h)		;091b	3a 93 5f	: . _
	call sub_10a5h		;091e	cd a5 10	. . .
	ld b,002h		;0921	06 02		. .
	ret c			;0923	d8		.
	rra			;0924	1f		.
	sbc a,a			;0925	9f		.
	ld b,(ix-07eh)		;0926	dd 46 82	. F .
	xor b			;0929	a8		.
	and c			;092a	a1		.
	xor b			;092b	a8		.
	ld (05f41h),a		;092c	32 41 5f	2 A _
	ret			;092f	c9		.
sub_0930h:
	ld a,(05f92h)		;0930	3a 92 5f	: . _
	sub 010h		;0933	d6 10		. .
	ld b,a			;0935	47		G
	call sub_0968h		;0936	cd 68 09	. h .
	ld (05f5dh),a		;0939	32 5d 5f	2 ] _
	call sub_3872h		;093c	cd 72 38	. r 8
	ld (ix-067h),001h	;093f	dd 36 99 01	. 6 . .
	ld h,040h		;0943	26 40		& @
	call sub_095ch		;0945	cd 5c 09	. \ .
	ld h,060h		;0948	26 60		& `
	call sub_095ch		;094a	cd 5c 09	. \ .
	ld h,0c0h		;094d	26 c0		& .
	call sub_095ch		;094f	cd 5c 09	. \ .
	ld h,0e0h		;0952	26 e0		& .
	call sub_095ch		;0954	cd 5c 09	. \ .
	ld a,002h		;0957	3e 02		> .
	jp l06a2h		;0959	c3 a2 06	. . .
sub_095ch:
	ld l,000h		;095c	2e 00		. .
	ld d,h			;095e	54		T
	ld e,001h		;095f	1e 01		. .
	ld bc,01f3fh		;0961	01 3f 1f	. ? .
	ld (hl),l		;0964	75		u
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ServiceLdirCopy - the "service session wait loop".
; LDIR + RET.  The emulator M1 trace shows long runs (389+) of
; consecutive fetches at this address during disk operations: that is
; not a poll loop but LDIR re-executing itself once per copied byte.
; Refines the note in ../monitor-rom.md ("0x0965 - Service Session
; Wait Loop").
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ServiceLdirCopy:
	ldir			;0965	ed b0		. .
	ret			;0967	c9		.
sub_0968h:
	ld a,(05f5dh)		;0968	3a 5d 5f	: ] _
	and 0f8h		;096b	e6 f8		. .
	dec b			;096d	05		.
	jr z,l0975h		;096e	28 05		( .
	dec b			;0970	05		.
	ret nz			;0971	c0		.
	or 002h			;0972	f6 02		. .
	ret			;0974	c9		.
l0975h:
	or 006h			;0975	f6 06		. .
	ret			;0977	c9		.
	call sub_10a5h		;0978	cd a5 10	. . .
	dec b			;097b	05		.
	ret c			;097c	d8		.
	ld c,a			;097d	4f		O
	ld a,(05f93h)		;097e	3a 93 5f	: . _
	call sub_10a5h		;0981	cd a5 10	. . .
	dec b			;0984	05		.
	ret c			;0985	d8		.
	rrca			;0986	0f		.
	rrca			;0987	0f		.
	rrca			;0988	0f		.
	rrca			;0989	0f		.
	xor c			;098a	a9		.
	and 0f0h		;098b	e6 f0		. .
	xor c			;098d	a9		.
	ld (05f62h),a		;098e	32 62 5f	2 b _
sub_0991h:
	ld a,(05f62h)		;0991	3a 62 5f	: b _
	bit 5,(ix-07fh)		;0994	dd cb 81 6e	. . . n
	jr nz,l099eh		;0998	20 04		  .
	rra			;099a	1f		.
	rra			;099b	1f		.
	rra			;099c	1f		.
	rra			;099d	1f		.
l099eh:
	ld c,a			;099e	4f		O
	xor (ix-063h)		;099f	dd ae 9d	. . .
	and 007h		;09a2	e6 07		. .
	xor (ix-063h)		;09a4	dd ae 9d	. . .
	ld (05f5ch),a		;09a7	32 5c 5f	2 \ _
	ld a,(05f5bh)		;09aa	3a 5b 5f	: [ _
	xor c			;09ad	a9		.
	and 0f7h		;09ae	e6 f7		. .
	xor c			;09b0	a9		.
	xor 008h		;09b1	ee 08		. .
	ld (05f5bh),a		;09b3	32 5b 5f	2 [ _
sub_09b6h:
	ld c,(ix-064h)		;09b6	dd 4e 9c	. N .
	ld a,(05f5ch)		;09b9	3a 5c 5f	: \ _
	out (c),a		;09bc	ed 79		. y
	ret			;09be	c9		.
	ld c,(ix-02ch)		;09bf	dd 4e d4	. N .
	ld b,a			;09c2	47		G
	jp l0557h		;09c3	c3 57 05	. W .
	ld a,(05f94h)		;09c6	3a 94 5f	: . _
	cp (ix-02dh)		;09c9	dd be d3	. . .
	ret c			;09cc	d8		.
	ld l,a			;09cd	6f		o
	cp 050h			;09ce	fe 50		. P
	ret nc			;09d0	d0		.
	ld a,(05f95h)		;09d1	3a 95 5f	: . _
	cp (ix-02ch)		;09d4	dd be d4	. . .
	ret c			;09d7	d8		.
	cp 019h			;09d8	fe 19		. .
	ret nc			;09da	d0		.
	ld h,a			;09db	67		g
	ld (05f46h),hl		;09dc	22 46 5f	" F _
	ld hl,(05f92h)		;09df	2a 92 5f	* . _
	ld (05f44h),hl		;09e2	22 44 5f	" D _
	jp l069ch		;09e5	c3 9c 06	. . .
	ld a,001h		;09e8	3e 01		> .
l09eah:
	ld c,0afh		;09ea	0e af		. .
	ld c,(ix-07bh)		;09ec	dd 4e 85	. N .
	jp l0659h		;09ef	c3 59 06	. Y .
	call sub_10a5h		;09f2	cd a5 10	. . .
	ld b,003h		;09f5	06 03		. .
	ret c			;09f7	d8		.
	res 2,(ix-07fh)		;09f8	dd cb 81 96	. . . .
	dec a			;09fc	3d		=
	jp z,l0586h		;09fd	ca 86 05	. . .
	inc a			;0a00	3c		<
	ex af,af'		;0a01	08		.
	call sub_05a0h		;0a02	cd a0 05	. . .
	ex af,af'		;0a05	08		.
	ret z			;0a06	c8		.
	set 2,(ix-07fh)		;0a07	dd cb 81 d6	. . . .
	ret			;0a0b	c9		.
	call sub_10a5h		;0a0c	cd a5 10	. . .
	rlca			;0a0f	07		.
	jr nz,l09eah		;0a10	20 d8		  .
	call sub_0a3fh		;0a12	cd 3f 0a	. ? .
	ld (05f53h),a		;0a15	32 53 5f	2 S _
	ret			;0a18	c9		.
	ld (05f6ch),a		;0a19	32 6c 5f	2 l _
	ld a,(05f93h)		;0a1c	3a 93 5f	: . _
	jr l0a39h		;0a1f	18 18		. .
	call sub_10a5h		;0a21	cd a5 10	. . .
	rlca			;0a24	07		.
	jr nz,$-38		;0a25	20 d8		  .
	call sub_0a3fh		;0a27	cd 3f 0a	. ? .
	ld (05f6ch),a		;0a2a	32 6c 5f	2 l _
	ld a,(05f93h)		;0a2d	3a 93 5f	: . _
	call sub_10a5h		;0a30	cd a5 10	. . .
	rlca			;0a33	07		.
	jr nz,$-38		;0a34	20 d8		  .
	call sub_0a3fh		;0a36	cd 3f 0a	. ? .
l0a39h:
	add a,a			;0a39	87		.
	dec a			;0a3a	3d		=
	ld (05f6bh),a		;0a3b	32 6b 5f	2 k _
	ret			;0a3e	c9		.
sub_0a3fh:
	inc a			;0a3f	3c		<
	cp 010h			;0a40	fe 10		. .
	ret c			;0a42	d8		.
	sub 008h		;0a43	d6 08		. .
	cp 010h			;0a45	fe 10		. .
	jr c,l0a4ch		;0a47	38 03		8 .
	sub 008h		;0a49	d6 08		. .
	add a,a			;0a4b	87		.
l0a4ch:
	add a,a			;0a4c	87		.
	ret			;0a4d	c9		.
	call sub_10a5h		;0a4e	cd a5 10	. . .
	ld b,008h		;0a51	06 08		. .
	ret c			;0a53	d8		.
	ld b,a			;0a54	47		G
	ld a,(05f93h)		;0a55	3a 93 5f	: . _
	call sub_10a5h		;0a58	cd a5 10	. . .
	ld b,008h		;0a5b	06 08		. .
	ret c			;0a5d	d8		.
	cp b			;0a5e	b8		.
	ret c			;0a5f	d8		.
	ld c,a			;0a60	4f		O
	jp l0f44h		;0a61	c3 44 0f	. D .
	call sub_10a5h		;0a64	cd a5 10	. . .
	ld b,003h		;0a67	06 03		. .
	ret c			;0a69	d8		.
	add a,a			;0a6a	87		.
	ld hl,0df58h		;0a6b	21 58 df	! X .
	call sub_1105h		;0a6e	cd 05 11	. . .
	ld de,05f93h		;0a71	11 93 5f	. . _
	ld b,008h		;0a74	06 08		. .
l0a76h:
	ld a,(de)		;0a76	1a		.
	inc de			;0a77	13		.
	call sub_10a5h		;0a78	cd a5 10	. . .
	dec b			;0a7b	05		.
	ret c			;0a7c	d8		.
	rld			;0a7d	ed 6f		. o
	bit 0,b			;0a7f	cb 40		. @
	jr z,l0a84h		;0a81	28 01		( .
	inc hl			;0a83	23		#
l0a84h:
	djnz l0a76h		;0a84	10 f0		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MonitorTable0A86 - monitor code/data mix island (RET, JP $0528,
; CALL $10A5, JR pair, LD (5F95),A ...).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
	ret			;0a86	c9		.
	ld c,000h		;0a87	0e 00		. .
	jp sub_0528h		;0a89	c3 28 05	. ( .
	call sub_10a5h		;0a8c	cd a5 10	. . .
	dec d			;0a8f	15		.
	ld d,d			;0a90	52		R
	ld d,h			;0a91	54		T
	ret c			;0a92	d8		.
	nop			;0a93	00		.
	jr l0a98h		;0a94	18 02		. .
	jr sub_0aa4h		;0a96	18 0c		. .
l0a98h:
	ld (05f95h),a		;0a98	32 95 5f	2 . _
	xor a			;0a9b	af		.
	ld (05f5eh),a		;0a9c	32 5e 5f	2 ^ _
l0a9fh:
	ld a,003h		;0a9f	3e 03		> .
	jp l07c1h		;0aa1	c3 c1 07	. . .
sub_0aa4h:
	ld hl,l3719h		;0aa4	21 19 37	! . 7
	jr sub_0aach		;0aa7	18 03		. .
	ld hl,05f92h		;0aa9	21 92 5f	! . _
sub_0aach:
	ld de,0df64h		;0aac	11 64 df	. d .
	ld bc,Rst10Vector	;0aaf	01 10 00	. . .
	ldir			;0ab2	ed b0		. .
	jr l0af3h		;0ab4	18 3d		. =
	ld b,003h		;0ab6	06 03		. .
	ld d,000h		;0ab8	16 00		. .
	ld hl,05f92h		;0aba	21 92 5f	! . _
l0abdh:
	ld a,(hl)		;0abd	7e		~
	call sub_10a5h		;0abe	cd a5 10	. . .
	ld b,004h		;0ac1	06 04		. .
	ret c			;0ac3	d8		.
	cpl			;0ac4	2f		/
	rra			;0ac5	1f		.
	rra			;0ac6	1f		.
	rl d			;0ac7	cb 12		. .
	rla			;0ac9	17		.
	sbc a,a			;0aca	9f		.
	and 008h		;0acb	e6 08		. .
	or d			;0acd	b2		.
	ld d,a			;0ace	57		W
	inc hl			;0acf	23		#
	djnz l0abdh		;0ad0	10 eb		. .
	add a,a			;0ad2	87		.
	add a,a			;0ad3	87		.
	xor d			;0ad4	aa		.
	and 0fch		;0ad5	e6 fc		. .
	xor d			;0ad7	aa		.
	or 00ch			;0ad8	f6 0c		. .
	ld d,a			;0ada	57		W
	ld hl,0df64h		;0adb	21 64 df	! d .
	ld a,(05f95h)		;0ade	3a 95 5f	: . _
	and 07fh		;0ae1	e6 7f		. .
	call sub_1106h		;0ae3	cd 06 11	. . .
	ld (hl),d		;0ae6	72		r
	inc (ix-02ah)		;0ae7	dd 34 d6	. 4 .
	jp p,l0af3h		;0aea	f2 f3 0a	. . .
	bit 4,(ix-02ah)		;0aed	dd cb d6 66	. . . f
	jr z,l0a9fh		;0af1	28 ac		( .
l0af3h:
	di			;0af3	f3		.
	ld a,i			;0af4	ed 57		. W
	push af			;0af6	f5		.
	ld a,03eh		;0af7	3e 3e		> >
	ld i,a			;0af9	ed 47		. G
	im 2			;0afb	ed 5e		. ^
	ld a,(05f5dh)		;0afd	3a 5d 5f	: ] _
	ld bc,0bd77h		;0b00	01 77 bd	. w .
	out (c),a		;0b03	ed 79		. y
	ld a,(05f5ch)		;0b05	3a 5c 5f	: \ _
	and 0f8h		;0b08	e6 f8		. .
	ld hl,0df64h		;0b0a	21 64 df	! d .
	ld c,0ffh		;0b0d	0e ff		. .
	ld d,008h		;0b0f	16 08		. .
	exx			;0b11	d9		.
	ld c,(ix-064h)		;0b12	dd 4e 9c	. N .
	set 3,c			;0b15	cb d9		. .
	exx			;0b17	d9		.
	ei			;0b18	fb		.
	halt			;0b19	76		v
	di			;0b1a	f3		.
l0b1bh:
	exx			;0b1b	d9		.
	out (c),a		;0b1c	ed 79		. y
	exx			;0b1e	d9		.
	outi			;0b1f	ed a3		. .
	inc a			;0b21	3c		<
	dec d			;0b22	15		.
	jr nz,l0b1bh		;0b23	20 f6		  .
	exx			;0b25	d9		.
	res 3,c			;0b26	cb 99		. .
	exx			;0b28	d9		.
	sub 008h		;0b29	d6 08		. .
	ld d,008h		;0b2b	16 08		. .
l0b2dh:
	exx			;0b2d	d9		.
	out (c),a		;0b2e	ed 79		. y
	exx			;0b30	d9		.
	outi			;0b31	ed a3		. .
	inc a			;0b33	3c		<
	dec d			;0b34	15		.
	jr nz,l0b2dh		;0b35	20 f6		  .
	add a,000h		;0b37	c6 00		. .
	call sub_09b6h		;0b39	cd b6 09	. . .
	pop af			;0b3c	f1		.
	ld i,a			;0b3d	ed 47		. G
	im 1			;0b3f	ed 56		. V
	call sub_3872h		;0b41	cd 72 38	. r 8
	ei			;0b44	fb		.
	ld bc,01000h		;0b45	01 00 10	. . .
	ld de,l00ffh		;0b48	11 ff 00	. . .
	ld hl,0df64h		;0b4b	21 64 df	! d .
l0b4eh:
	call sub_0b83h		;0b4e	cd 83 0b	. . .
	cp d			;0b51	ba		.
	jr c,l0b58h		;0b52	38 04		8 .
	ld d,a			;0b54	57		W
	ld (ix-035h),c		;0b55	dd 71 cb	. q .
l0b58h:
	cp e			;0b58	bb		.
	jr nc,l0b5fh		;0b59	30 04		0 .
	ld e,a			;0b5b	5f		_
	ld (ix-034h),c		;0b5c	dd 71 cc	. q .
l0b5fh:
	inc c			;0b5f	0c		.
	inc hl			;0b60	23		#
	djnz l0b4eh		;0b61	10 eb		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FdcTable0B63 - FDC parameter table (see ../fdc-driver.md).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'FdcTable0B63' (start 0x0b63 end 0x0b6b)
FdcTable0B63_start:
	defb 07ah		;0b63	7a		z
	defb 083h		;0b64	83		.
	defb 032h		;0b65	32		2
	defb 08ch		;0b66	8c		.
	defb 05fh		;0b67	5f		_
	defb 0c3h		;0b68	c3		.
	defb 091h		;0b69	91		.
	defb 009h		;0b6a	09		.
FdcTable0B63_end:
	push hl			;0b6b	e5		.
	ld hl,0df64h		;0b6c	21 64 df	! d .
	call sub_1106h		;0b6f	cd 06 11	. . .
	call sub_0b83h		;0b72	cd 83 0b	. . .
	add a,a			;0b75	87		.
	ld hl,05f8ch		;0b76	21 8c 5f	! . _
	cp (hl)			;0b79	be		.
	pop hl			;0b7a	e1		.
	ld a,(05f8bh)		;0b7b	3a 8b 5f	: . _
	ret nc			;0b7e	d0		.
	ld a,(05f8ah)		;0b7f	3a 8a 5f	: . _
	ret			;0b82	c9		.
sub_0b83h:
	push hl			;0b83	e5		.
	push bc			;0b84	c5		.
	ld c,(hl)		;0b85	4e		N
	ld hl,l0b97h		;0b86	21 97 0b	! . .
	ld b,006h		;0b89	06 06		. .
	xor a			;0b8b	af		.
l0b8ch:
	rr c			;0b8c	cb 19		. .
	jr c,l0b91h		;0b8e	38 01		8 .
	add a,(hl)		;0b90	86		.
l0b91h:
	inc hl			;0b91	23		#
	djnz l0b8ch		;0b92	10 f8		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FdcTable0B94 - FDC parameter table (see ../fdc-driver.md).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'FdcTable0B94' (start 0x0b94 end 0x0b9d)
FdcTable0B94_start:
	defb 0c1h		;0b94	c1		.
	defb 0e1h		;0b95	e1		.
	defb 0c9h		;0b96	c9		.
l0b97h:
	defb 002h		;0b97	02		.
	defb 006h		;0b98	06		.
	defb 00ch		;0b99	0c		.
	defb 001h		;0b9a	01		.
	defb 003h		;0b9b	03		.
	defb 006h		;0b9c	06		.
Rst20Handler:
	push bc			;0b9d	c5		.
	push de			;0b9e	d5		.
	push hl			;0b9f	e5		.
	push iy			;0ba0	fd e5		. .
	push af			;0ba2	f5		.
	ex af,af'		;0ba3	08		.
	ld a,001h		;0ba4	3e 01		> .
	rst 18h			;0ba6	df		.
	push af			;0ba7	f5		.
	call sub_05a0h		;0ba8	cd a0 05	. . .
	ei			;0bab	fb		.
	ex af,af'		;0bac	08		.
	call 0df43h		;0bad	cd 43 df	. C .
	call l0586h		;0bb0	cd 86 05	. . .
	pop af			;0bb3	f1		.
	rst 18h			;0bb4	df		.
	ei			;0bb5	fb		.
	pop af			;0bb6	f1		.
	pop iy			;0bb7	fd e1		. .
	pop hl			;0bb9	e1		.
	pop de			;0bba	d1		.
	pop bc			;0bbb	c1		.
	ret			;0bbc	c9		.
sub_0bbdh:
	xor a			;0bbd	af		.
	in a,(0feh)		;0bbe	db fe		. .
	or 0e0h			;0bc0	f6 e0		. .
	inc a			;0bc2	3c		<
	ret z			;0bc3	c8		.
	ld l,0f8h		;0bc4	2e f8		. .
	ld de,WbootStub		;0bc6	11 00 00	. . .
	ld bc,0fefeh		;0bc9	01 fe fe	. . .
l0bcch:
	in a,(c)		;0bcc	ed 78		. x
	cpl			;0bce	2f		/
	and 01fh		;0bcf	e6 1f		. .
	jr z,l0befh		;0bd1	28 1c		( .
	ld h,a			;0bd3	67		g
	ld a,l			;0bd4	7d		}
l0bd5h:
	add a,008h		;0bd5	c6 08		. .
	srl h			;0bd7	cb 3c		. <
	jr nc,l0bd5h		;0bd9	30 fa		0 .
	or a			;0bdb	b7		.
	jr nz,l0be2h		;0bdc	20 04		  .
	set 7,e			;0bde	cb fb		. .
	jr l0bebh		;0be0	18 09		. .
l0be2h:
	cp 00fh			;0be2	fe 0f		. .
	jr nz,l0beah		;0be4	20 04		  .
	set 6,e			;0be6	cb f3		. .
	jr l0bebh		;0be8	18 01		. .
l0beah:
	ld d,a			;0bea	57		W
l0bebh:
	inc h			;0beb	24		$
	dec h			;0bec	25		%
	jr nz,l0bd5h		;0bed	20 e6		  .
l0befh:
	inc l			;0bef	2c		,
	rlc b			;0bf0	cb 00		. .
	jr c,l0bcch		;0bf2	38 d8		8 .
	ld a,d			;0bf4	7a		z
	or e			;0bf5	b3		.
	ret			;0bf6	c9		.
l0bf7h:
	ld b,b			;0bf7	40		@
	add a,b			;0bf8	80		.
l0bf9h:
	inc bc			;0bf9	03		.
	inc b			;0bfa	04		.
	dec bc			;0bfb	0b		.
	inc c			;0bfc	0c		.
	inc de			;0bfd	13		.
	inc d			;0bfe	14		.
	dec de			;0bff	1b		.
l0c00h:
	inc e			;0c00	1c		.
	inc hl			;0c01	23		#
	inc h			;0c02	24		$
l0c03h:
	inc b			;0c03	04		.
	dec b			;0c04	05		.
	ex af,af'		;0c05	08		.
	inc c			;0c06	0c		.
	dec c			;0c07	0d		.
	ld c,010h		;0c08	0e 10		. .
	inc d			;0c0a	14		.
	ld d,017h		;0c0b	16 17		. .
	jr $+32			;0c0d	18 1e		. .
	rra			;0c0f	1f		.
	jr nz,l0c38h		;0c10	20 26		  &
	daa			;0c12	27		'
	dec (ix-058h)		;0c13	dd 35 a8	. 5 .
	ret p			;0c16	f0		.
	ld (ix-058h),000h	;0c17	dd 36 a8 00	. 6 . .
	call sub_0bbdh		;0c1b	cd bd 0b	. . .
	cp (ix-057h)		;0c1e	dd be a9	. . .
	jr z,l0c81h		;0c21	28 5e		( ^
	ld b,(ix-055h)		;0c23	dd 46 ab	. F .
	ld (ix-055h),0ffh	;0c26	dd 36 ab ff	. 6 . .
	inc b			;0c2a	04		.
	ld d,a			;0c2b	57		W
	ld c,03fh		;0c2c	0e 3f		. ?
	jr z,l0c46h		;0c2e	28 16		( .
	dec b			;0c30	05		.
	and c			;0c31	a1		.
	jr z,l0c42h		;0c32	28 0e		( .
	xor b			;0c34	a8		.
	and c			;0c35	a1		.
	jr z,l0c42h		;0c36	28 0a		( .
l0c38h:
	ld a,b			;0c38	78		x
	and c			;0c39	a1		.
	jr z,l0c42h		;0c3a	28 06		( .
	ld a,b			;0c3c	78		x
	and 0c0h		;0c3d	e6 c0		. .
	or c			;0c3f	b1		.
	jr l0c57h		;0c40	18 15		. .
l0c42h:
	ld a,b			;0c42	78		x
	or d			;0c43	b2		.
	jr l0c5bh		;0c44	18 15		. .
l0c46h:
	ld a,(05f40h)		;0c46	3a 40 5f	: @ _
	cpl			;0c49	2f		/
	or c			;0c4a	b1		.
	and d			;0c4b	a2		.
	ld hl,l0bf7h		;0c4c	21 f7 0b	! . .
	ld bc,l001ch		;0c4f	01 1c 00	. . .
	cpir			;0c52	ed b1		. .
	ld a,d			;0c54	7a		z
	jr nz,l0c5bh		;0c55	20 04		  .
l0c57h:
	ld (05f6ah),a		;0c57	32 6a 5f	2 j _
	ret			;0c5a	c9		.
l0c5bh:
	ld b,a			;0c5b	47		G
	ld a,(05f6bh)		;0c5c	3a 6b 5f	: k _
	ld (05f69h),a		;0c5f	32 69 5f	2 i _
	ld (ix-058h),005h	;0c62	dd 36 a8 05	. 6 . .
	ld a,b			;0c66	78		x
	and 03fh		;0c67	e6 3f		. ?
	jr z,l0c72h		;0c69	28 07		( .
	xor (ix-057h)		;0c6b	dd ae a9	. . .
	and 03fh		;0c6e	e6 3f		. ?
	jr nz,l0c7ch		;0c70	20 0a		  .
l0c72h:
	ld a,(05f40h)		;0c72	3a 40 5f	: @ _
	xor b			;0c75	a8		.
	and 03fh		;0c76	e6 3f		. ?
	xor b			;0c78	a8		.
	ld (05f40h),a		;0c79	32 40 5f	2 @ _
l0c7ch:
	ld (ix-057h),b		;0c7c	dd 70 a9	. p .
	jr l0c90h		;0c7f	18 0f		. .
l0c81h:
	bit 7,(ix-056h)		;0c81	dd cb aa 7e	. . . ~
	ret nz			;0c85	c0		.
	dec (ix-056h)		;0c86	dd 35 aa	. 5 .
	ret nz			;0c89	c0		.
	ld a,(05f6ch)		;0c8a	3a 6c 5f	: l _
	ld (05f69h),a		;0c8d	32 69 5f	2 i _
l0c90h:
	ld a,(05f40h)		;0c90	3a 40 5f	: @ _
	and 0c0h		;0c93	e6 c0		. .
	xor (ix-057h)		;0c95	dd ae a9	. . .
	jp p,l0ca4h		;0c98	f2 a4 0c	. . .
	ld hl,l0bf9h		;0c9b	21 f9 0b	! . .
	ld c,00ah		;0c9e	0e 0a		. .
	ld d,028h		;0ca0	16 28		. (
	jr l0cafh		;0ca2	18 0b		. .
l0ca4h:
	bit 6,a			;0ca4	cb 77		. w
	jr z,l0cb9h		;0ca6	28 11		( .
	ld d,032h		;0ca8	16 32		. 2
	ld hl,l0c03h		;0caa	21 03 0c	! . .
	ld c,010h		;0cad	0e 10		. .
l0cafh:
	ld b,000h		;0caf	06 00		. .
	and 03fh		;0cb1	e6 3f		. ?
	cpir			;0cb3	ed b1		. .
	jr nz,l0cb9h		;0cb5	20 02		  .
	ld a,d			;0cb7	7a		z
	add a,c			;0cb8	81		.
l0cb9h:
	or a			;0cb9	b7		.
	ret z			;0cba	c8		.
	ld b,a			;0cbb	47		G
	cp 02fh			;0cbc	fe 2f		. /
	jr nz,l0cceh		;0cbe	20 0e		  .
	ld (ix-056h),080h	;0cc0	dd 36 aa 80	. 6 . .
	ld a,(05f40h)		;0cc4	3a 40 5f	: @ _
	xor 010h		;0cc7	ee 10		. .
	ld (05f40h),a		;0cc9	32 40 5f	2 @ _
	xor a			;0ccc	af		.
	ret			;0ccd	c9		.
l0cceh:
	cp 02bh			;0cce	fe 2b		. +
	jr nz,l0cd8h		;0cd0	20 06		  .
	ld (ix-056h),080h	;0cd2	dd 36 aa 80	. 6 . .
	jr l0cf0h		;0cd6	18 18		. .
l0cd8h:
	cp 007h			;0cd8	fe 07		. .
	jr nz,l0cf0h		;0cda	20 14		  .
	bit 3,(ix-07eh)		;0cdc	dd cb 82 5e	. . . ^
	jr nz,l0cf0h		;0ce0	20 0e		  .
	ld a,(05f40h)		;0ce2	3a 40 5f	: @ _
	or 03eh			;0ce5	f6 3e		. >
	inc a			;0ce7	3c		<
	jr nz,l0cf0h		;0ce8	20 06		  .
	ld a,003h		;0cea	3e 03		> .
	rst 18h			;0cec	df		.
	jp 0f864h		;0ced	c3 64 f8	. d .
l0cf0h:
	ld a,(05f6dh)		;0cf0	3a 6d 5f	: m _
	cp 010h			;0cf3	fe 10		. .
	ld hl,(05f6eh)		;0cf5	2a 6e 5f	* n _
	jp z,l078eh		;0cf8	ca 8e 07	. . .
	ld hl,07fc0h		;0cfb	21 c0 7f	! . .
	call sub_1106h		;0cfe	cd 06 11	. . .
	inc (ix-052h)		;0d01	dd 34 ae	. 4 .
	ld (hl),b		;0d04	70		p
	ld hl,(05f70h)		;0d05	2a 70 5f	* p _
	jp l078eh		;0d08	c3 8e 07	. . .
	bit 2,(ix-07fh)		;0d0b	dd cb 81 56	. . . V
	jr z,l0d30h		;0d0f	28 1f		( .
	call sub_0d50h		;0d11	cd 50 0d	. P .
	jr nz,l0d36h		;0d14	20 20		   
	ld a,001h		;0d16	3e 01		> .
	rst 18h			;0d18	df		.
	push af			;0d19	f5		.
	call l0586h		;0d1a	cd 86 05	. . .
	pop af			;0d1d	f1		.
	rst 18h			;0d1e	df		.
	ei			;0d1f	fb		.
	call l0d30h		;0d20	cd 30 0d	. 0 .
	push af			;0d23	f5		.
	ld a,001h		;0d24	3e 01		> .
	rst 18h			;0d26	df		.
	push af			;0d27	f5		.
	call sub_05a0h		;0d28	cd a0 05	. . .
	pop af			;0d2b	f1		.
	rst 18h			;0d2c	df		.
	ei			;0d2d	fb		.
	pop af			;0d2e	f1		.
	ret			;0d2f	c9		.
l0d30h:
	call 05f78h		;0d30	cd 78 5f	. x _
	or a			;0d33	b7		.
	jr z,l0d30h		;0d34	28 fa		( .
l0d36h:
	call sub_0d3dh		;0d36	cd 3d 0d	. = .
	call sub_0d84h		;0d39	cd 84 0d	. . .
	ret			;0d3c	c9		.
sub_0d3dh:
	ld hl,07fc1h		;0d3d	21 c1 7f	! . .
	ld de,07fc0h		;0d40	11 c0 7f	. . .
	dec (ix-052h)		;0d43	dd 35 ae	. 5 .
	ld a,(de)		;0d46	1a		.
	ld bc,l000fh		;0d47	01 0f 00	. . .
	ldir			;0d4a	ed b0		. .
	ret			;0d4c	c9		.
l0d4dh:
	call sub_0d3dh		;0d4d	cd 3d 0d	. = .
sub_0d50h:
	ld a,(05f6dh)		;0d50	3a 6d 5f	: m _
	or a			;0d53	b7		.
	ret z			;0d54	c8		.
	ld a,(07fc0h)		;0d55	3a c0 7f	: . .
	call sub_0d84h		;0d58	cd 84 0d	. . .
	or a			;0d5b	b7		.
	jr z,l0d4dh		;0d5c	28 ef		( .
	ld a,0ffh		;0d5e	3e ff		> .
	ret			;0d60	c9		.
	ld a,(05f6dh)		;0d61	3a 6d 5f	: m _
	or a			;0d64	b7		.
	ld a,001h		;0d65	3e 01		> .
	jr z,l0d73h		;0d67	28 0a		( .
	call sub_0d3dh		;0d69	cd 3d 0d	. = .
	push af			;0d6c	f5		.
	call sub_0d84h		;0d6d	cd 84 0d	. . .
	ld c,a			;0d70	4f		O
	pop hl			;0d71	e1		.
	xor a			;0d72	af		.
l0d73h:
	ld l,000h		;0d73	2e 00		. .
	ld d,(ix-07fh)		;0d75	dd 56 81	. V .
	ret			;0d78	c9		.
	ld a,(05f6dh)		;0d79	3a 6d 5f	: m _
	or a			;0d7c	b7		.
	ld a,001h		;0d7d	3e 01		> .
	jr z,l0d73h		;0d7f	28 f2		( .
	xor a			;0d81	af		.
	jr l0d73h		;0d82	18 ef		. .
sub_0d84h:
	ld e,a			;0d84	5f		_
	ld hl,MenuKbdRows_start	;0d85	21 32 0e	! 2 .
	call sub_1106h		;0d88	cd 06 11	. . .
	ld a,(hl)		;0d8b	7e		~
	or a			;0d8c	b7		.
	scf			;0d8d	37		7
	ret z			;0d8e	c8		.
	cp 020h			;0d8f	fe 20		.  
	ret z			;0d91	c8		.
	cp 00dh			;0d92	fe 0d		. .
	jr nz,l0da2h		;0d94	20 0c		  .
	ld a,(05f40h)		;0d96	3a 40 5f	: @ _
	or 03eh			;0d99	f6 3e		. >
	inc a			;0d9b	3c		<
	ld a,00dh		;0d9c	3e 0d		> .
	ret nz			;0d9e	c0		.
	ld a,00ah		;0d9f	3e 0a		> .
	ret			;0da1	c9		.
l0da2h:
	ld d,000h		;0da2	16 00		. .
	ex af,af'		;0da4	08		.
	ld a,e			;0da5	7b		{
	bit 5,(ix-07fh)		;0da6	dd cb 81 6e	. . . n
	jr z,l0dbch		;0daa	28 10		( .
	cp 03ah			;0dac	fe 3a		. :
	jr c,l0dbch		;0dae	38 0c		8 .
	sub 03ah		;0db0	d6 3a		. :
	ld hl,l0e74h		;0db2	21 74 0e	! t .
	call sub_1106h		;0db5	cd 06 11	. . .
	ld a,(hl)		;0db8	7e		~
	ex af,af'		;0db9	08		.
	jr l0dcah		;0dba	18 0e		. .
l0dbch:
	cp 028h			;0dbc	fe 28		. (
	jr nc,l0dc9h		;0dbe	30 09		0 .
	dec a			;0dc0	3d		=
	and 006h		;0dc1	e6 06		. .
	cp 002h			;0dc3	fe 02		. .
	jr nz,l0dc8h		;0dc5	20 01		  .
	inc d			;0dc7	14		.
l0dc8h:
	inc d			;0dc8	14		.
l0dc9h:
	inc d			;0dc9	14		.
l0dcah:
	ld a,(05f40h)		;0dca	3a 40 5f	: @ _
	and 0c0h		;0dcd	e6 c0		. .
	rra			;0dcf	1f		.
	rra			;0dd0	1f		.
	rra			;0dd1	1f		.
	rra			;0dd2	1f		.
	add a,d			;0dd3	82		.
	ld hl,l0de1h		;0dd4	21 e1 0d	! . .
	call sub_1106h		;0dd7	cd 06 11	. . .
	ld a,(hl)		;0dda	7e		~
	call sub_1106h		;0ddb	cd 06 11	. . .
	ex af,af'		;0dde	08		.
	or a			;0ddf	b7		.
	jp (hl)			;0de0	e9		.
l0de1h:
	dec d			;0de1	15		.
	jr c,$+21		;0de2	38 13		8 .
	rrca			;0de4	0f		.
	inc c			;0de5	0c		.
	dec bc			;0de6	0b		.
	ld a,019h		;0de7	3e 19		> .
	dec bc			;0de9	0b		.
	ld h,009h		;0dea	26 09		& .
	dec d			;0dec	15		.
	inc b			;0ded	04		.
	inc bc			;0dee	03		.
	rrca			;0def	0f		.
	ld bc,037afh		;0df0	01 af 37	. . 7
	ret			;0df3	c9		.
	xor 020h		;0df4	ee 20		.  
	bit 4,(ix-07fh)		;0df6	dd cb 81 66	. . . f
	ret z			;0dfa	c8		.
	xor 020h		;0dfb	ee 20		.  
	ret			;0dfd	c9		.
	and 01fh		;0dfe	e6 1f		. .
	ret			;0e00	c9		.
	sub 010h		;0e01	d6 10		. .
	cp 022h			;0e03	fe 22		. "
	jr z,l0e0dh		;0e05	28 06		( .
	cp 020h			;0e07	fe 20		.  
	ret nz			;0e09	c0		.
	ld a,05fh		;0e0a	3e 5f		> _
	ret			;0e0c	c9		.
l0e0dh:
	ld a,040h		;0e0d	3e 40		> @
	ret			;0e0f	c9		.
	sub 028h		;0e10	d6 28		. (
	cp 002h			;0e12	fe 02		. .
	jr nc,$-35		;0e14	30 db		0 .
	add a,a			;0e16	87		.
	add a,05bh		;0e17	c6 5b		. [
	ret			;0e19	c9		.
	or a			;0e1a	b7		.
	ret p			;0e1b	f0		.
	ld a,00eh		;0e1c	3e 0e		> .
	bit 5,(ix-07fh)		;0e1e	dd cb 81 6e	. . . n
	ret nz			;0e22	c0		.
	inc a			;0e23	3c		<
	ret			;0e24	c9		.
	sub 041h		;0e25	d6 41		. A
	ld hl,l0e7ch		;0e27	21 7c 0e	! | .
	call sub_1106h		;0e2a	cd 06 11	. . .
	ld a,(hl)		;0e2d	7e		~
	or a			;0e2e	b7		.
	ret nz			;0e2f	c0		.
	scf			;0e30	37		7
	ret			;0e31	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MenuKbdRows - menu keyboard half-rows "AQ10P", "ZSW29OL",
; "XDE38IKM", "CFRT47UJ", "VGT56YHB" (ZX matrix layout).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MenuKbdRows' (start 0x0e32 end 0x0e5a)
MenuKbdRows_start:
	defb 000h		;0e32	00		.
	defb 041h		;0e33	41		A
	defb 051h		;0e34	51		Q
	defb 031h		;0e35	31		1
	defb 030h		;0e36	30		0
	defb 050h		;0e37	50		P
	defb 00dh		;0e38	0d		.
	defb 020h		;0e39	20		 
	defb 05ah		;0e3a	5a		Z
	defb 053h		;0e3b	53		S
	defb 057h		;0e3c	57		W
	defb 032h		;0e3d	32		2
	defb 039h		;0e3e	39		9
	defb 04fh		;0e3f	4f		O
	defb 04ch		;0e40	4c		L
	defb 000h		;0e41	00		.
	defb 058h		;0e42	58		X
	defb 044h		;0e43	44		D
	defb 045h		;0e44	45		E
	defb 033h		;0e45	33		3
	defb 038h		;0e46	38		8
	defb 049h		;0e47	49		I
	defb 04bh		;0e48	4b		K
	defb 04dh		;0e49	4d		M
	defb 043h		;0e4a	43		C
	defb 046h		;0e4b	46		F
	defb 052h		;0e4c	52		R
	defb 034h		;0e4d	34		4
	defb 037h		;0e4e	37		7
	defb 055h		;0e4f	55		U
	defb 04ah		;0e50	4a		J
	defb 04eh		;0e51	4e		N
	defb 056h		;0e52	56		V
	defb 047h		;0e53	47		G
	defb 054h		;0e54	54		T
	defb 035h		;0e55	35		5
	defb 036h		;0e56	36		6
	defb 059h		;0e57	59		Y
	defb 048h		;0e58	48		H
	defb 042h		;0e59	42		B
MenuKbdRows_end:
	jr l0e6fh		;0e5a	18 13		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KbdSymMap0E5C - shifted-symbol key map, tailing into a small
; JP island (JP $0C13/$0D0B/$0D50/$0D61/$0D79, RET).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KbdSymMap0E5C' (start 0x0e5c end 0x0ea6)
KbdSymMap0E5C_start:
	defb 005h		;0e5c	05		.
	defb 080h		;0e5d	80		.
	defb 004h		;0e5e	04		.
	defb 009h		;0e5f	09		.
	defb 000h		;0e60	00		.
	defb 000h		;0e61	00		.
	defb 008h		;0e62	08		.
	defb 01bh		;0e63	1b		.
	defb 02ah		;0e64	2a		*
	defb 05eh		;0e65	5e		^
	defb 02fh		;0e66	2f		/
	defb 02ch		;0e67	2c		,
	defb 02dh		;0e68	2d		-
	defb 03fh		;0e69	3f		?
	defb 02eh		;0e6a	2e		.
	defb 02bh		;0e6b	2b		+
	defb 028h		;0e6c	28		(
	defb 060h		;0e6d	60		`
	defb 03dh		;0e6e	3d		=
l0e6fh:
	defb 03bh		;0e6f	3b		;
	defb 029h		;0e70	29		)
	defb 03ah		;0e71	3a		:
	defb 022h		;0e72	22		"
	defb 05fh		;0e73	5f		_
l0e74h:
	defb 05bh		;0e74	5b		[
	defb 05fh		;0e75	5f		_
	defb 03dh		;0e76	3d		=
	defb 03bh		;0e77	3b		;
	defb 05dh		;0e78	5d		]
	defb 05eh		;0e79	5e		^
	defb 040h		;0e7a	40		@
	defb 05ch		;0e7b	5c		\
l0e7ch:
	defb 07eh		;0e7c	7e		~
	defb 02ah		;0e7d	2a		*
	defb 03fh		;0e7e	3f		?
	defb 05ch		;0e7f	5c		\
	defb 000h		;0e80	00		.
	defb 07bh		;0e81	7b		{
	defb 07dh		;0e82	7d		}
	defb 05eh		;0e83	5e		^
	defb 000h		;0e84	00		.
	defb 02dh		;0e85	2d		-
	defb 02bh		;0e86	2b		+
	defb 03dh		;0e87	3d		=
	defb 02eh		;0e88	2e		.
	defb 02ch		;0e89	2c		,
	defb 03bh		;0e8a	3b		;
	defb 022h		;0e8b	22		"
	defb 000h		;0e8c	00		.
	defb 03ch		;0e8d	3c		<
	defb 07ch		;0e8e	7c		|
	defb 03eh		;0e8f	3e		>
	defb 05dh		;0e90	5d		]
	defb 02fh		;0e91	2f		/
	defb 000h		;0e92	00		.
	defb 060h		;0e93	60		`
	defb 05bh		;0e94	5b		[
	defb 03ah		;0e95	3a		:
l0e96h:
	defb 0c3h		;0e96	c3		.
	defb 013h		;0e97	13		.
	defb 00ch		;0e98	0c		.
	defb 0c3h		;0e99	c3		.
	defb 00bh		;0e9a	0b		.
	defb 00dh		;0e9b	0d		.
	defb 0c3h		;0e9c	c3		.
	defb 050h		;0e9d	50		P
	defb 00dh		;0e9e	0d		.
	defb 0c3h		;0e9f	c3		.
	defb 061h		;0ea0	61		a
	defb 00dh		;0ea1	0d		.
	defb 0c3h		;0ea2	c3		.
	defb 079h		;0ea3	79		y
	defb 00dh		;0ea4	0d		.
	defb 0c9h		;0ea5	c9		.
KbdSymMap0E5C_end:
	sub 002h		;0ea6	d6 02		. .
	jr nz,l0ef1h		;0ea8	20 47		  G
	ld (05f4bh),a		;0eaa	32 4b 5f	2 K _
	ld (ix-073h),05fh	;0ead	dd 36 8d 5f	. 6 . _
	ld hl,041c0h		;0eb1	21 c0 41	! . A
	ld (05f4dh),hl		;0eb4	22 4d 5f	" M _
	ret			;0eb7	c9		.
sub_0eb8h:
	ex af,af'		;0eb8	08		.
	xor a			;0eb9	af		.
	ex af,af'		;0eba	08		.
	ld c,a			;0ebb	4f		O
	ld hl,(05f4dh)		;0ebc	2a 4d 5f	* M _
	bit 0,(ix-07eh)		;0ebf	dd cb 82 46	. . . F
	jr z,$+16		;0ec3	28 0e		( .
	cp 020h			;0ec5	fe 20		.  
	jr z,l0ed4h		;0ec7	28 0b		( .
	ld a,020h		;0ec9	3e 20		>  
	cp (hl)			;0ecb	be		.
	jr z,$+7		;0ecc	28 05		( .
	ex af,af'		;0ece	08		.
	ld a,001h		;0ecf	3e 01		> .
	ex af,af'		;0ed1	08		.
	ld a,071h		;0ed2	3e 71		> q
l0ed4h:
	bit 1,(ix-07eh)		;0ed4	dd cb 82 4e	. . . N
	jr nz,l0ee1h		;0ed8	20 07		  .
	call sub_0ee3h		;0eda	cd e3 0e	. . .
	ld a,(05f4fh)		;0edd	3a 4f 5f	: O _
	ld (hl),a		;0ee0	77		w
l0ee1h:
	ex af,af'		;0ee1	08		.
	ret			;0ee2	c9		.
sub_0ee3h:
	ld a,h			;0ee3	7c		|
	or 0c0h			;0ee4	f6 c0		. .
	jr l0ef4h		;0ee6	18 0c		. .
l0ee8h:
	ld hl,(05f4dh)		;0ee8	2a 4d 5f	* M _
	call sub_0ef3h		;0eeb	cd f3 0e	. . .
	ld (05f4dh),hl		;0eee	22 4d 5f	" M _
l0ef1h:
	xor a			;0ef1	af		.
	ret			;0ef2	c9		.
sub_0ef3h:
	ld a,h			;0ef3	7c		|
l0ef4h:
	xor 020h		;0ef4	ee 20		.  
	ld h,a			;0ef6	67		g
	and 020h		;0ef7	e6 20		.  
	ret nz			;0ef9	c0		.
	inc hl			;0efa	23		#
	ret			;0efb	c9		.
sub_0efch:
	call sub_0f04h		;0efc	cd 04 0f	. . .
	ld (05f4dh),hl		;0eff	22 4d 5f	" M _
	xor a			;0f02	af		.
	ret			;0f03	c9		.
sub_0f04h:
	xor a			;0f04	af		.
	ld h,a			;0f05	67		g
	rrc c			;0f06	cb 09		. .
	rra			;0f08	1f		.
	rra			;0f09	1f		.
	rra			;0f0a	1f		.
	or 041h			;0f0b	f6 41		. A
	ld l,b			;0f0d	68		h
	ld b,a			;0f0e	47		G
	set 7,c			;0f0f	cb f9		. .
	set 6,c			;0f11	cb f1		. .
	add hl,hl		;0f13	29		)
	add hl,hl		;0f14	29		)
	add hl,hl		;0f15	29		)
	add hl,hl		;0f16	29		)
	add hl,hl		;0f17	29		)
	add hl,hl		;0f18	29		)
	add hl,bc		;0f19	09		.
	ret			;0f1a	c9		.
sub_0f1bh:
	or a			;0f1b	b7		.
	ex af,af'		;0f1c	08		.
	ld hl,(05f4dh)		;0f1d	2a 4d 5f	* M _
	bit 0,(ix-074h)		;0f20	dd cb 8c 46	. . . F
	call nz,sub_0ee3h	;0f24	c4 e3 0e	. . .
	ex af,af'		;0f27	08		.
	ld a,(05f50h)		;0f28	3a 50 5f	: P _
	call nz,sub_0f31h	;0f2b	c4 31 0f	. 1 .
	ld (hl),a		;0f2e	77		w
	xor a			;0f2f	af		.
	ret			;0f30	c9		.
sub_0f31h:
	ld a,(hl)		;0f31	7e		~
	ld (05f50h),a		;0f32	32 50 5f	2 P _
	ld a,(05f4ch)		;0f35	3a 4c 5f	: L _
	bit 0,(ix-074h)		;0f38	dd cb 8c 46	. . . F
	ret z			;0f3c	c8		.
	xor (hl)		;0f3d	ae		.
	ret			;0f3e	c9		.
l0f3fh:
	ld (05f4fh),a		;0f3f	32 4f 5f	2 O _
	xor a			;0f42	af		.
	ret			;0f43	c9		.
l0f44h:
	set 0,(ix-074h)		;0f44	dd cb 8c c6	. . . .
	ld a,007h		;0f48	3e 07		> .
	cp c			;0f4a	b9		.
	jr nz,l0f58h		;0f4b	20 0b		  .
	cp b			;0f4d	b8		.
	ld a,05fh		;0f4e	3e 5f		> _
	jr z,l0f65h		;0f50	28 13		( .
	inc b			;0f52	04		.
	dec b			;0f53	05		.
	ld a,03fh		;0f54	3e 3f		> ?
	jr z,l0f69h		;0f56	28 11		( .
l0f58h:
	bit 2,b			;0f58	cb 50		. P
	ld a,0ach		;0f5a	3e ac		> .
	jr nz,l0f65h		;0f5c	20 07		  .
	ld a,0afh		;0f5e	3e af		> .
	bit 2,c			;0f60	cb 51		. Q
	jr z,l0f65h		;0f62	28 01		( .
	dec a			;0f64	3d		=
l0f65h:
	res 0,(ix-074h)		;0f65	dd cb 8c 86	. . . .
l0f69h:
	ld (05f4ch),a		;0f69	32 4c 5f	2 L _
	xor a			;0f6c	af		.
	ret			;0f6d	c9		.
	ld a,0ffh		;0f6e	3e ff		> .
	ret			;0f70	c9		.
	ex af,af'		;0f71	08		.
	ld a,b			;0f72	78		x
	call sub_11efh		;0f73	cd ef 11	. . .
	inc b			;0f76	04		.
	ld l,(hl)		;0f77	6e		n
	rrca			;0f78	0f		.
	ld a,a			;0f79	7f		.
	rrca			;0f7a	0f		.
	ld h,l			;0f7b	65		e
	rrca			;0f7c	0f		.
	add a,h			;0f7d	84		.
	rrca			;0f7e	0f		.
	ld hl,l00ffh+1		;0f7f	21 00 01	! . .
	xor a			;0f82	af		.
	ret			;0f83	c9		.
	set 0,(ix-074h)		;0f84	dd cb 8c c6	. . . .
	jr l0f69h		;0f88	18 df		. .
sub_0f8ah:
	ld c,e			;0f8a	4b		K
	srl c			;0f8b	cb 39		. 9
	ld b,c			;0f8d	41		A
	ret nc			;0f8e	d0		.
	inc c			;0f8f	0c		.
	ret			;0f90	c9		.
sub_0f91h:
	ex af,af'		;0f91	08		.
	ld a,083h		;0f92	3e 83		> .
	scf			;0f94	37		7
	inc d			;0f95	14		.
	dec d			;0f96	15		.
	ret z			;0f97	c8		.
	inc e			;0f98	1c		.
	dec e			;0f99	1d		.
	ret z			;0f9a	c8		.
	call sub_0f04h		;0f9b	cd 04 0f	. . .
	call sub_0f8ah		;0f9e	cd 8a 0f	. . .
	ex af,af'		;0fa1	08		.
	or a			;0fa2	b7		.
	ret			;0fa3	c9		.
sub_0fa4h:
	ld a,0c3h		;0fa4	3e c3		> .
	ld (0df40h),a		;0fa6	32 40 df	2 @ .
	exx			;0fa9	d9		.
l0faah:
	ld a,c			;0faa	79		y
	exx			;0fab	d9		.
	or a			;0fac	b7		.
	call nz,sub_0fcbh	;0fad	c4 cb 0f	. . .
	bit 1,(ix-07eh)		;0fb0	dd cb 82 4e	. . . N
	jr nz,l0fc3h		;0fb4	20 0d		  .
	push hl			;0fb6	e5		.
	ex af,af'		;0fb7	08		.
	call sub_0ee3h		;0fb8	cd e3 0e	. . .
	ld a,(05f4fh)		;0fbb	3a 4f 5f	: O _
	call sub_0fcbh		;0fbe	cd cb 0f	. . .
	ex af,af'		;0fc1	08		.
	pop hl			;0fc2	e1		.
l0fc3h:
	xor a			;0fc3	af		.
	sbc hl,de		;0fc4	ed 52		. R
	exx			;0fc6	d9		.
	djnz l0faah		;0fc7	10 e1		. .
	exx			;0fc9	d9		.
	ret			;0fca	c9		.
sub_0fcbh:
	push de			;0fcb	d5		.
	push hl			;0fcc	e5		.
	call 0df40h		;0fcd	cd 40 df	. @ .
	pop hl			;0fd0	e1		.
	push hl			;0fd1	e5		.
	ex af,af'		;0fd2	08		.
	call sub_0ef3h		;0fd3	cd f3 0e	. . .
	ex af,af'		;0fd6	08		.
	call 0df40h		;0fd7	cd 40 df	. @ .
	pop hl			;0fda	e1		.
	pop de			;0fdb	d1		.
	ret			;0fdc	c9		.
l0fddh:
	call sub_0f91h		;0fdd	cd 91 0f	. . .
	ret c			;0fe0	d8		.
	ld e,a			;0fe1	5f		_
	push de			;0fe2	d5		.
	exx			;0fe3	d9		.
	pop bc			;0fe4	c1		.
l0fe5h:
	exx			;0fe5	d9		.
	ld de,l0ff4h		;0fe6	11 f4 0f	. . .
	ld (0df41h),de		;0fe9	ed 53 41 df	. S A .
	ld de,0ffc0h		;0fed	11 c0 ff	. . .
	call sub_0fa4h		;0ff0	cd a4 0f	. . .
	ret			;0ff3	c9		.
l0ff4h:
	push bc			;0ff4	c5		.
	dec c			;0ff5	0d		.
	jp m,l1003h		;0ff6	fa 03 10	. . .
	ld (hl),a		;0ff9	77		w
	jr z,l1003h		;0ffa	28 07		( .
	ld d,h			;0ffc	54		T
	ld e,l			;0ffd	5d		]
	inc de			;0ffe	13		.
	ld b,000h		;0fff	06 00		. .
	ldir			;1001	ed b0		. .
l1003h:
	pop bc			;1003	c1		.
	ld e,b			;1004	58		X
	ld b,c			;1005	41		A
	ld c,e			;1006	4b		K
	ret			;1007	c9		.
l1008h:
	call sub_0f91h		;1008	cd 91 0f	. . .
	ret c			;100b	d8		.
	cp 002h			;100c	fe 02		. .
	jr nc,l103fh		;100e	30 2f		0 /
	rra			;1010	1f		.
	dec d			;1011	15		.
	jr z,l103ah		;1012	28 26		( &
	ld a,d			;1014	7a		z
	exx			;1015	d9		.
	ld b,a			;1016	47		G
	ld c,001h		;1017	0e 01		. .
	exx			;1019	d9		.
	ld de,l108eh		;101a	11 8e 10	. . .
	ld (0df41h),de		;101d	ed 53 41 df	. S A .
	ld de,0ffc0h		;1021	11 c0 ff	. . .
	jr nc,l1034h		;1024	30 0e		0 .
	ex de,hl		;1026	eb		.
	ld l,a			;1027	6f		o
	ld h,000h		;1028	26 00		& .
	add hl,hl		;102a	29		)
	add hl,hl		;102b	29		)
	add hl,hl		;102c	29		)
	add hl,hl		;102d	29		)
	add hl,hl		;102e	29		)
	add hl,hl		;102f	29		)
	add hl,de		;1030	19		.
	ld de,l0040h		;1031	11 40 00	. @ .
l1034h:
	push bc			;1034	c5		.
	call sub_0fa4h		;1035	cd a4 0f	. . .
	pop bc			;1038	c1		.
	exx			;1039	d9		.
l103ah:
	ld bc,l0120h		;103a	01 20 01	.   .
	jr l0fe5h		;103d	18 a6		. .
l103fh:
	ld a,b			;103f	78		x
	ld bc,sub_0ef3h		;1040	01 f3 0e	. . .
	jr z,l1050h		;1043	28 0b		( .
	call sub_1106h		;1045	cd 06 11	. . .
	ld bc,l1085h		;1048	01 85 10	. . .
	bit 0,e			;104b	cb 43		. C
	call z,l1085h		;104d	cc 85 10	. . .
l1050h:
	ld (0df41h),bc		;1050	ed 43 41 df	. C A .
	ld a,0c3h		;1054	3e c3		> .
	ld (0df40h),a		;1056	32 40 df	2 @ .
l1059h:
	ld a,020h		;1059	3e 20		>  
	push hl			;105b	e5		.
	call sub_107ah		;105c	cd 7a 10	. z .
	pop hl			;105f	e1		.
	bit 1,(ix-07eh)		;1060	dd cb 82 4e	. . . N
	jr nz,l1071h		;1064	20 0b		  .
	push hl			;1066	e5		.
	call sub_0ee3h		;1067	cd e3 0e	. . .
	ld a,(05f4fh)		;106a	3a 4f 5f	: O _
	call sub_107ah		;106d	cd 7a 10	. z .
	pop hl			;1070	e1		.
l1071h:
	ld bc,l0040h		;1071	01 40 00	. @ .
	add hl,bc		;1074	09		.
	dec d			;1075	15		.
	jr nz,l1059h		;1076	20 e1		  .
	xor a			;1078	af		.
	ret			;1079	c9		.
sub_107ah:
	ld b,e			;107a	43		C
	ld c,a			;107b	4f		O
l107ch:
	ld a,(hl)		;107c	7e		~
	ld (hl),c		;107d	71		q
	ld c,a			;107e	4f		O
	call 0df40h		;107f	cd 40 df	. @ .
	djnz l107ch		;1082	10 f8		. .
	ret			;1084	c9		.
l1085h:
	ld a,h			;1085	7c		|
	xor 020h		;1086	ee 20		.  
	ld h,a			;1088	67		g
	and 020h		;1089	e6 20		.  
	ret z			;108b	c8		.
	dec hl			;108c	2b		+
	ret			;108d	c9		.
l108eh:
	push de			;108e	d5		.
	push bc			;108f	c5		.
	push hl			;1090	e5		.
	push hl			;1091	e5		.
	or a			;1092	b7		.
	sbc hl,de		;1093	ed 52		. R
	pop de			;1095	d1		.
	ld b,000h		;1096	06 00		. .
	inc c			;1098	0c		.
	dec c			;1099	0d		.
	jr z,l109eh		;109a	28 02		( .
	ldir			;109c	ed b0		. .
l109eh:
	pop hl			;109e	e1		.
	pop bc			;109f	c1		.
	ld e,b			;10a0	58		X
	ld b,c			;10a1	41		A
	ld c,e			;10a2	4b		K
	pop de			;10a3	d1		.
	ret			;10a4	c9		.
sub_10a5h:
	add a,020h		;10a5	c6 20		.  
	ex (sp),hl		;10a7	e3		.
	push bc			;10a8	c5		.
	ld c,(hl)		;10a9	4e		N
	inc hl			;10aa	23		#
	srl c			;10ab	cb 39		. 9
	jr nc,l10b9h		;10ad	30 0a		0 .
	cp 061h			;10af	fe 61		. a
	jr c,l10b9h		;10b1	38 06		8 .
	cp 07bh			;10b3	fe 7b		. {
	jr nc,l10b9h		;10b5	30 02		0 .
	sub 020h		;10b7	d6 20		.  
l10b9h:
	push bc			;10b9	c5		.
	srl c			;10ba	cb 39		. 9
	srl c			;10bc	cb 39		. 9
	jr z,l10cah		;10be	28 0a		( .
	ld b,000h		;10c0	06 00		. .
	cpir			;10c2	ed b1		. .
	add hl,bc		;10c4	09		.
	jr nz,l10cah		;10c5	20 03		  .
	ld a,c			;10c7	79		y
	or 080h			;10c8	f6 80		. .
l10cah:
	pop bc			;10ca	c1		.
	ld b,030h		;10cb	06 30		. 0
	rr c			;10cd	cb 19		. .
	rr c			;10cf	cb 19		. .
	jp p,l10dch		;10d1	f2 dc 10	. . .
	ld c,00ah		;10d4	0e 0a		. .
	jr nc,l10e4h		;10d6	30 0c		0 .
	ld c,(hl)		;10d8	4e		N
	inc hl			;10d9	23		#
	jr l10e4h		;10da	18 08		. .
l10dch:
	ld c,010h		;10dc	0e 10		. .
	jr c,l10e4h		;10de	38 04		8 .
	ld c,0ffh		;10e0	0e ff		. .
	ld b,000h		;10e2	06 00		. .
l10e4h:
	or a			;10e4	b7		.
	jp m,l10f9h		;10e5	fa f9 10	. . .
	sub b			;10e8	90		.
	jr c,l1101h		;10e9	38 16		8 .
	cp 00ah			;10eb	fe 0a		. .
	jr c,l10f5h		;10ed	38 06		8 .
	sub 007h		;10ef	d6 07		. .
	cp 00ah			;10f1	fe 0a		. .
	jr c,l1101h		;10f3	38 0c		8 .
l10f5h:
	cp c			;10f5	b9		.
	ccf			;10f6	3f		?
	jr l1101h		;10f7	18 08		. .
l10f9h:
	ld c,(hl)		;10f9	4e		N
	inc c			;10fa	0c		.
	dec c			;10fb	0d		.
	push af			;10fc	f5		.
	call nz,sub_1104h	;10fd	c4 04 11	. . .
	pop af			;1100	f1		.
l1101h:
	pop bc			;1101	c1		.
	ex (sp),hl		;1102	e3		.
	ret			;1103	c9		.
sub_1104h:
	inc a			;1104	3c		<
sub_1105h:
	add a,a			;1105	87		.
sub_1106h:
	add a,l			;1106	85		.
	ld l,a			;1107	6f		o
	ret nc			;1108	d0		.
	inc h			;1109	24		$
	ret			;110a	c9		.
Rst08Handler:
	ex (sp),hl		;110b	e3		.
	ld a,(hl)		;110c	7e		~
	inc hl			;110d	23		#
	inc a			;110e	3c		<
	jr z,l1115h		;110f	28 04		( .
	dec a			;1111	3d		=
	ld (05f91h),a		;1112	32 91 5f	2 . _
l1115h:
	ld a,(hl)		;1115	7e		~
	inc hl			;1116	23		#
	inc a			;1117	3c		<
	jr z,l111eh		;1118	28 04		( .
	dec a			;111a	3d		=
	ld (05f90h),a		;111b	32 90 5f	2 . _
l111eh:
	ex (sp),hl		;111e	e3		.
	push hl			;111f	e5		.
	push de			;1120	d5		.
	push bc			;1121	c5		.
	res 6,h			;1122	cb b4		. .
	res 7,h			;1124	cb bc		. .
	res 6,d			;1126	cb b2		. .
	res 7,d			;1128	cb ba		. .
	or a			;112a	b7		.
	sbc hl,de		;112b	ed 52		. R
	add hl,de		;112d	19		.
	jr c,l1131h		;112e	38 01		8 .
	ex de,hl		;1130	eb		.
l1131h:
	ld hl,04000h		;1131	21 00 40	! . @
	or a			;1134	b7		.
	sbc hl,de		;1135	ed 52		. R
	ld (05f56h),hl		;1137	22 56 5f	" V _
	sbc hl,bc		;113a	ed 42		. B
	pop bc			;113c	c1		.
	pop de			;113d	d1		.
	jr nc,l1151h		;113e	30 11		0 .
	ld bc,(05f56h)		;1140	ed 4b 56 5f	. K V _
	ex (sp),hl		;1144	e3		.
	call sub_1152h		;1145	cd 52 11	. R .
	pop bc			;1148	c1		.
	push hl			;1149	e5		.
	xor a			;114a	af		.
	ld h,a			;114b	67		g
	ld l,a			;114c	6f		o
	sbc hl,bc		;114d	ed 42		. B
	ld b,h			;114f	44		D
	ld c,l			;1150	4d		M
l1151h:
	pop hl			;1151	e1		.
sub_1152h:
	push hl			;1152	e5		.
	push de			;1153	d5		.
	push bc			;1154	c5		.
	call sub_1162h		;1155	cd 62 11	. b .
	pop bc			;1158	c1		.
	pop hl			;1159	e1		.
	add hl,bc		;115a	09		.
	ex de,hl		;115b	eb		.
	pop hl			;115c	e1		.
	add hl,bc		;115d	09		.
	ld bc,WbootStub		;115e	01 00 00	. . .
	ret			;1161	c9		.
sub_1162h:
	ld a,(05f91h)		;1162	3a 91 5f	: . _
	ex de,hl		;1165	eb		.
	call sub_11d3h		;1166	cd d3 11	. . .
	ld (05f56h),a		;1169	32 56 5f	2 V _
	ex de,hl		;116c	eb		.
	ld a,(05f90h)		;116d	3a 90 5f	: . _
	call sub_11d3h		;1170	cd d3 11	. . .
	ld (05f57h),a		;1173	32 57 5f	2 W _
	ld a,h			;1176	7c		|
	and d			;1177	a2		.
	or 03fh			;1178	f6 3f		. ?
	inc a			;117a	3c		<
	di			;117b	f3		.
	ld a,(05f51h)		;117c	3a 51 5f	: Q _
	push af			;117f	f5		.
	jr nz,l118ah		;1180	20 08		  .
	ld a,(05f57h)		;1182	3a 57 5f	: W _
	cp (ix-069h)		;1185	dd be 97	. . .
	jr nz,l11a0h		;1188	20 16		  .
l118ah:
	ld a,h			;118a	7c		|
	or 03fh			;118b	f6 3f		. ?
	inc a			;118d	3c		<
	ld a,(05f57h)		;118e	3a 57 5f	: W _
	jr z,l1196h		;1191	28 03		( .
	ld a,(05f56h)		;1193	3a 56 5f	: V _
l1196h:
	push bc			;1196	c5		.
	rst 18h			;1197	df		.
	pop bc			;1198	c1		.
	ei			;1199	fb		.
	ldir			;119a	ed b0		. .
l119ch:
	pop af			;119c	f1		.
	rst 18h			;119d	df		.
	ei			;119e	fb		.
	ret			;119f	c9		.
l11a0h:
	rlc c			;11a0	cb 01		. .
	srl c			;11a2	cb 39		. 9
	ld a,b			;11a4	78		x
	rla			;11a5	17		.
	jr nz,l11abh		;11a6	20 03		  .
	ld c,080h		;11a8	0e 80		. .
	dec a			;11aa	3d		=
l11abh:
	inc a			;11ab	3c		<
	ld b,000h		;11ac	06 00		. .
l11aeh:
	ex af,af'		;11ae	08		.
	exx			;11af	d9		.
	ld a,(05f57h)		;11b0	3a 57 5f	: W _
	rst 18h			;11b3	df		.
	ei			;11b4	fb		.
	exx			;11b5	d9		.
	ld a,c			;11b6	79		y
	push de			;11b7	d5		.
	ld de,07f40h		;11b8	11 40 7f	. @ .
	ldir			;11bb	ed b0		. .
	pop de			;11bd	d1		.
	ld c,a			;11be	4f		O
	exx			;11bf	d9		.
	ld a,(05f56h)		;11c0	3a 56 5f	: V _
	rst 18h			;11c3	df		.
	ei			;11c4	fb		.
	exx			;11c5	d9		.
	push hl			;11c6	e5		.
	ld hl,07f40h		;11c7	21 40 7f	! @ .
	ldir			;11ca	ed b0		. .
	pop hl			;11cc	e1		.
	ex af,af'		;11cd	08		.
	dec a			;11ce	3d		=
	jr nz,l11aeh		;11cf	20 dd		  .
	jr l119ch		;11d1	18 c9		. .
sub_11d3h:
	bit 7,a			;11d3	cb 7f		. .
	ret z			;11d5	c8		.
	bit 7,h			;11d6	cb 7c		. |
	ld a,003h		;11d8	3e 03		> .
	ret nz			;11da	c0		.
	inc a			;11db	3c		<
	bit 6,h			;11dc	cb 74		. t
	set 6,h			;11de	cb f4		. .
	set 7,h			;11e0	cb fc		. .
	ret nz			;11e2	c0		.
	xor a			;11e3	af		.
	ret			;11e4	c9		.
	call PageZeroReserved_end	;11e5	cd 4b 00	. K .
	ld (05f90h),a		;11e8	32 90 5f	2 . _
	ld (05f91h),a		;11eb	32 91 5f	2 . _
	ret			;11ee	c9		.
sub_11efh:
	ex (sp),hl		;11ef	e3		.
	cp (hl)			;11f0	be		.
	inc hl			;11f1	23		#
	jr nc,l11f8h		;11f2	30 04		0 .
	inc a			;11f4	3c		<
	call sub_1105h		;11f5	cd 05 11	. . .
l11f8h:
	ld a,(hl)		;11f8	7e		~
	inc hl			;11f9	23		#
	ld h,(hl)		;11fa	66		f
	ld l,a			;11fb	6f		o
	ex (sp),hl		;11fc	e3		.
	ex af,af'		;11fd	08		.
	ret			;11fe	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MonitorPrintNumber - decimal print used by the RAM-size report
; (CALL $11FF right before the "k." suffix at MsgKib).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MonitorPrintNumber:
	ld a,(hl)		;11ff	7e		~
	or a			;1200	b7		.
	inc hl			;1201	23		#
	ret z			;1202	c8		.
	rst 20h			;1203	e7		.
	jr MonitorPrintNumber	;1204	18 f9		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MonitorPrint - string print used by the RAM-size report
; (prints "Memory " at MsgMemory and "size is " at MsgSizeIs).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MonitorPrint:
	ex (sp),hl		;1206	e3		.
	push af			;1207	f5		.
	call MonitorPrintNumber	;1208	cd ff 11	. . .
	pop af			;120b	f1		.
	ex (sp),hl		;120c	e3		.
	ret			;120d	c9		.
sub_120eh:
	push af			;120e	f5		.
	call sub_1217h		;120f	cd 17 12	. . .
	ld a,020h		;1212	3e 20		>  
	rst 20h			;1214	e7		.
	pop af			;1215	f1		.
	ret			;1216	c9		.
sub_1217h:
	push af			;1217	f5		.
	rrca			;1218	0f		.
	rrca			;1219	0f		.
	rrca			;121a	0f		.
	rrca			;121b	0f		.
	call sub_1224h		;121c	cd 24 12	. $ .
	pop af			;121f	f1		.
	call sub_1224h		;1220	cd 24 12	. $ .
	ret			;1223	c9		.
sub_1224h:
	and 00fh		;1224	e6 0f		. .
	add a,090h		;1226	c6 90		. .
	daa			;1228	27		'
	adc a,040h		;1229	ce 40		. @
	daa			;122b	27		'
	rst 20h			;122c	e7		.
	ret			;122d	c9		.
sub_122eh:
	ld (ix-019h),c		;122e	dd 71 e7	. q .
	ld hl,05fa7h		;1231	21 a7 5f	! . _
	inc de			;1234	13		.
	ld bc,Rst08Vector	;1235	01 08 00	. . .
	ex de,hl		;1238	eb		.
	rst 8			;1239	cf		.
	inc bc			;123a	03		.
	cp 0fbh			;123b	fe fb		. .
	push iy			;123d	fd e5		. .
	call sub_125ch		;123f	cd 5c 12	. \ .
	pop iy			;1242	fd e1		. .
	ret			;1244	c9		.
sub_1245h:
	ei			;1245	fb		.
	ld (05fa7h),a		;1246	32 a7 5f	2 . _
	ld (ix-019h),c		;1249	dd 71 e7	. q .
	push hl			;124c	e5		.
	push de			;124d	d5		.
	push iy			;124e	fd e5		. .
	ld (05fadh),de		;1250	ed 53 ad 5f	. S . _
	call sub_125ch		;1254	cd 5c 12	. \ .
	pop iy			;1257	fd e1		. .
	pop de			;1259	d1		.
	pop hl			;125a	e1		.
	ret			;125b	c9		.
sub_125ch:
	ld a,(05fa6h)		;125c	3a a6 5f	: . _
	ld hl,0f951h		;125f	21 51 f9	! Q .
	call sub_143eh		;1262	cd 3e 14	. > .
	push hl			;1265	e5		.
	pop iy			;1266	fd e1		. .
	ld a,(05fa7h)		;1268	3a a7 5f	: . _
	cp 007h			;126b	fe 07		. .
	jp z,l140dh		;126d	ca 0d 14	. . .
	ld a,(iy+000h)		;1270	fd 7e 00	. ~ .
	cp 0ffh			;1273	fe ff		. .
	ld a,009h		;1275	3e 09		> .
	ret nz			;1277	c0		.
	ld a,(05fa7h)		;1278	3a a7 5f	: . _
	cp 008h			;127b	fe 08		. .
	jp z,l142dh		;127d	ca 2d 14	. - .
	cp 000h			;1280	fe 00		. .
	jr z,l12b5h		;1282	28 31		( 1
	cp 003h			;1284	fe 03		. .
	jr z,l12b5h		;1286	28 2d		( -
	dec (ix-014h)		;1288	dd 35 ec	. 5 .
	ld de,(05fa9h)		;128b	ed 5b a9 5f	. [ . _
	ld l,(iy+012h)		;128f	fd 6e 12	. n .
	ld h,(iy+013h)		;1292	fd 66 13	. f .
	dec hl			;1295	2b		+
	xor a			;1296	af		.
	sbc hl,de		;1297	ed 52		. R
	ld a,008h		;1299	3e 08		> .
	ret c			;129b	d8		.
	ld a,(iy+004h)		;129c	fd 7e 04	. ~ .
	add a,(iy+005h)		;129f	fd 86 05	. . .
	ld c,a			;12a2	4f		O
	ld b,000h		;12a3	06 00		. .
	call sub_12d7h		;12a5	cd d7 12	. . .
	ld (ix-010h),l		;12a8	dd 75 f0	. u .
	ld l,(iy+00ch)		;12ab	fd 6e 0c	. n .
	ld h,(iy+00dh)		;12ae	fd 66 0d	. f .
	add hl,de		;12b1	19		.
	ld (05fb0h),hl		;12b2	22 b0 5f	" . _
l12b5h:
	ld a,005h		;12b5	3e 05		> .
	ld (05fb7h),a		;12b7	32 b7 5f	2 . _
l12bah:
	ld a,(05fa7h)		;12ba	3a a7 5f	: . _
	push af			;12bd	f5		.
	call sub_12f1h		;12be	cd f1 12	. . .
	ld e,a			;12c1	5f		_
	pop af			;12c2	f1		.
	ld (05fa7h),a		;12c3	32 a7 5f	2 . _
	ld a,e			;12c6	7b		{
	or a			;12c7	b7		.
	ret z			;12c8	c8		.
	ld (ix-005h),0ffh	;12c9	dd 36 fb ff	. 6 . .
	and 080h		;12cd	e6 80		. .
	ld a,e			;12cf	7b		{
	ret z			;12d0	c8		.
	dec (ix-008h)		;12d1	dd 35 f8	. 5 .
	jr nz,l12bah		;12d4	20 e4		  .
	ret			;12d6	c9		.
sub_12d7h:
	ld hl,WbootStub		;12d7	21 00 00	! . .
	ld a,010h		;12da	3e 10		> .
l12dch:
	ex af,af'		;12dc	08		.
	add hl,hl		;12dd	29		)
	xor a			;12de	af		.
	sla e			;12df	cb 23		. #
	rl d			;12e1	cb 12		. .
	adc a,l			;12e3	8d		.
	ld l,a			;12e4	6f		o
	sbc hl,bc		;12e5	ed 42		. B
	inc de			;12e7	13		.
	jr nc,l12ech		;12e8	30 02		0 .
	add hl,bc		;12ea	09		.
	dec de			;12eb	1b		.
l12ech:
	ex af,af'		;12ec	08		.
	dec a			;12ed	3d		=
	jr nz,l12dch		;12ee	20 ec		  .
	ret			;12f0	c9		.
sub_12f1h:
	ld a,(05fa7h)		;12f1	3a a7 5f	: . _
	ld h,a			;12f4	67		g
	cp 007h			;12f5	fe 07		. .
	ld a,052h		;12f7	3e 52		> R
	ret nc			;12f9	d0		.
	ld a,h			;12fa	7c		|
	ld hl,l1302h		;12fb	21 02 13	! . .
	call sub_143eh		;12fe	cd 3e 14	. > .
	jp (hl)			;1301	e9		.
l1302h:
	cpl			;1302	2f		/
	inc de			;1303	13		.
	djnz $+21		;1304	10 13		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ConfigTable1306 - cold-shell configuration table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'ConfigTable1306' (start 0x1306 end 0x130e)
ConfigTable1306_start:
	defb 010h		;1306	10		.
	defb 013h		;1307	13		.
	defb 010h		;1308	10		.
	defb 013h		;1309	13		.
	defb 035h		;130a	35		5
	defb 013h		;130b	13		.
	defb 077h		;130c	77		w
	defb 013h		;130d	13		.
ConfigTable1306_end:
	ld d,b			;130e	50		P
	inc de			;130f	13		.
l1310h:
	push iy			;1310	fd e5		. .
	pop bc			;1312	c1		.
	ld a,(iy+001h)		;1313	fd 7e 01	. ~ .
	cp 008h			;1316	fe 08		. .
	jr c,l131dh		;1318	38 03		8 .
	ld a,050h		;131a	3e 50		> P
	ret			;131c	c9		.
l131dh:
	ld hl,0f965h		;131d	21 65 f9	! e .
	call sub_143eh		;1320	cd 3e 14	. > .
	ld a,l			;1323	7d		}
	or h			;1324	b4		.
	ld a,051h		;1325	3e 51		> Q
	ret z			;1327	c8		.
	ld a,(05fa7h)		;1328	3a a7 5f	: . _
	call sub_143eh		;132b	cd 3e 14	. > .
	jp (hl)			;132e	e9		.
	ld (ix-005h),0ffh	;132f	dd 36 fb ff	. 6 . .
	jr l1310h		;1333	18 db		. .
l1335h:
	call sub_137bh		;1335	cd 7b 13	. { .
	jr z,l134ah		;1338	28 10		( .
	ld (ix-018h),001h	;133a	dd 36 e8 01	. 6 . .
	call l1310h		;133e	cd 10 13	. . .
	ret nz			;1341	c0		.
	ld (ix-018h),004h	;1342	dd 36 e8 04	. 6 . .
	call l1310h		;1346	cd 10 13	. . .
	ret nz			;1349	c0		.
l134ah:
	call sub_13dbh		;134a	cd db 13	. . .
	jr nz,l1335h		;134d	20 e6		  .
	ret			;134f	c9		.
	ld (ix-018h),001h	;1350	dd 36 e8 01	. 6 . .
	call l1310h		;1354	cd 10 13	. . .
	ret nz			;1357	c0		.
l1358h:
	call sub_137bh		;1358	cd 7b 13	. { .
	jr z,l1365h		;135b	28 08		( .
	ld (ix-018h),004h	;135d	dd 36 e8 04	. 6 . .
	call l1310h		;1361	cd 10 13	. . .
	ret nz			;1364	c0		.
l1365h:
	call sub_13fch		;1365	cd fc 13	. . .
	ld (ix-018h),006h	;1368	dd 36 e8 06	. 6 . .
	call l1310h		;136c	cd 10 13	. . .
	pop hl			;136f	e1		.
	ret nz			;1370	c0		.
	call sub_13edh		;1371	cd ed 13	. . .
	jr nz,l1358h		;1374	20 e2		  .
	ret			;1376	c9		.
	ld a,052h		;1377	3e 52		> R
	or a			;1379	b7		.
	ret			;137a	c9		.
sub_137bh:
	push bc			;137b	c5		.
	ld b,(iy+01dh)		;137c	fd 46 1d	. F .
	ld c,0ffh		;137f	0e ff		. .
	ld a,(05fabh)		;1381	3a ab 5f	: . _
	inc b			;1384	04		.
	jr l138bh		;1385	18 04		. .
l1387h:
	srl a			;1387	cb 3f		. ?
	sla c			;1389	cb 21		. !
l138bh:
	djnz l1387h		;138b	10 fa		. .
	ld (05fb4h),a		;138d	32 b4 5f	2 . _
	ld a,(05fabh)		;1390	3a ab 5f	: . _
	or c			;1393	b1		.
	xor c			;1394	a9		.
	ld c,000h		;1395	0e 00		. .
	srl a			;1397	cb 3f		. ?
	rr c			;1399	cb 19		. .
	ld b,a			;139b	47		G
	ld hl,0fb5ah		;139c	21 5a fb	! Z .
	ld (05fb5h),hl		;139f	22 b5 5f	" . _
	add hl,bc		;13a2	09		.
	ld (05fb2h),hl		;13a3	22 b2 5f	" . _
	ld a,(05fa6h)		;13a6	3a a6 5f	: . _
	cp (ix-005h)		;13a9	dd be fb	. . .
	jr nz,l13c4h		;13ac	20 16		  .
	ld hl,(05fa9h)		;13ae	2a a9 5f	* . _
	ld bc,(05fbbh)		;13b1	ed 4b bb 5f	. K . _
	sbc hl,bc		;13b5	ed 42		. B
	jr nz,l13c4h		;13b7	20 0b		  .
	ld a,(05fb4h)		;13b9	3a b4 5f	: . _
	cp (ix-002h)		;13bc	dd be fe	. . .
	jr nz,l13c4h		;13bf	20 03		  .
	xor a			;13c1	af		.
	jr l13d9h		;13c2	18 15		. .
l13c4h:
	ld a,(05fa6h)		;13c4	3a a6 5f	: . _
	ld (05fbah),a		;13c7	32 ba 5f	2 . _
	ld hl,(05fa9h)		;13ca	2a a9 5f	* . _
	ld (05fbbh),hl		;13cd	22 bb 5f	" . _
	ld a,(05fb4h)		;13d0	3a b4 5f	: . _
	ld (05fbdh),a		;13d3	32 bd 5f	2 . _
	ld a,0ffh		;13d6	3e ff		> .
	and a			;13d8	a7		.
l13d9h:
	pop bc			;13d9	c1		.
	ret			;13da	c9		.
sub_13dbh:
	push af			;13db	f5		.
	push de			;13dc	d5		.
	ld bc,l0080h		;13dd	01 80 00	. . .
	ld de,(05fadh)		;13e0	ed 5b ad 5f	. [ . _
	ld hl,(05fb2h)		;13e4	2a b2 5f	* . _
	rst 8			;13e7	cf		.
	cp 003h			;13e8	fe 03		. .
	ex de,hl		;13ea	eb		.
	pop de			;13eb	d1		.
	pop af			;13ec	f1		.
sub_13edh:
	ld (05fadh),hl		;13ed	22 ad 5f	" . _
	ld l,005h		;13f0	2e 05		. .
	ld (ix-008h),l		;13f2	dd 75 f8	. u .
	inc (ix-014h)		;13f5	dd 34 ec	. 4 .
	dec (ix-017h)		;13f8	dd 35 e9	. 5 .
	ret			;13fb	c9		.
sub_13fch:
	push de			;13fc	d5		.
	ld bc,l0080h		;13fd	01 80 00	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ColdShellEntry - cold-boot configuration shell.
; LD DE,(5FB2) / LD HL,(5FAD) / RST 08 ... JP (HL): menu-driven
; memory/disk configuration.  Runs from the $D400 RAM copy at cold
; start (the +$C000 view of this range); after configuration WBOOT
; replaces the copy with the CCP+BDOS image.  The RAM-size options it
; presents live in RamSizeTable ($3815): 128 / 512 / 1024 KiB - the
; 512/1024 modes the prince.trd investigation booted (../analysis.md).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ColdShellEntry:
	ld de,(05fb2h)		;1400	ed 5b b2 5f	. [ . _
	ld hl,(05fadh)		;1404	2a ad 5f	* . _
	rst 8			;1407	cf		.
	inc bc			;1408	03		.
	cp 0d1h			;1409	fe d1		. .
	ex (sp),hl		;140b	e3		.
	jp (hl)			;140c	e9		.
l140dh:
	ld (iy+000h),0ffh	;140d	fd 36 00 ff	. 6 . .
	push iy			;1411	fd e5		. .
	pop de			;1413	d1		.
	inc de			;1414	13		.
	ld hl,(05fadh)		;1415	2a ad 5f	* . _
	ld bc,Rst20Vector+2	;1418	01 22 00	. " .
	rst 8			;141b	cf		.
	inc bc			;141c	03		.
	cp 0fbh			;141d	fe fb		. .
	ld (ix-018h),000h	;141f	dd 36 e8 00	. 6 . .
	call sub_125ch		;1423	cd 5c 12	. \ .
	ld (ix-018h),003h	;1426	dd 36 e8 03	. 6 . .
	jp sub_125ch		;142a	c3 5c 12	. \ .
l142dh:
	push iy			;142d	fd e5		. .
	pop hl			;142f	e1		.
	inc hl			;1430	23		#
	ld de,(05fadh)		;1431	ed 5b ad 5f	. [ . _
	ld bc,Rst20Vector+2	;1435	01 22 00	. " .
	rst 8			;1438	cf		.
	cp 003h			;1439	fe 03		. .
	ei			;143b	fb		.
	xor a			;143c	af		.
	ret			;143d	c9		.
sub_143eh:
	call sub_1105h		;143e	cd 05 11	. . .
	ld e,(hl)		;1441	5e		^
	inc hl			;1442	23		#
	ld d,(hl)		;1443	56		V
	ex de,hl		;1444	eb		.
	ret			;1445	c9		.
	adc a,(hl)		;1446	8e		.
	inc d			;1447	14		.
	pop de			;1448	d1		.
	inc d			;1449	14		.
	ld h,d			;144a	62		b
	rla			;144b	17		.
	rla			;144c	17		.
	ld d,0a1h		;144d	16 a1		. .
	dec d			;144f	15		.
	adc a,h			;1450	8c		.
	inc d			;1451	14		.
	jp (hl)			;1452	e9		.
	dec d			;1453	15		.
sub_1454h:
	ld hl,05fb8h		;1454	21 b8 5f	! . _
	ld a,(iy+002h)		;1457	fd 7e 02	. ~ .
	jp sub_1106h		;145a	c3 06 11	. . .
sub_145dh:
	ld (05f8eh),sp		;145d	ed 73 8e 5f	. s . _
sub_1461h:
	ld c,(iy+002h)		;1461	fd 4e 02	. N .
	xor a			;1464	af		.
	sub (ix-010h)		;1465	dd 96 f0	. . .
	cpl			;1468	2f		/
	and 010h		;1469	e6 10		. .
	or c			;146b	b1		.
	ld c,(iy+01ch)		;146c	fd 4e 1c	. N .
	xor c			;146f	a9		.
	and 0bfh		;1470	e6 bf		. .
	xor c			;1472	a9		.
	or 00ch			;1473	f6 0c		. .
	out (0ffh),a		;1475	d3 ff		. .
	call sub_1635h		;1477	cd 35 16	. 5 .
	call sub_1454h		;147a	cd 54 14	. T .
	ld a,(hl)		;147d	7e		~
	inc a			;147e	3c		<
	jr nz,l1489h		;147f	20 08		  .
	call sub_14b0h		;1481	cd b0 14	. . .
	jp nz,l154bh		;1484	c2 4b 15	. K .
	ld a,001h		;1487	3e 01		> .
l1489h:
	dec a			;1489	3d		=
	out (03fh),a		;148a	d3 3f		. ?
	xor a			;148c	af		.
	ret			;148d	c9		.
	di			;148e	f3		.
	ld a,008h		;148f	3e 08		> .
	out (0ffh),a		;1491	d3 ff		. .
	ld b,000h		;1493	06 00		. .
l1495h:
	djnz l1495h		;1495	10 fe		. .
	ld a,00ch		;1497	3e 0c		> .
	out (0ffh),a		;1499	d3 ff		. .
	ld a,0d0h		;149b	3e d0		> .
	out (01fh),a		;149d	d3 1f		. .
	call sub_14a5h		;149f	cd a5 14	. . .
l14a2h:
	xor a			;14a2	af		.
	ei			;14a3	fb		.
	ret			;14a4	c9		.
sub_14a5h:
	push af			;14a5	f5		.
	call sub_1454h		;14a6	cd 54 14	. T .
	ld (hl),0ffh		;14a9	36 ff		6 .
	pop af			;14ab	f1		.
	ret			;14ac	c9		.
sub_14adh:
	ld a,(05fb0h)		;14ad	3a b0 5f	: . _
sub_14b0h:
	ld c,(iy+020h)		;14b0	fd 4e 20	. N  
	set 3,c			;14b3	cb d9		. .
	or a			;14b5	b7		.
	ld (hl),a		;14b6	77		w
	jr z,l14c8h		;14b7	28 0f		( .
	out (07fh),a		;14b9	d3 7f		. .
	set 4,c			;14bb	cb e1		. .
	ld a,(05fa7h)		;14bd	3a a7 5f	: . _
	cp 002h			;14c0	fe 02		. .
	jr z,l14cbh		;14c2	28 07		( .
	set 2,c			;14c4	cb d1		. .
	jr l14cbh		;14c6	18 03		. .
l14c8h:
	dec a			;14c8	3d		=
	out (03fh),a		;14c9	d3 3f		. ?
l14cbh:
	ld a,c			;14cb	79		y
sub_14cch:
	out (01fh),a		;14cc	d3 1f		. .
	jp l1523h		;14ce	c3 23 15	. # .
	call sub_145dh		;14d1	cd 5d 14	. ] .
	ld a,(05fb0h)		;14d4	3a b0 5f	: . _
	cp (hl)			;14d7	be		.
	jr z,l14a2h		;14d8	28 c8		( .
sub_14dah:
	ex af,af'		;14da	08		.
	call sub_14adh		;14db	cd ad 14	. . .
	jr z,l14a2h		;14de	28 c2		( .
	ex af,af'		;14e0	08		.
	or a			;14e1	b7		.
	jr z,l154bh		;14e2	28 67		( g
	ld a,0c0h		;14e4	3e c0		> .
	ld hl,07f40h		;14e6	21 40 7f	! @ .
	ld bc,0087fh		;14e9	01 7f 08	. . .
	ld de,WbootStub		;14ec	11 00 00	. . .
	di			;14ef	f3		.
	out (01fh),a		;14f0	d3 1f		. .
	call sub_1583h		;14f2	cd 83 15	. . .
	ei			;14f5	fb		.
	in a,(01fh)		;14f6	db 1f		. .
	and 01ch		;14f8	e6 1c		. .
	jr nz,l1506h		;14fa	20 0a		  .
	ld a,(07f40h)		;14fc	3a 40 7f	: @ .
	out (03fh),a		;14ff	d3 3f		. ?
	call sub_14adh		;1501	cd ad 14	. . .
	jr z,l14a2h		;1504	28 9c		( .
l1506h:
	xor a			;1506	af		.
	call sub_14b0h		;1507	cd b0 14	. . .
	call sub_14adh		;150a	cd ad 14	. . .
	jr nz,l154bh		;150d	20 3c		  <
	jr l14a2h		;150f	18 91		. .
sub_1511h:
	ld a,(05f5dh)		;1511	3a 5d 5f	: ] _
	ld b,004h		;1514	06 04		. .
	and 008h		;1516	e6 08		. .
	ret z			;1518	c8		.
	sla b			;1519	cb 20		.  
	ret			;151b	c9		.
l151ch:
	pop de			;151c	d1		.
	pop bc			;151d	c1		.
	in a,(01fh)		;151e	db 1f		. .
	bit 4,a			;1520	cb 67		. g
	ret			;1522	c9		.
l1523h:
	push bc			;1523	c5		.
	push de			;1524	d5		.
	call sub_1511h		;1525	cd 11 15	. . .
	ld de,WbootStub		;1528	11 00 00	. . .
l152bh:
	in a,(0ffh)		;152b	db ff		. .
	and 080h		;152d	e6 80		. .
	jr nz,l151ch		;152f	20 eb		  .
	dec de			;1531	1b		.
	ld a,d			;1532	7a		z
	or e			;1533	b3		.
	jr nz,l152bh		;1534	20 f5		  .
	djnz l152bh		;1536	10 f3		. .
l1538h:
	ld a,059h		;1538	3e 59		> Y
	ld hl,0403eh		;153a	21 3e 40	! > @
	ld hl,(05f8eh)		;153d	2a 8e 5f	* . _
	inc hl			;1540	23		#
	inc hl			;1541	23		#
	ld sp,hl		;1542	f9		.
	ld h,a			;1543	67		g
	ld a,0d0h		;1544	3e d0		> .
	out (01fh),a		;1546	d3 1f		. .
	ld a,h			;1548	7c		|
	or a			;1549	b7		.
	ret			;154a	c9		.
l154bh:
	call sub_14a5h		;154b	cd a5 14	. . .
	jr $-19			;154e	18 eb		. .
sub_1550h:
	ld (05f8eh),sp		;1550	ed 73 8e 5f	. s . _
	call sub_1461h		;1554	cd 61 14	. a .
	in a,(01fh)		;1557	db 1f		. .
	and 020h		;1559	e6 20		.  
	jr nz,l156ah		;155b	20 0d		  .
	ld a,(hl)		;155d	7e		~
	out (07fh),a		;155e	d3 7f		. .
	ld a,(iy+020h)		;1560	fd 7e 20	. ~  
	or 018h			;1563	f6 18		. .
	call sub_14cch		;1565	cd cc 14	. . .
	jr nz,l154bh		;1568	20 e1		  .
l156ah:
	ld a,(05fb4h)		;156a	3a b4 5f	: . _
	inc a			;156d	3c		<
	out (05fh),a		;156e	d3 5f		. _
	call sub_1511h		;1570	cd 11 15	. . .
	xor a			;1573	af		.
	sub (ix-010h)		;1574	dd 96 f0	. . .
	and 008h		;1577	e6 08		. .
	ld hl,(05fb5h)		;1579	2a b5 5f	* . _
	ld c,07fh		;157c	0e 7f		. .
	ld de,WbootStub		;157e	11 00 00	. . .
	exx			;1581	d9		.
	ret			;1582	c9		.
sub_1583h:
	in a,(0ffh)		;1583	db ff		. .
	and 0c0h		;1585	e6 c0		. .
	jp m,l15dfh		;1587	fa df 15	. . .
	jr nz,l159dh		;158a	20 11		  .
	dec de			;158c	1b		.
	ld a,d			;158d	7a		z
	or e			;158e	b3		.
	jr nz,sub_1583h		;158f	20 f2		  .
	djnz sub_1583h		;1591	10 f0		. .
	jp l1538h		;1593	c3 38 15	. 8 .
l1596h:
	in a,(0ffh)		;1596	db ff		. .
	and 0c0h		;1598	e6 c0		. .
	jr z,l1596h		;159a	28 fa		( .
	ret m			;159c	f8		.
l159dh:
	ini			;159d	ed a2		. .
	jr l1596h		;159f	18 f5		. .
	call sub_1550h		;15a1	cd 50 15	. P .
	ld c,01ch		;15a4	0e 1c		. .
	exx			;15a6	d9		.
	or 080h			;15a7	f6 80		. .
	di			;15a9	f3		.
	out (01fh),a		;15aa	d3 1f		. .
	call sub_1583h		;15ac	cd 83 15	. . .
l15afh:
	in a,(01fh)		;15af	db 1f		. .
	exx			;15b1	d9		.
	and c			;15b2	a1		.
l15b3h:
	ei			;15b3	fb		.
	ret z			;15b4	c8		.
	ld c,a			;15b5	4f		O
l15b6h:
	bit 4,c			;15b6	cb 61		. a
	jr nz,l15d2h		;15b8	20 18		  .
	bit 3,c			;15ba	cb 59		. Y
	ld a,084h		;15bc	3e 84		> .
	ret nz			;15be	c0		.
	bit 2,c			;15bf	cb 51		. Q
	ld a,083h		;15c1	3e 83		> .
	ret nz			;15c3	c0		.
	bit 5,c			;15c4	cb 69		. i
	ld a,053h		;15c6	3e 53		> S
	ret nz			;15c8	c0		.
	bit 6,c			;15c9	cb 71		. q
	ld a,054h		;15cb	3e 54		> T
	ret nz			;15cd	c0		.
	ld a,056h		;15ce	3e 56		> V
	or a			;15d0	b7		.
	ret			;15d1	c9		.
l15d2h:
	call sub_1454h		;15d2	cd 54 14	. T .
	ld a,(05fb0h)		;15d5	3a b0 5f	: . _
	call sub_14dah		;15d8	cd da 14	. . .
	ld a,081h		;15db	3e 81		> .
	or a			;15dd	b7		.
	ret			;15de	c9		.
l15dfh:
	in a,(01fh)		;15df	db 1f		. .
	exx			;15e1	d9		.
	and c			;15e2	a1		.
	jp z,0153bh		;15e3	ca 3b 15	. ; .
	pop bc			;15e6	c1		.
	jr l15b3h		;15e7	18 ca		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ColdShellTab15E9 - unreachable cold-shell fragment
; (CALL $1550, EXX, port $1F writes; orphaned CALL byte at end).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'ColdShellTab15E9' (start 0x15e9 end 0x15f5)
ColdShellTab15E9_start:
	defb 0cdh		;15e9	cd		.
	defb 050h		;15ea	50		P
	defb 015h		;15eb	15		.
	defb 00eh		;15ec	0e		.
	defb 07ch		;15ed	7c		|
	defb 0d9h		;15ee	d9		.
	defb 0f6h		;15ef	f6		.
	defb 0a0h		;15f0	a0		.
	defb 0f3h		;15f1	f3		.
	defb 0d3h		;15f2	d3		.
	defb 01fh		;15f3	1f		.
	defb 0cdh		;15f4	cd		.
ColdShellTab15E9_end:
	ld sp,hl		;15f5	f9		.
	dec d			;15f6	15		.
	jr l15afh		;15f7	18 b6		. .
l15f9h:
	in a,(0ffh)		;15f9	db ff		. .
	and 0c0h		;15fb	e6 c0		. .
	jp m,l15dfh		;15fd	fa df 15	. . .
l1600h:
	jr nz,l1613h		;1600	20 11		  .
	dec de			;1602	1b		.
	ld a,d			;1603	7a		z
	or e			;1604	b3		.
	jr nz,l15f9h		;1605	20 f2		  .
	djnz l15f9h		;1607	10 f0		. .
	jp l1538h		;1609	c3 38 15	. 8 .
l160ch:
	in a,(0ffh)		;160c	db ff		. .
	and 0c0h		;160e	e6 c0		. .
	jr z,l160ch		;1610	28 fa		( .
	ret m			;1612	f8		.
l1613h:
	outi			;1613	ed a3		. .
	jr l160ch		;1615	18 f5		. .
l1617h:
	call sub_145dh		;1617	cd 5d 14	. ] .
	xor a			;161a	af		.
	call sub_14b0h		;161b	cd b0 14	. . .
	jp nz,0153bh		;161e	c2 3b 15	. ; .
	xor a			;1621	af		.
	ret			;1622	c9		.
sub_1623h:
	out (07fh),a		;1623	d3 7f		. .
	ld b,a			;1625	47		G
	ld a,c			;1626	79		y
	or 018h			;1627	f6 18		. .
	out (01fh),a		;1629	d3 1f		. .
	call l1523h		;162b	cd 23 15	. # .
	ld e,a			;162e	5f		_
	scf			;162f	37		7
	ret nz			;1630	c0		.
	in a,(03fh)		;1631	db 3f		. ?
	cp b			;1633	b8		.
	ret			;1634	c9		.
sub_1635h:
	bit 7,(iy+006h)		;1635	fd cb 06 7e	. . . ~
	call nz,sub_1644h	;1639	c4 44 16	. D .
	bit 7,(iy+020h)		;163c	fd cb 20 7e	. .   ~
	call nz,sub_1677h	;1640	c4 77 16	. w .
	ret			;1643	c9		.
sub_1644h:
	ld a,00fh		;1644	3e 0f		> .
	out (01fh),a		;1646	d3 1f		. .
	call l1523h		;1648	cd 23 15	. # .
	jp nz,0153bh		;164b	c2 3b 15	. ; .
l164eh:
	ld d,050h		;164e	16 50		. P
	ld c,013h		;1650	0e 13		. .
	ld a,04fh		;1652	3e 4f		> O
	call sub_1623h		;1654	cd 23 16	. # .
	jp c,l154bh		;1657	da 4b 15	. K .
	jr nz,l1666h		;165a	20 0a		  .
	ld a,001h		;165c	3e 01		> .
	call sub_1623h		;165e	cd 23 16	. # .
	jp c,l154bh		;1661	da 4b 15	. K .
	jr z,l1668h		;1664	28 02		( .
l1666h:
	ld d,028h		;1666	16 28		. (
l1668h:
	bit 1,(iy+005h)		;1668	fd cb 05 4e	. . . N
	ld (iy+006h),d		;166c	fd 72 06	. r .
	ld a,d			;166f	7a		z
	jr z,l1673h		;1670	28 01		( .
	add a,a			;1672	87		.
l1673h:
	ld (iy+012h),a		;1673	fd 77 12	. w .
	ret			;1676	c9		.
sub_1677h:
	ld c,0ffh		;1677	0e ff		. .
l1679h:
	inc c			;1679	0c		.
	bit 2,c			;167a	cb 51		. Q
	jp nz,0153bh		;167c	c2 3b 15	. ; .
	ld a,c			;167f	79		y
	or 008h			;1680	f6 08		. .
	out (01fh),a		;1682	d3 1f		. .
	call l1523h		;1684	cd 23 15	. # .
	jr nz,l1679h		;1687	20 f0		  .
	ld a,014h		;1689	3e 14		> .
	call sub_1623h		;168b	cd 23 16	. # .
	jr nz,l1679h		;168e	20 e9		  .
	ld a,001h		;1690	3e 01		> .
	call sub_1623h		;1692	cd 23 16	. # .
	jr nz,l1679h		;1695	20 e2		  .
	bit 2,e			;1697	cb 53		. S
	jr nz,l1679h		;1699	20 de		  .
	ld a,c			;169b	79		y
	or 038h			;169c	f6 38		. 8
	out (01fh),a		;169e	d3 1f		. .
	call l1523h		;16a0	cd 23 15	. # .
	jr nz,l1679h		;16a3	20 d4		  .
	bit 2,a			;16a5	cb 57		. W
	jp z,l1679h		;16a7	ca 79 16	. y .
	ld (iy+020h),c		;16aa	fd 71 20	. q  
	ret			;16ad	c9		.
sub_16aeh:
	ld a,c			;16ae	79		y
	jr l16b3h		;16af	18 02		. .
sub_16b1h:
	ld b,001h		;16b1	06 01		. .
l16b3h:
	ex af,af'		;16b3	08		.
l16b4h:
	in a,(0ffh)		;16b4	db ff		. .
	and 0c0h		;16b6	e6 c0		. .
	jr z,l16b4h		;16b8	28 fa		( .
	ret m			;16ba	f8		.
	ex af,af'		;16bb	08		.
	out (07fh),a		;16bc	d3 7f		. .
	djnz l16b3h		;16be	10 f3		. .
	ret			;16c0	c9		.
sub_16c1h:
	ld a,(iy+01dh)		;16c1	fd 7e 1d	. ~ .
	ld c,a			;16c4	4f		O
	or a			;16c5	b7		.
	jp po,l16cah		;16c6	e2 ca 16	. . .
	inc c			;16c9	0c		.
l16cah:
	ld b,000h		;16ca	06 00		. .
	sub 001h		;16cc	d6 01		. .
	rr b			;16ce	cb 18		. .
	push bc			;16d0	c5		.
	ld de,07f40h		;16d1	11 40 7f	. @ .
	ld a,0f4h		;16d4	3e f4		> .
	di			;16d6	f3		.
	out (01fh),a		;16d7	d3 1f		. .
	ld a,04eh		;16d9	3e 4e		> N
	ld b,(iy+01fh)		;16db	fd 46 1f	. F .
	call l16b3h		;16de	cd b3 16	. . .
	ld bc,l0c00h		;16e1	01 00 0c	. . .
	call sub_16aeh		;16e4	cd ae 16	. . .
	ld bc,l03f6h		;16e7	01 f6 03	. . .
	call sub_16aeh		;16ea	cd ae 16	. . .
	ld a,0fch		;16ed	3e fc		> .
	call sub_16b1h		;16ef	cd b1 16	. . .
l16f2h:
	ld bc,0324eh		;16f2	01 4e 32	. N 2
	call sub_16aeh		;16f5	cd ae 16	. . .
	ld bc,l0c00h		;16f8	01 00 0c	. . .
	call sub_16aeh		;16fb	cd ae 16	. . .
	ld bc,003f5h		;16fe	01 f5 03	. . .
	call sub_16aeh		;1701	cd ae 16	. . .
	ld a,0feh		;1704	3e fe		> .
	call sub_16b1h		;1706	cd b1 16	. . .
	ld a,(05fb0h)		;1709	3a b0 5f	: . _
	call sub_16b1h		;170c	cd b1 16	. . .
	ld a,(05fafh)		;170f	3a af 5f	: . _
	call sub_16b1h		;1712	cd b1 16	. . .
	ld a,(de)		;1715	1a		.
	inc de			;1716	13		.
	call sub_16b1h		;1717	cd b1 16	. . .
	ld a,(iy+01dh)		;171a	fd 7e 1d	. ~ .
	call sub_16b1h		;171d	cd b1 16	. . .
	ld a,0f7h		;1720	3e f7		> .
	call sub_16b1h		;1722	cd b1 16	. . .
	ld bc,l164eh		;1725	01 4e 16	. N .
	call sub_16aeh		;1728	cd ae 16	. . .
	ld bc,l0c00h		;172b	01 00 0c	. . .
	call sub_16aeh		;172e	cd ae 16	. . .
	ld bc,003f5h		;1731	01 f5 03	. . .
	call sub_16aeh		;1734	cd ae 16	. . .
	ld a,0fbh		;1737	3e fb		> .
	call sub_16b1h		;1739	cd b1 16	. . .
	ld a,(05fa8h)		;173c	3a a8 5f	: . _
	pop bc			;173f	c1		.
	push bc			;1740	c5		.
l1741h:
	call l16b3h		;1741	cd b3 16	. . .
	dec c			;1744	0d		.
	jr nz,l1741h		;1745	20 fa		  .
	ld a,0f7h		;1747	3e f7		> .
	call sub_16b1h		;1749	cd b1 16	. . .
	pop hl			;174c	e1		.
	ret m			;174d	f8		.
	push hl			;174e	e5		.
	ld a,(de)		;174f	1a		.
	or a			;1750	b7		.
	jr nz,l16f2h		;1751	20 9f		  .
	pop hl			;1753	e1		.
	ld c,005h		;1754	0e 05		. .
l1756h:
	dec c			;1756	0d		.
	ret m			;1757	f8		.
	ld a,04eh		;1758	3e 4e		> N
	call l16b3h		;175a	cd b3 16	. . .
	jp p,l1756h		;175d	f2 56 17	. V .
	xor a			;1760	af		.
	ret			;1761	c9		.
	ld a,(05f5dh)		;1762	3a 5d 5f	: ] _
	ld (05f56h),a		;1765	32 56 5f	2 V _
	res 3,(ix-062h)		;1768	dd cb 9e 9e	. . . .
	call sub_3872h		;176c	cd 72 38	. r 8
	call sub_145dh		;176f	cd 5d 14	. ] .
	call sub_14adh		;1772	cd ad 14	. . .
	ld a,040h		;1775	3e 40		> @
	ret nz			;1777	c0		.
	ld a,(05fadh)		;1778	3a ad 5f	: . _
	call sub_17ceh		;177b	cd ce 17	. . .
	ld b,008h		;177e	06 08		. .
	di			;1780	f3		.
l1781h:
	push bc			;1781	c5		.
	call sub_16c1h		;1782	cd c1 16	. . .
	pop bc			;1785	c1		.
	in a,(01fh)		;1786	db 1f		. .
	ld c,a			;1788	4f		O
	jp p,l179eh		;1789	f2 9e 17	. . .
	bit 0,c			;178c	cb 41		. A
	jr z,l1794h		;178e	28 04		( .
	ld a,0d0h		;1790	3e d0		> .
	out (01fh),a		;1792	d3 1f		. .
l1794h:
	bit 6,c			;1794	cb 71		. q
	jp nz,l15b6h		;1796	c2 b6 15	. . .
l1799h:
	djnz l1781h		;1799	10 e6		. .
	jp l15b6h		;179b	c3 b6 15	. . .
l179eh:
	ld de,07f41h		;179e	11 41 7f	. A .
l17a1h:
	ld a,(de)		;17a1	1a		.
	or a			;17a2	b7		.
	jr nz,l17a6h		;17a3	20 01		  .
	inc a			;17a5	3c		<
l17a6h:
	out (05fh),a		;17a6	d3 5f		. _
	ld a,080h		;17a8	3e 80		> .
	out (01fh),a		;17aa	d3 1f		. .
l17ach:
	in a,(0ffh)		;17ac	db ff		. .
	and 0c0h		;17ae	e6 c0		. .
	jr z,l17ach		;17b0	28 fa		( .
	in a,(01fh)		;17b2	db 1f		. .
	and 018h		;17b4	e6 18		. .
	ld c,a			;17b6	4f		O
	ld a,0d0h		;17b7	3e d0		> .
	out (01fh),a		;17b9	d3 1f		. .
	jr nz,l1799h		;17bb	20 dc		  .
	ld a,(de)		;17bd	1a		.
	inc de			;17be	13		.
	or a			;17bf	b7		.
	jr nz,l17a1h		;17c0	20 df		  .
	ld a,(05f56h)		;17c2	3a 56 5f	: V _
	ld (05f5dh),a		;17c5	32 5d 5f	2 ] _
	call sub_3872h		;17c8	cd 72 38	. r 8
	jp l14a2h		;17cb	c3 a2 14	. . .
sub_17ceh:
	ld hl,07f40h		;17ce	21 40 7f	! @ .
	ld de,07f41h		;17d1	11 41 7f	. A .
	ld c,(iy+008h)		;17d4	fd 4e 08	. N .
	ld b,000h		;17d7	06 00		. .
	ld (hl),000h		;17d9	36 00		6 .
	ldir			;17db	ed b0		. .
	ld hl,07f40h		;17dd	21 40 7f	! @ .
	ld c,a			;17e0	4f		O
	ld b,(iy+008h)		;17e1	fd 46 08	. F .
	ld d,001h		;17e4	16 01		. .
	jr l17efh		;17e6	18 07		. .
l17e8h:
	push bc			;17e8	c5		.
	ld c,001h		;17e9	0e 01		. .
	call sub_17fdh		;17eb	cd fd 17	. . .
	pop bc			;17ee	c1		.
l17efh:
	ld a,(hl)		;17ef	7e		~
	or a			;17f0	b7		.
	jr nz,l17e8h		;17f1	20 f5		  .
	ld (hl),d		;17f3	72		r
	inc d			;17f4	14		.
	push bc			;17f5	c5		.
	call sub_17fdh		;17f6	cd fd 17	. . .
	pop bc			;17f9	c1		.
	djnz l17efh		;17fa	10 f3		. .
	ret			;17fc	c9		.
sub_17fdh:
	push de			;17fd	d5		.
	ld b,000h		;17fe	06 00		. .
	add hl,bc		;1800	09		.
	ld c,(iy+008h)		;1801	fd 4e 08	. N .
	ex de,hl		;1804	eb		.
	ld hl,07f40h		;1805	21 40 7f	! @ .
	add hl,bc		;1808	09		.
	dec hl			;1809	2b		+
	sbc hl,de		;180a	ed 52		. R
	ex de,hl		;180c	eb		.
	jr nc,l1812h		;180d	30 03		0 .
	or a			;180f	b7		.
	sbc hl,bc		;1810	ed 42		. B
l1812h:
	pop de			;1812	d1		.
	ret			;1813	c9		.
	ld (02418h),hl		;1814	22 18 24	" . $
	jr $-115		;1817	18 8b		. .
	jr l183dh		;1819	18 22		. "
	jr l1872h		;181b	18 55		. U
	jr $+36			;181d	18 22		. "
	jr $+116		;181f	18 72		. r
	jr $-79			;1821	18 af		. .
	ret			;1823	c9		.
	ld a,(05fb0h)		;1824	3a b0 5f	: . _
	dec a			;1827	3d		=
	ld (05f8dh),a		;1828	32 8d 5f	2 . _
	xor a			;182b	af		.
	ret			;182c	c9		.
sub_182dh:
	bit 0,(ix-01ah)		;182d	dd cb e6 46	. . . F
	call nz,sub_1846h	;1831	c4 46 18	. F .
	bit 7,(ix-032h)		;1834	dd cb ce 7e	. . . ~
	jr nz,l186eh		;1838	20 34		  4
	ld a,(05fb4h)		;183a	3a b4 5f	: . _
l183dh:
	rra			;183d	1f		.
	ld l,000h		;183e	2e 00		. .
	rr l			;1840	cb 1d		. .
	or 0c0h			;1842	f6 c0		. .
	ld h,a			;1844	67		g
	ret			;1845	c9		.
sub_1846h:
	res 0,(ix-01ah)		;1846	dd cb e6 86	. . . .
	xor a			;184a	af		.
	call PageZeroReserved_end	;184b	cd 4b 00	. K .
	rst 18h			;184e	df		.
	ex af,af'		;184f	08		.
	ld a,0e5h		;1850	3e e5		> .
	jp l189ah		;1852	c3 9a 18	. . .
	call sub_182dh		;1855	cd 2d 18	. - .
	ld a,(05f8dh)		;1858	3a 8d 5f	: . _
	call PageZeroReserved_end	;185b	cd 4b 00	. K .
	ld (05f90h),a		;185e	32 90 5f	2 . _
	ld bc,l0080h		;1861	01 80 00	. . .
	ld de,(05fb5h)		;1864	ed 5b b5 5f	. [ . _
	rst 8			;1868	cf		.
	inc bc			;1869	03		.
	rst 38h			;186a	ff		.
	ei			;186b	fb		.
	xor a			;186c	af		.
	ret			;186d	c9		.
l186eh:
	pop af			;186e	f1		.
l186fh:
	ld a,008h		;186f	3e 08		> .
	ret			;1871	c9		.
l1872h:
	call sub_182dh		;1872	cd 2d 18	. - .
	ld a,(05f8dh)		;1875	3a 8d 5f	: . _
	call PageZeroReserved_end	;1878	cd 4b 00	. K .
	ld (05f91h),a		;187b	32 91 5f	2 . _
	ld bc,l0080h		;187e	01 80 00	. . .
	ex de,hl		;1881	eb		.
	ld hl,(05fb5h)		;1882	2a b5 5f	* . _
	rst 8			;1885	cf		.
	rst 38h			;1886	ff		.
	inc bc			;1887	03		.
	ei			;1888	fb		.
	xor a			;1889	af		.
	ret			;188a	c9		.
	ld a,(05fb0h)		;188b	3a b0 5f	: . _
	dec a			;188e	3d		=
	jp m,l186fh		;188f	fa 6f 18	. o .
	call PageZeroReserved_end	;1892	cd 4b 00	. K .
	rst 18h			;1895	df		.
	ex af,af'		;1896	08		.
	ld a,(05fa8h)		;1897	3a a8 5f	: . _
l189ah:
	ld hl,0c000h		;189a	21 00 c0	! . .
	ld de,0c001h		;189d	11 01 c0	. . .
	ld bc,l3fffh		;18a0	01 ff 3f	. . ?
	ld (hl),a		;18a3	77		w
	ldir			;18a4	ed b0		. .
	ex af,af'		;18a6	08		.
	rst 18h			;18a7	df		.
	ei			;18a8	fb		.
	xor a			;18a9	af		.
	ret			;18aa	c9		.
	ld a,(bc)		;18ab	0a		.
	add hl,de		;18ac	19		.
	ld l,d			;18ad	6a		j
	add hl,de		;18ae	19		.
	rlca			;18af	07		.
	ld a,(de)		;18b0	1a		.
	ld a,(bc)		;18b1	0a		.
	add hl,de		;18b2	19		.
	sbc a,h			;18b3	9c		.
	add hl,de		;18b4	19		.
	jr nc,$+27		;18b5	30 19		0 .
	ret c			;18b7	d8		.
	add hl,de		;18b8	19		.
sub_18b9h:
	bit 6,(iy+003h)		;18b9	fd cb 03 76	. . . v
	ret nz			;18bd	c0		.
	bit 7,(iy+003h)		;18be	fd cb 03 7e	. . . ~
	jr nz,l1901h		;18c2	20 3d		  =
	ld bc,l00efh		;18c4	01 ef 00	. . .
	in a,(c)		;18c7	ed 78		. x
	inc a			;18c9	3c		<
	jr z,l1901h		;18ca	28 35		( 5
	ld d,000h		;18cc	16 00		. .
	call sub_1916h		;18ce	cd 16 19	. . .
	or a			;18d1	b7		.
	jr nz,l1901h		;18d2	20 2d		  -
	ld bc,000afh		;18d4	01 af 00	. . .
	ld a,055h		;18d7	3e 55		> U
	out (c),a		;18d9	ed 79		. y
	cpl			;18db	2f		/
	ld c,08fh		;18dc	0e 8f		. .
	out (c),a		;18de	ed 79		. y
	ld c,0afh		;18e0	0e af		. .
	in a,(c)		;18e2	ed 78		. x
	cp 055h			;18e4	fe 55		. U
	jr nz,l1901h		;18e6	20 19		  .
	ld c,08fh		;18e8	0e 8f		. .
	in a,(c)		;18ea	ed 78		. x
	cp 0aah			;18ec	fe aa		. .
	jr nz,l1901h		;18ee	20 11		  .
	set 6,(iy+003h)		;18f0	fd cb 03 f6	. . . .
sub_18f4h:
	ld hl,(05fb0h)		;18f4	2a b0 5f	* . _
	ld bc,0008fh		;18f7	01 8f 00	. . .
	out (c),l		;18fa	ed 69		. i
	ld c,0afh		;18fc	0e af		. .
	out (c),h		;18fe	ed 61		. a
	ret			;1900	c9		.
l1901h:
	set 7,(iy+003h)		;1901	fd cb 03 fe	. . . .
	pop af			;1905	f1		.
l1906h:
	ld a,059h		;1906	3e 59		> Y
	or a			;1908	b7		.
	ret			;1909	c9		.
l190ah:
	call sub_18b9h		;190a	cd b9 18	. . .
	ld bc,l00efh		;190d	01 ef 00	. . .
	ld a,010h		;1910	3e 10		> .
	out (c),a		;1912	ed 79		. y
l1914h:
	ld d,040h		;1914	16 40		. @
sub_1916h:
	ld bc,l00efh		;1916	01 ef 00	. . .
	ld hl,WbootStub		;1919	21 00 00	! . .
l191ch:
	in e,(c)		;191c	ed 58		. X
	bit 7,e			;191e	cb 7b		. {
	jr z,l192ch		;1920	28 0a		( .
	dec hl			;1922	2b		+
	ld a,l			;1923	7d		}
	or h			;1924	b4		.
	jr nz,l191ch		;1925	20 f5		  .
	dec d			;1927	15		.
	jr nz,l191ch		;1928	20 f2		  .
	jr l1906h		;192a	18 da		. .
l192ch:
	bit 0,e			;192c	cb 43		. C
	jr nz,l1932h		;192e	20 02		  .
	xor a			;1930	af		.
	ret			;1931	c9		.
l1932h:
	ld bc,l002fh		;1932	01 2f 00	. / .
	in b,(c)		;1935	ed 40		. @
	bit 0,b			;1937	cb 40		. @
	ld a,082h		;1939	3e 82		> .
	ret nz			;193b	c0		.
	bit 2,b			;193c	cb 50		. P
	jr nz,l1958h		;193e	20 18		  .
	bit 4,b			;1940	cb 60		. `
	ld a,081h		;1942	3e 81		> .
	ret nz			;1944	c0		.
	bit 6,b			;1945	cb 70		. p
	jr z,l1950h		;1947	28 07		( .
	bit 2,e			;1949	cb 53		. S
	ld a,084h		;194b	3e 84		> .
	ret z			;194d	c8		.
	xor a			;194e	af		.
	ret			;194f	c9		.
l1950h:
	bit 7,b			;1950	cb 78		. x
	ld a,056h		;1952	3e 56		> V
	ret nz			;1954	c0		.
	ld a,040h		;1955	3e 40		> @
	ret			;1957	c9		.
l1958h:
	bit 6,e			;1958	cb 73		. s
	ld a,059h		;195a	3e 59		> Y
	ret nz			;195c	c0		.
	bit 5,e			;195d	cb 6b		. k
	ld a,054h		;195f	3e 54		> T
	ret nz			;1961	c0		.
	bit 4,e			;1962	cb 63		. c
	ld a,053h		;1964	3e 53		> S
	ret nz			;1966	c0		.
	ld a,052h		;1967	3e 52		> R
	ret			;1969	c9		.
sub_196ah:
	call sub_18b9h		;196a	cd b9 18	. . .
	ld hl,(05fb0h)		;196d	2a b0 5f	* . _
	ld a,h			;1970	7c		|
	or l			;1971	b5		.
	jr z,l190ah		;1972	28 96		( .
	call sub_18f4h		;1974	cd f4 18	. . .
	ld c,0efh		;1977	0e ef		. .
	ld a,070h		;1979	3e 70		> p
	out (c),a		;197b	ed 79		. y
	jr l1914h		;197d	18 95		. .
sub_197fh:
	ld a,(05fafh)		;197f	3a af 5f	: . _
	or 0a0h			;1982	f6 a0		. .
	ld bc,000cfh		;1984	01 cf 00	. . .
	out (c),a		;1987	ed 79		. y
	ld a,(05fb4h)		;1989	3a b4 5f	: . _
	inc a			;198c	3c		<
	ld c,06fh		;198d	0e 6f		. o
	out (c),a		;198f	ed 79		. y
	ld a,d			;1991	7a		z
	ld c,04fh		;1992	0e 4f		. O
	out (c),a		;1994	ed 79		. y
	call sub_18f4h		;1996	cd f4 18	. . .
	ld c,0efh		;1999	0e ef		. .
	ret			;199b	c9		.
	call sub_18b9h		;199c	cd b9 18	. . .
	ld d,001h		;199f	16 01		. .
	call sub_197fh		;19a1	cd 7f 19	. . .
	ld a,020h		;19a4	3e 20		>  
	out (c),a		;19a6	ed 79		. y
	call l1914h		;19a8	cd 14 19	. . .
	or a			;19ab	b7		.
	ret nz			;19ac	c0		.
	ld hl,(05fb5h)		;19ad	2a b5 5f	* . _
	ld bc,l000fh		;19b0	01 0f 00	. . .
	inir			;19b3	ed b2		. .
	inir			;19b5	ed b2		. .
	ld bc,l00efh		;19b7	01 ef 00	. . .
	in a,(c)		;19ba	ed 78		. x
	and 008h		;19bc	e6 08		. .
	ret z			;19be	c8		.
	ld a,083h		;19bf	3e 83		> .
	ret			;19c1	c9		.
sub_19c2h:
	call sub_197fh		;19c2	cd 7f 19	. . .
	ld a,030h		;19c5	3e 30		> 0
	out (c),a		;19c7	ed 79		. y
l19c9h:
	in e,(c)		;19c9	ed 58		. X
	bit 0,e			;19cb	cb 43		. C
	jp nz,l1932h		;19cd	c2 32 19	. 2 .
	bit 3,e			;19d0	cb 5b		. [
	jr z,l19c9h		;19d2	28 f5		( .
	ld bc,0010fh		;19d4	01 0f 01	. . .
	ret			;19d7	c9		.
	call sub_18b9h		;19d8	cd b9 18	. . .
	ld d,001h		;19db	16 01		. .
	call sub_19c2h		;19dd	cd c2 19	. . .
	ld hl,(05fb5h)		;19e0	2a b5 5f	* . _
	dec d			;19e3	15		.
	inc hl			;19e4	23		#
l19e5h:
	ld a,(hl)		;19e5	7e		~
	out (c),a		;19e6	ed 79		. y
	dec b			;19e8	05		.
	dec hl			;19e9	2b		+
	ld a,(hl)		;19ea	7e		~
	out (c),a		;19eb	ed 79		. y
	inc b			;19ed	04		.
	inc hl			;19ee	23		#
	inc hl			;19ef	23		#
	inc hl			;19f0	23		#
	push bc			;19f1	c5		.
	ld bc,l00efh		;19f2	01 ef 00	. . .
	in a,(c)		;19f5	ed 78		. x
	pop bc			;19f7	c1		.
	dec d			;19f8	15		.
	jr nz,l19e5h		;19f9	20 ea		  .
	ld bc,07ffdh		;19fb	01 fd 7f	. . .
l19feh:
	in a,(c)		;19fe	ed 78		. x
	and 040h		;1a00	e6 40		. @
	jr z,l19feh		;1a02	28 fa		( .
	jp l1914h		;1a04	c3 14 19	. . .
	call sub_196ah		;1a07	cd 6a 19	. j .
	or a			;1a0a	b7		.
	ret nz			;1a0b	c0		.
	ld d,(iy+008h)		;1a0c	fd 56 08	. V .
	call sub_19c2h		;1a0f	cd c2 19	. . .
l1a12h:
	ld a,(05fa8h)		;1a12	3a a8 5f	: . _
	out (c),a		;1a15	ed 79		. y
	dec b			;1a17	05		.
	ld e,b			;1a18	58		X
l1a19h:
	push bc			;1a19	c5		.
	ld bc,l00efh		;1a1a	01 ef 00	. . .
	in b,(c)		;1a1d	ed 40		. @
	pop bc			;1a1f	c1		.
	out (c),a		;1a20	ed 79		. y
	dec e			;1a22	1d		.
	jr nz,l1a19h		;1a23	20 f4		  .
	ld bc,07ffdh		;1a25	01 fd 7f	. . .
l1a28h:
	in a,(c)		;1a28	ed 78		. x
	and 040h		;1a2a	e6 40		. @
	jr z,l1a28h		;1a2c	28 fa		( .
	ld bc,l00efh		;1a2e	01 ef 00	. . .
	in e,(c)		;1a31	ed 58		. X
	bit 0,e			;1a33	cb 43		. C
	jp nz,l1932h		;1a35	c2 32 19	. 2 .
	bit 3,e			;1a38	cb 5b		. [
	jp z,l1914h		;1a3a	ca 14 19	. . .
	ld bc,0010fh		;1a3d	01 0f 01	. . .
	jr l1a12h		;1a40	18 d0		. .
sub_1a42h:
	ld (ix-018h),000h	;1a42	dd 36 e8 00	. 6 . .
	call sub_1b38h		;1a46	cd 38 1b	. 8 .
	ret nz			;1a49	c0		.
	ld (ix-018h),003h	;1a4a	dd 36 e8 03	. 6 . .
	call sub_1b38h		;1a4e	cd 38 1b	. 8 .
	ret nz			;1a51	c0		.
	ld (ix-018h),004h	;1a52	dd 36 e8 04	. 6 . .
	ld hl,CcpTable1A64_start	;1a56	21 64 1a	! d .
	ld de,05fa8h		;1a59	11 a8 5f	. . _
	ld bc,l0007h		;1a5c	01 07 00	. . .
	ldir			;1a5f	ed b0		. .
	jp sub_1b38h		;1a61	c3 38 1b	. 8 .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpTable1A64 - CCP lookup island.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpTable1A64' (start 0x1a64 end 0x1a6b)
CcpTable1A64_start:
	defb 001h		;1a64	01		.
	defb 000h		;1a65	00		.
	defb 000h		;1a66	00		.
	defb 001h		;1a67	01		.
	defb 000h		;1a68	00		.
	defb 000h		;1a69	00		.
	defb 080h		;1a6a	80		.
CcpTable1A64_end:
	ld (ix-019h),000h	;1a6b	dd 36 e7 00	. 6 . .
	call sub_1a42h		;1a6f	cd 42 1a	. B .
	jr z,l1a7ch		;1a72	28 08		( .
	ld (ix-019h),002h	;1a74	dd 36 e7 02	. 6 . .
	call sub_1a42h		;1a78	cd 42 1a	. B .
	ret nz			;1a7b	c0		.
l1a7ch:
	ld hl,08000h		;1a7c	21 00 80	! . .
	ld b,080h		;1a7f	06 80		. .
	xor a			;1a81	af		.
l1a82h:
	add a,(hl)		;1a82	86		.
	adc a,000h		;1a83	ce 00		. .
	inc hl			;1a85	23		#
	djnz l1a82h		;1a86	10 fa		. .
	dec a			;1a88	3d		=
	ret nz			;1a89	c0		.
	ld a,(08003h)		;1a8a	3a 03 80	: . .
	cp 055h			;1a8d	fe 55		. U
	ret nz			;1a8f	c0		.
	ld a,(08004h)		;1a90	3a 04 80	: . .
	cp 0aah			;1a93	fe aa		. .
	ret nz			;1a95	c0		.
	ld a,0c9h		;1a96	3e c9		> .
	ld (0f8dbh),a		;1a98	32 db f8	2 . .
	ld (0f8feh),a		;1a9b	32 fe f8	2 . .
	ld iy,08005h		;1a9e	fd 21 05 80	. ! . .
l1aa2h:
	ld a,(iy+000h)		;1aa2	fd 7e 00	. ~ .
	call sub_1aaah		;1aa5	cd aa 1a	. . .
	jr l1ac0h		;1aa8	18 16		. .
sub_1aaah:
	call sub_11efh		;1aaa	cd ef 11	. . .
	add hl,bc		;1aad	09		.
	jp p,0fc1ah		;1aae	f2 1a fc	. . .
	ld a,(de)		;1ab1	1a		.
	rlca			;1ab2	07		.
	dec de			;1ab3	1b		.
	ld a,(de)		;1ab4	1a		.
	dec de			;1ab5	1b		.
	ld c,d			;1ab6	4a		J
	dec de			;1ab7	1b		.
	ld (hl),e		;1ab8	73		s
	dec de			;1ab9	1b		.
	ld a,(hl)		;1aba	7e		~
	dec de			;1abb	1b		.
	adc a,(hl)		;1abc	8e		.
	dec de			;1abd	1b		.
	and l			;1abe	a5		.
	dec de			;1abf	1b		.
l1ac0h:
	or a			;1ac0	b7		.
	jr z,l1af3h		;1ac1	28 30		( 0
	call MonitorPrint	;1ac3	cd 06 12	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpMsgConfigError - "\r\nConfig error " message.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpMsgConfigError' (start 0x1ac6 end 0x1ad6)
CcpMsgConfigError_start:
	defb 00dh		;1ac6	0d		.
	defb 00ah		;1ac7	0a		.
	defb 043h		;1ac8	43		C
	defb 06fh		;1ac9	6f		o
	defb 06eh		;1aca	6e		n
	defb 066h		;1acb	66		f
	defb 069h		;1acc	69		i
	defb 067h		;1acd	67		g
	defb 020h		;1ace	20		 
	defb 065h		;1acf	65		e
	defb 072h		;1ad0	72		r
	defb 072h		;1ad1	72		r
	defb 06fh		;1ad2	6f		o
	defb 072h		;1ad3	72		r
	defb 020h		;1ad4	20		 
	defb 000h		;1ad5	00		.
CcpMsgConfigError_end:
	call sub_120eh		;1ad6	cd 0e 12	. . .
	call MonitorPrint	;1ad9	cd 06 12	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpMsgStartupAborted - "startup aborted!\r\n" message.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpMsgStartupAborted' (start 0x1adc end 0x1af0)
CcpMsgStartupAborted_start:
	defb 073h		;1adc	73		s
	defb 074h		;1add	74		t
	defb 061h		;1ade	61		a
	defb 072h		;1adf	72		r
	defb 074h		;1ae0	74		t
	defb 075h		;1ae1	75		u
	defb 070h		;1ae2	70		p
	defb 020h		;1ae3	20		 
	defb 061h		;1ae4	61		a
	defb 062h		;1ae5	62		b
	defb 06fh		;1ae6	6f		o
	defb 072h		;1ae7	72		r
	defb 074h		;1ae8	74		t
	defb 065h		;1ae9	65		e
	defb 064h		;1aea	64		d
	defb 021h		;1aeb	21		!
	defb 00dh		;1aec	0d		.
	defb 00ah		;1aed	0a		.
	defb 000h		;1aee	00		.
	defb 037h		;1aef	37		7
CcpMsgStartupAborted_end:
	jr l1afeh		;1af0	18 0c		. .
	pop af			;1af2	f1		.
l1af3h:
	ld b,000h		;1af3	06 00		. .
l1af5h:
	ld c,(iy+001h)		;1af5	fd 4e 01	. N .
	add iy,bc		;1af8	fd 09		. .
	jr l1aa2h		;1afa	18 a6		. .
	pop af			;1afc	f1		.
	or a			;1afd	b7		.
l1afeh:
	ld a,0f3h		;1afe	3e f3		> .
	ld (0f8dbh),a		;1b00	32 db f8	2 . .
	ld (0f8feh),a		;1b03	32 fe f8	2 . .
	ret			;1b06	c9		.
	pop af			;1b07	f1		.
	ld b,(iy+002h)		;1b08	fd 46 02	. F .
	jr l1af5h		;1b0b	18 e8		. .
sub_1b0dh:
	ex (sp),hl		;1b0d	e3		.
	ld e,(hl)		;1b0e	5e		^
	inc hl			;1b0f	23		#
	ex (sp),hl		;1b10	e3		.
	ld d,000h		;1b11	16 00		. .
	push iy			;1b13	fd e5		. .
	ex (sp),hl		;1b15	e3		.
	add hl,de		;1b16	19		.
	ex (sp),hl		;1b17	e3		.
	pop de			;1b18	d1		.
	ret			;1b19	c9		.
	call sub_1b0dh		;1b1a	cd 0d 1b	. . .
	inc bc			;1b1d	03		.
l1b1eh:
	ld a,007h		;1b1e	3e 07		> .
	ld c,009h		;1b20	0e 09		. .
	call sub_1245h		;1b22	cd 45 12	. E .
	or a			;1b25	b7		.
	ret nz			;1b26	c0		.
	ld de,05fa6h		;1b27	11 a6 5f	. . _
	ld hl,l1b41h		;1b2a	21 41 1b	! A .
	ld bc,Rst08Vector+1	;1b2d	01 09 00	. . .
	ldir			;1b30	ed b0		. .
	ld a,(iy+002h)		;1b32	fd 7e 02	. ~ .
	ld (05fa8h),a		;1b35	32 a8 5f	2 . _
sub_1b38h:
	push iy			;1b38	fd e5		. .
	call sub_125ch		;1b3a	cd 5c 12	. \ .
	pop iy			;1b3d	fd e1		. .
	or a			;1b3f	b7		.
	ret			;1b40	c9		.
l1b41h:
	add hl,bc		;1b41	09		.
	inc b			;1b42	04		.
	ld bc,WbootStub		;1b43	01 00 00	. . .
	ld (bc),a		;1b46	02		.
	nop			;1b47	00		.
	add a,b			;1b48	80		.
	add a,b			;1b49	80		.
	call sub_1b0dh		;1b4a	cd 0d 1b	. . .
	inc b			;1b4d	04		.
	ld c,(iy+003h)		;1b4e	fd 4e 03	. N .
	bit 7,c			;1b51	cb 79		. y
	jr nz,l1b60h		;1b53	20 0b		  .
	push de			;1b55	d5		.
	ld a,007h		;1b56	3e 07		> .
	call sub_1245h		;1b58	cd 45 12	. E .
	pop de			;1b5b	d1		.
	or a			;1b5c	b7		.
	ret nz			;1b5d	c0		.
	jr l1b63h		;1b5e	18 03		. .
l1b60h:
	ld de,WbootStub		;1b60	11 00 00	. . .
l1b63h:
	ld b,(iy+002h)		;1b63	fd 46 02	. F .
	bit 7,b			;1b66	cb 78		. x
	ret nz			;1b68	c0		.
	ld c,(iy+003h)		;1b69	fd 4e 03	. N .
	set 7,b			;1b6c	cb f8		. .
	res 7,c			;1b6e	cb b9		. .
	jp 0ea33h		;1b70	c3 33 ea	. 3 .
	call sub_1b0dh		;1b73	cd 0d 1b	. . .
	ld (bc),a		;1b76	02		.
l1b77h:
	ld a,(de)		;1b77	1a		.
	inc de			;1b78	13		.
	or a			;1b79	b7		.
	ret z			;1b7a	c8		.
	rst 20h			;1b7b	e7		.
	jr l1b77h		;1b7c	18 f9		. .
	ld l,(iy+002h)		;1b7e	fd 6e 02	. n .
	ld h,(iy+003h)		;1b81	fd 66 03	. f .
	ld a,h			;1b84	7c		|
	or l			;1b85	b5		.
	jr nz,l1b8dh		;1b86	20 05		  .
	call sub_1b0dh		;1b88	cd 0d 1b	. . .
	inc b			;1b8b	04		.
	ex de,hl		;1b8c	eb		.
l1b8dh:
	jp (hl)			;1b8d	e9		.
	call sub_1b0dh		;1b8e	cd 0d 1b	. . .
	ld (bc),a		;1b91	02		.
	ld a,(iy+001h)		;1b92	fd 7e 01	. ~ .
	sub 002h		;1b95	d6 02		. .
	ld (0d407h),a		;1b97	32 07 d4	2 . .
	ld c,a			;1b9a	4f		O
	ld b,000h		;1b9b	06 00		. .
	ex de,hl		;1b9d	eb		.
	ld de,0d408h		;1b9e	11 08 d4	. . .
	ldir			;1ba1	ed b0		. .
	xor a			;1ba3	af		.
	ret			;1ba4	c9		.
	ld a,(iy+002h)		;1ba5	fd 7e 02	. ~ .
	ld (05fa6h),a		;1ba8	32 a6 5f	2 . _
	ld a,(iy+003h)		;1bab	fd 7e 03	. ~ .
	ld (05fa9h),a		;1bae	32 a9 5f	2 . _
	ld a,(iy+004h)		;1bb1	fd 7e 04	. ~ .
	ld (05fa8h),a		;1bb4	32 a8 5f	2 . _
	ld (ix-015h),000h	;1bb7	dd 36 eb 00	. 6 . .
	ld (ix-012h),002h	;1bbb	dd 36 ee 02	. 6 . .
	ld (ix-018h),002h	;1bbf	dd 36 e8 02	. 6 . .
	jp sub_1b38h		;1bc3	c3 38 1b	. 8 .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpBase - DRI Console Command Processor, cold entry.
; JP $D75C / JP $D758: trampolines to +$35C/+$358 inside the $D400
; copy (the t-$B83A view).  The DRI serial $7F 00 sits at +3, the
; "COPYRIGHT (C) 1979, DIGITAL RESEARCH" banner at +8; keywords
; DIR ERA TYPE SAVE REN USER at $1ED6.  Runtime $D400 is the warm
; entry every transient returns to.
; Drive mapping that decided the PR2 launch: A: = electronic (RAM)
; disk, B: = floppy; the CCP re-selects the CURRENT drive before
; entering a transient, so FCB-drive-0 programs need "B:" typed
; first (../bios-ram.md, ../pr2-loader.md).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CcpBase:
	jp 0d75ch		;1bc6	c3 5c d7	. \ .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpSerialBanner - DRI serial $7F $00 at CcpBase+3, then
; "COPYRIGHT (C) 1979, DIGITAL RESEARCH" and padding.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpSerialBanner' (start 0x1bc9 end 0x1c11)
CcpSerialBanner_start:
	defb 0c3h		;1bc9	c3		.
	defb 058h		;1bca	58		X
	defb 0d7h		;1bcb	d7		.
	defb 07fh		;1bcc	7f		.
	defb 000h		;1bcd	00		.
	defb 020h		;1bce	20		 
	defb 020h		;1bcf	20		 
	defb 020h		;1bd0	20		 
	defb 020h		;1bd1	20		 
	defb 020h		;1bd2	20		 
	defb 020h		;1bd3	20		 
	defb 020h		;1bd4	20		 
	defb 020h		;1bd5	20		 
	defb 020h		;1bd6	20		 
	defb 020h		;1bd7	20		 
	defb 020h		;1bd8	20		 
	defb 020h		;1bd9	20		 
	defb 020h		;1bda	20		 
	defb 020h		;1bdb	20		 
	defb 020h		;1bdc	20		 
	defb 020h		;1bdd	20		 
	defb 043h		;1bde	43		C
	defb 04fh		;1bdf	4f		O
	defb 050h		;1be0	50		P
	defb 059h		;1be1	59		Y
	defb 052h		;1be2	52		R
	defb 049h		;1be3	49		I
	defb 047h		;1be4	47		G
	defb 048h		;1be5	48		H
	defb 054h		;1be6	54		T
	defb 020h		;1be7	20		 
	defb 028h		;1be8	28		(
	defb 043h		;1be9	43		C
	defb 029h		;1bea	29		)
	defb 020h		;1beb	20		 
	defb 031h		;1bec	31		1
	defb 039h		;1bed	39		9
	defb 037h		;1bee	37		7
	defb 039h		;1bef	39		9
	defb 02ch		;1bf0	2c		,
	defb 020h		;1bf1	20		 
	defb 044h		;1bf2	44		D
	defb 049h		;1bf3	49		I
	defb 047h		;1bf4	47		G
	defb 049h		;1bf5	49		I
	defb 054h		;1bf6	54		T
	defb 041h		;1bf7	41		A
	defb 04ch		;1bf8	4c		L
	defb 020h		;1bf9	20		 
	defb 052h		;1bfa	52		R
	defb 045h		;1bfb	45		E
	defb 053h		;1bfc	53		S
	defb 045h		;1bfd	45		E
	defb 041h		;1bfe	41		A
	defb 052h		;1bff	52		R
	defb 043h		;1c00	43		C
	defb 048h		;1c01	48		H
	defb 020h		;1c02	20		 
	defb 020h		;1c03	20		 
	defb 000h		;1c04	00		.
	defb 000h		;1c05	00		.
	defb 000h		;1c06	00		.
	defb 000h		;1c07	00		.
	defb 000h		;1c08	00		.
	defb 000h		;1c09	00		.
	defb 000h		;1c0a	00		.
	defb 000h		;1c0b	00		.
	defb 000h		;1c0c	00		.
	defb 000h		;1c0d	00		.
	defb 000h		;1c0e	00		.
	defb 000h		;1c0f	00		.
	defb 000h		;1c10	00		.
CcpSerialBanner_end:
	nop			;1c11	00		.
	nop			;1c12	00		.
	nop			;1c13	00		.
	nop			;1c14	00		.
	nop			;1c15	00		.
	nop			;1c16	00		.
	nop			;1c17	00		.
	nop			;1c18	00		.
	nop			;1c19	00		.
	nop			;1c1a	00		.
	nop			;1c1b	00		.
	nop			;1c1c	00		.
	nop			;1c1d	00		.
	nop			;1c1e	00		.
	nop			;1c1f	00		.
	nop			;1c20	00		.
	nop			;1c21	00		.
	nop			;1c22	00		.
	nop			;1c23	00		.
	nop			;1c24	00		.
	nop			;1c25	00		.
	nop			;1c26	00		.
	nop			;1c27	00		.
	nop			;1c28	00		.
	nop			;1c29	00		.
	nop			;1c2a	00		.
	nop			;1c2b	00		.
	nop			;1c2c	00		.
	nop			;1c2d	00		.
	nop			;1c2e	00		.
	nop			;1c2f	00		.
	nop			;1c30	00		.
	nop			;1c31	00		.
	nop			;1c32	00		.
	nop			;1c33	00		.
	nop			;1c34	00		.
	nop			;1c35	00		.
	nop			;1c36	00		.
	nop			;1c37	00		.
	nop			;1c38	00		.
	nop			;1c39	00		.
	nop			;1c3a	00		.
	nop			;1c3b	00		.
	nop			;1c3c	00		.
	nop			;1c3d	00		.
	nop			;1c3e	00		.
	nop			;1c3f	00		.
	nop			;1c40	00		.
	nop			;1c41	00		.
	nop			;1c42	00		.
	nop			;1c43	00		.
	nop			;1c44	00		.
	nop			;1c45	00		.
	nop			;1c46	00		.
	nop			;1c47	00		.
	nop			;1c48	00		.
	nop			;1c49	00		.
	nop			;1c4a	00		.
	nop			;1c4b	00		.
	nop			;1c4c	00		.
	nop			;1c4d	00		.
	ex af,af'		;1c4e	08		.
	call nc,WbootStub	;1c4f	d4 00 00	. . .
	ld e,a			;1c52	5f		_
	ld c,002h		;1c53	0e 02		. .
	jp BdosEntry		;1c55	c3 05 00	. . .
	push bc			;1c58	c5		.
	call 0d48ch		;1c59	cd 8c d4	. . .
	pop bc			;1c5c	c1		.
	ret			;1c5d	c9		.
	ld a,00dh		;1c5e	3e 0d		> .
	call 0d492h		;1c60	cd 92 d4	. . .
	ld a,00ah		;1c63	3e 0a		> .
	jp 0d492h		;1c65	c3 92 d4	. . .
	ld a,020h		;1c68	3e 20		>  
	jp 0d492h		;1c6a	c3 92 d4	. . .
	push bc			;1c6d	c5		.
	call 0d498h		;1c6e	cd 98 d4	. . .
	pop hl			;1c71	e1		.
	ld a,(hl)		;1c72	7e		~
	or a			;1c73	b7		.
	ret z			;1c74	c8		.
	inc hl			;1c75	23		#
	push hl			;1c76	e5		.
	call 0d48ch		;1c77	cd 8c d4	. . .
	pop hl			;1c7a	e1		.
	jp 0d4ach		;1c7b	c3 ac d4	. . .
	ld c,00dh		;1c7e	0e 0d		. .
	jp BdosEntry		;1c80	c3 05 00	. . .
	ld e,a			;1c83	5f		_
	ld c,00eh		;1c84	0e 0e		. .
	jp BdosEntry		;1c86	c3 05 00	. . .
	call BdosEntry		;1c89	cd 05 00	. . .
	ld (0dbeeh),a		;1c8c	32 ee db	2 . .
	inc a			;1c8f	3c		<
	ret			;1c90	c9		.
	ld c,00fh		;1c91	0e 0f		. .
	jp 0d4c3h		;1c93	c3 c3 d4	. . .
	xor a			;1c96	af		.
	ld (0dbedh),a		;1c97	32 ed db	2 . .
	ld de,0dbcdh		;1c9a	11 cd db	. . .
	jp 0d4cbh		;1c9d	c3 cb d4	. . .
	ld c,010h		;1ca0	0e 10		. .
	jp 0d4c3h		;1ca2	c3 c3 d4	. . .
	ld c,011h		;1ca5	0e 11		. .
	jp 0d4c3h		;1ca7	c3 c3 d4	. . .
	ld c,012h		;1caa	0e 12		. .
	jp 0d4c3h		;1cac	c3 c3 d4	. . .
	ld de,0dbcdh		;1caf	11 cd db	. . .
	jp 0d4dfh		;1cb2	c3 df d4	. . .
	ld c,013h		;1cb5	0e 13		. .
	jp BdosEntry		;1cb7	c3 05 00	. . .
	call BdosEntry		;1cba	cd 05 00	. . .
	or a			;1cbd	b7		.
	ret			;1cbe	c9		.
	ld c,014h		;1cbf	0e 14		. .
	jp 0d4f4h		;1cc1	c3 f4 d4	. . .
	ld de,0dbcdh		;1cc4	11 cd db	. . .
	jp 0d4f9h		;1cc7	c3 f9 d4	. . .
	ld c,015h		;1cca	0e 15		. .
	jp 0d4f4h		;1ccc	c3 f4 d4	. . .
	ld c,016h		;1ccf	0e 16		. .
	jp 0d4c3h		;1cd1	c3 c3 d4	. . .
	ld c,017h		;1cd4	0e 17		. .
	jp BdosEntry		;1cd6	c3 05 00	. . .
	ld e,0ffh		;1cd9	1e ff		. .
	ld c,020h		;1cdb	0e 20		.  
	jp BdosEntry		;1cdd	c3 05 00	. . .
	call 0d513h		;1ce0	cd 13 d5	. . .
	add a,a			;1ce3	87		.
	add a,a			;1ce4	87		.
	add a,a			;1ce5	87		.
	add a,a			;1ce6	87		.
	ld hl,0dbefh		;1ce7	21 ef db	! . .
	or (hl)			;1cea	b6		.
	ld (l0004h),a		;1ceb	32 04 00	2 . .
	ret			;1cee	c9		.
	ld a,(0dbefh)		;1cef	3a ef db	: . .
	ld (l0004h),a		;1cf2	32 04 00	2 . .
	ret			;1cf5	c9		.
	cp 061h			;1cf6	fe 61		. a
	ret c			;1cf8	d8		.
	cp 07bh			;1cf9	fe 7b		. {
	ret nc			;1cfb	d0		.
	and 05fh		;1cfc	e6 5f		. _
	ret			;1cfe	c9		.
	ld a,(0dbabh)		;1cff	3a ab db	: . .
	or a			;1d02	b7		.
	jp z,0d596h		;1d03	ca 96 d5	. . .
	ld a,(0dbefh)		;1d06	3a ef db	: . .
	or a			;1d09	b7		.
	ld a,000h		;1d0a	3e 00		> .
	call nz,0d4bdh		;1d0c	c4 bd d4	. . .
	ld de,0dbach		;1d0f	11 ac db	. . .
	call 0d4cbh		;1d12	cd cb d4	. . .
	jp z,0d596h		;1d15	ca 96 d5	. . .
	ld a,(0dbbbh)		;1d18	3a bb db	: . .
	dec a			;1d1b	3d		=
	ld (0dbcch),a		;1d1c	32 cc db	2 . .
	ld de,0dbach		;1d1f	11 ac db	. . .
	call 0d4f9h		;1d22	cd f9 d4	. . .
	jp nz,0d596h		;1d25	c2 96 d5	. . .
	ld de,0d407h		;1d28	11 07 d4	. . .
	ld hl,l0080h		;1d2b	21 80 00	! . .
	ld b,080h		;1d2e	06 80		. .
	call 0d842h		;1d30	cd 42 d8	. B .
	ld hl,0dbbah		;1d33	21 ba db	! . .
	ld (hl),000h		;1d36	36 00		6 .
	inc hl			;1d38	23		#
	dec (hl)		;1d39	35		5
	ld de,0dbach		;1d3a	11 ac db	. . .
	call 0d4dah		;1d3d	cd da d4	. . .
	jp z,0d596h		;1d40	ca 96 d5	. . .
	ld a,(0dbefh)		;1d43	3a ef db	: . .
	or a			;1d46	b7		.
	call nz,0d4bdh		;1d47	c4 bd d4	. . .
	ld hl,0d408h		;1d4a	21 08 d4	! . .
	call 0d4ach		;1d4d	cd ac d4	. . .
	call 0d5c2h		;1d50	cd c2 d5	. . .
	jp z,0d5a7h		;1d53	ca a7 d5	. . .
	call 0d5ddh		;1d56	cd dd d5	. . .
	jp 0d782h		;1d59	c3 82 d7	. . .
	call 0d5ddh		;1d5c	cd dd d5	. . .
	call 0d51ah		;1d5f	cd 1a d5	. . .
	ld c,00ah		;1d62	0e 0a		. .
	ld de,0d406h		;1d64	11 06 d4	. . .
	call BdosEntry		;1d67	cd 05 00	. . .
	call 0d529h		;1d6a	cd 29 d5	. ) .
	ld hl,0d407h		;1d6d	21 07 d4	! . .
	ld b,(hl)		;1d70	46		F
	inc hl			;1d71	23		#
	ld a,b			;1d72	78		x
	or a			;1d73	b7		.
	jp z,0d5bah		;1d74	ca ba d5	. . .
	ld a,(hl)		;1d77	7e		~
	call 0d530h		;1d78	cd 30 d5	. 0 .
	ld (hl),a		;1d7b	77		w
	dec b			;1d7c	05		.
	jp 0d5abh		;1d7d	c3 ab d5	. . .
	ld (hl),a		;1d80	77		w
	ld hl,0d408h		;1d81	21 08 d4	! . .
	ld (0d488h),hl		;1d84	22 88 d4	" . .
	ret			;1d87	c9		.
	ld c,00bh		;1d88	0e 0b		. .
	call BdosEntry		;1d8a	cd 05 00	. . .
	or a			;1d8d	b7		.
	ret z			;1d8e	c8		.
	ld c,001h		;1d8f	0e 01		. .
	call BdosEntry		;1d91	cd 05 00	. . .
	or a			;1d94	b7		.
	ret			;1d95	c9		.
	ld c,019h		;1d96	0e 19		. .
	jp BdosEntry		;1d98	c3 05 00	. . .
	ld de,l0080h		;1d9b	11 80 00	. . .
	ld c,01ah		;1d9e	0e 1a		. .
	jp BdosEntry		;1da0	c3 05 00	. . .
	ld hl,0dbabh		;1da3	21 ab db	! . .
	ld a,(hl)		;1da6	7e		~
	or a			;1da7	b7		.
	ret z			;1da8	c8		.
	ld (hl),000h		;1da9	36 00		6 .
	xor a			;1dab	af		.
	call 0d4bdh		;1dac	cd bd d4	. . .
	ld de,0dbach		;1daf	11 ac db	. . .
	call 0d4efh		;1db2	cd ef d4	. . .
	ld a,(0dbefh)		;1db5	3a ef db	: . .
	jp 0d4bdh		;1db8	c3 bd d4	. . .
	ld de,0d728h		;1dbb	11 28 d7	. ( .
	ld hl,0dc00h		;1dbe	21 00 dc	! . .
	ld b,006h		;1dc1	06 06		. .
	ld a,(de)		;1dc3	1a		.
	cp (hl)			;1dc4	be		.
	jp nz,0d7cfh		;1dc5	c2 cf d7	. . .
	inc de			;1dc8	13		.
	inc hl			;1dc9	23		#
	dec b			;1dca	05		.
	jp nz,0d5fdh		;1dcb	c2 fd d5	. . .
	ret			;1dce	c9		.
	call 0d498h		;1dcf	cd 98 d4	. . .
	ld hl,(0d48ah)		;1dd2	2a 8a d4	* . .
	ld a,(hl)		;1dd5	7e		~
	cp 020h			;1dd6	fe 20		.  
	jp z,0d622h		;1dd8	ca 22 d6	. " .
	or a			;1ddb	b7		.
	jp z,0d622h		;1ddc	ca 22 d6	. " .
	push hl			;1ddf	e5		.
	call 0d48ch		;1de0	cd 8c d4	. . .
	pop hl			;1de3	e1		.
	inc hl			;1de4	23		#
	jp 0d60fh		;1de5	c3 0f d6	. . .
	ld a,03fh		;1de8	3e 3f		> ?
	call 0d48ch		;1dea	cd 8c d4	. . .
	call 0d498h		;1ded	cd 98 d4	. . .
	call 0d5ddh		;1df0	cd dd d5	. . .
	jp 0d782h		;1df3	c3 82 d7	. . .
	ld a,(de)		;1df6	1a		.
	or a			;1df7	b7		.
	ret z			;1df8	c8		.
	cp 020h			;1df9	fe 20		.  
	jp c,0d609h		;1dfb	da 09 d6	. . .
	ret z			;1dfe	c8		.
	cp 03dh			;1dff	fe 3d		. =
	ret z			;1e01	c8		.
	cp 05fh			;1e02	fe 5f		. _
	ret z			;1e04	c8		.
	cp 02eh			;1e05	fe 2e		. .
	ret z			;1e07	c8		.
	cp 03ah			;1e08	fe 3a		. :
	ret z			;1e0a	c8		.
	cp 03bh			;1e0b	fe 3b		. ;
	ret z			;1e0d	c8		.
	cp 03ch			;1e0e	fe 3c		. <
	ret z			;1e10	c8		.
	cp 03eh			;1e11	fe 3e		. >
	ret z			;1e13	c8		.
	ret			;1e14	c9		.
	ld a,(de)		;1e15	1a		.
	or a			;1e16	b7		.
	ret z			;1e17	c8		.
	cp 020h			;1e18	fe 20		.  
	ret nz			;1e1a	c0		.
	inc de			;1e1b	13		.
	jp 0d64fh		;1e1c	c3 4f d6	. O .
	add a,l			;1e1f	85		.
	ld l,a			;1e20	6f		o
	ret nc			;1e21	d0		.
	inc h			;1e22	24		$
	ret			;1e23	c9		.
	ld a,000h		;1e24	3e 00		> .
	ld hl,0dbcdh		;1e26	21 cd db	! . .
	call 0d659h		;1e29	cd 59 d6	. Y .
	push hl			;1e2c	e5		.
	push hl			;1e2d	e5		.
	xor a			;1e2e	af		.
	ld (0dbf0h),a		;1e2f	32 f0 db	2 . .
	ld hl,(0d488h)		;1e32	2a 88 d4	* . .
	ex de,hl		;1e35	eb		.
	call 0d64fh		;1e36	cd 4f d6	. O .
	ex de,hl		;1e39	eb		.
	ld (0d48ah),hl		;1e3a	22 8a d4	" . .
	ex de,hl		;1e3d	eb		.
	pop hl			;1e3e	e1		.
	ld a,(de)		;1e3f	1a		.
	or a			;1e40	b7		.
	jp z,0d689h		;1e41	ca 89 d6	. . .
	sbc a,040h		;1e44	de 40		. @
	ld b,a			;1e46	47		G
	inc de			;1e47	13		.
	ld a,(de)		;1e48	1a		.
	cp 03ah			;1e49	fe 3a		. :
	jp z,0d690h		;1e4b	ca 90 d6	. . .
	dec de			;1e4e	1b		.
	ld a,(0dbefh)		;1e4f	3a ef db	: . .
	ld (hl),a		;1e52	77		w
	jp 0d696h		;1e53	c3 96 d6	. . .
	ld a,b			;1e56	78		x
	ld (0dbf0h),a		;1e57	32 f0 db	2 . .
	ld (hl),b		;1e5a	70		p
	inc de			;1e5b	13		.
	ld b,008h		;1e5c	06 08		. .
	call 0d630h		;1e5e	cd 30 d6	. 0 .
	jp z,0d6b9h		;1e61	ca b9 d6	. . .
	inc hl			;1e64	23		#
	cp 02ah			;1e65	fe 2a		. *
	jp nz,0d6a9h		;1e67	c2 a9 d6	. . .
	ld (hl),03fh		;1e6a	36 3f		6 ?
	jp 0d6abh		;1e6c	c3 ab d6	. . .
	ld (hl),a		;1e6f	77		w
	inc de			;1e70	13		.
	dec b			;1e71	05		.
	jp nz,0d698h		;1e72	c2 98 d6	. . .
	call 0d630h		;1e75	cd 30 d6	. 0 .
	jp z,0d6c0h		;1e78	ca c0 d6	. . .
	inc de			;1e7b	13		.
	jp 0d6afh		;1e7c	c3 af d6	. . .
	inc hl			;1e7f	23		#
	ld (hl),020h		;1e80	36 20		6  
	dec b			;1e82	05		.
	jp nz,0d6b9h		;1e83	c2 b9 d6	. . .
	ld b,003h		;1e86	06 03		. .
	cp 02eh			;1e88	fe 2e		. .
	jp nz,0d6e9h		;1e8a	c2 e9 d6	. . .
	inc de			;1e8d	13		.
	call 0d630h		;1e8e	cd 30 d6	. 0 .
	jp z,0d6e9h		;1e91	ca e9 d6	. . .
	inc hl			;1e94	23		#
	cp 02ah			;1e95	fe 2a		. *
	jp nz,0d6d9h		;1e97	c2 d9 d6	. . .
	ld (hl),03fh		;1e9a	36 3f		6 ?
	jp 0d6dbh		;1e9c	c3 db d6	. . .
	ld (hl),a		;1e9f	77		w
	inc de			;1ea0	13		.
	dec b			;1ea1	05		.
	jp nz,0d6c8h		;1ea2	c2 c8 d6	. . .
	call 0d630h		;1ea5	cd 30 d6	. 0 .
	jp z,0d6f0h		;1ea8	ca f0 d6	. . .
	inc de			;1eab	13		.
	jp 0d6dfh		;1eac	c3 df d6	. . .
	inc hl			;1eaf	23		#
	ld (hl),020h		;1eb0	36 20		6  
	dec b			;1eb2	05		.
	jp nz,0d6e9h		;1eb3	c2 e9 d6	. . .
	ld b,003h		;1eb6	06 03		. .
	inc hl			;1eb8	23		#
	ld (hl),000h		;1eb9	36 00		6 .
	dec b			;1ebb	05		.
	jp nz,0d6f2h		;1ebc	c2 f2 d6	. . .
	ex de,hl		;1ebf	eb		.
	ld (0d488h),hl		;1ec0	22 88 d4	" . .
	pop hl			;1ec3	e1		.
	ld bc,ZeroPageRst10Pad_start	;1ec4	01 0b 00	. . .
	inc hl			;1ec7	23		#
	ld a,(hl)		;1ec8	7e		~
	cp 03fh			;1ec9	fe 3f		. ?
	jp nz,0d709h		;1ecb	c2 09 d7	. . .
	inc b			;1ece	04		.
	dec c			;1ecf	0d		.
	jp nz,0d701h		;1ed0	c2 01 d7	. . .
	ld a,b			;1ed3	78		x
	or a			;1ed4	b7		.
	ret			;1ed5	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpCommandKeywords - "DIR ERA TYPE SAVE REN USER" keyword
; table (high-bit terminated entries).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpCommandKeywords' (start 0x1ed6 end 0x1ee0)
CcpCommandKeywords_start:
	defb 044h		;1ed6	44		D
	defb 049h		;1ed7	49		I
	defb 052h		;1ed8	52		R
	defb 020h		;1ed9	20		 
	defb 045h		;1eda	45		E
	defb 052h		;1edb	52		R
	defb 041h		;1edc	41		A
	defb 020h		;1edd	20		 
	defb 054h		;1ede	54		T
	defb 059h		;1edf	59		Y
CcpCommandKeywords_end:
	ld d,b			;1ee0	50		P
	ld b,l			;1ee1	45		E
	ld d,e			;1ee2	53		S
	ld b,c			;1ee3	41		A
	ld d,(hl)		;1ee4	56		V
	ld b,l			;1ee5	45		E
	ld d,d			;1ee6	52		R
	ld b,l			;1ee7	45		E
	ld c,(hl)		;1ee8	4e		N
	jr nz,$+87		;1ee9	20 55		  U
	ld d,e			;1eeb	53		S
	ld b,l			;1eec	45		E
	ld d,d			;1eed	52		R
	ld sp,hl		;1eee	f9		.
	ld d,000h		;1eef	16 00		. .
	nop			;1ef1	00		.
	nop			;1ef2	00		.
	ld l,e			;1ef3	6b		k
	ld hl,0d710h		;1ef4	21 10 d7	! . .
	ld c,000h		;1ef7	0e 00		. .
	ld a,c			;1ef9	79		y
	cp 006h			;1efa	fe 06		. .
	ret nc			;1efc	d0		.
	ld de,0dbceh		;1efd	11 ce db	. . .
	ld b,004h		;1f00	06 04		. .
	ld a,(de)		;1f02	1a		.
	cp (hl)			;1f03	be		.
	jp nz,0d74fh		;1f04	c2 4f d7	. O .
	inc de			;1f07	13		.
	inc hl			;1f08	23		#
	dec b			;1f09	05		.
	jp nz,0d73ch		;1f0a	c2 3c d7	. < .
	ld a,(de)		;1f0d	1a		.
	cp 020h			;1f0e	fe 20		.  
	jp nz,0d754h		;1f10	c2 54 d7	. T .
	ld a,c			;1f13	79		y
	ret			;1f14	c9		.
	inc hl			;1f15	23		#
	dec b			;1f16	05		.
	jp nz,0d74fh		;1f17	c2 4f d7	. O .
	inc c			;1f1a	0c		.
	jp 0d733h		;1f1b	c3 33 d7	. 3 .
	xor a			;1f1e	af		.
	ld (0d407h),a		;1f1f	32 07 d4	2 . .
	ld sp,0dbabh		;1f22	31 ab db	1 . .
	push bc			;1f25	c5		.
	ld a,c			;1f26	79		y
	rra			;1f27	1f		.
	rra			;1f28	1f		.
	rra			;1f29	1f		.
	rra			;1f2a	1f		.
	and 00fh		;1f2b	e6 0f		. .
	ld e,a			;1f2d	5f		_
	call 0d515h		;1f2e	cd 15 d5	. . .
	call 0d4b8h		;1f31	cd b8 d4	. . .
	ld (0dbabh),a		;1f34	32 ab db	2 . .
	pop bc			;1f37	c1		.
	ld a,c			;1f38	79		y
	and 00fh		;1f39	e6 0f		. .
	ld (0dbefh),a		;1f3b	32 ef db	2 . .
	call 0d4bdh		;1f3e	cd bd d4	. . .
	ld a,(0d407h)		;1f41	3a 07 d4	: . .
	or a			;1f44	b7		.
	jp nz,0d798h		;1f45	c2 98 d7	. . .
	ld sp,0dbabh		;1f48	31 ab db	1 . .
	call 0d498h		;1f4b	cd 98 d4	. . .
	call 0d5d0h		;1f4e	cd d0 d5	. . .
	add a,041h		;1f51	c6 41		. A
	call 0d48ch		;1f53	cd 8c d4	. . .
	ld a,03eh		;1f56	3e 3e		> >
	call 0d48ch		;1f58	cd 8c d4	. . .
	call 0d539h		;1f5b	cd 39 d5	. 9 .
	ld de,l0080h		;1f5e	11 80 00	. . .
	call 0d5d8h		;1f61	cd d8 d5	. . .
	call 0d5d0h		;1f64	cd d0 d5	. . .
	ld (0dbefh),a		;1f67	32 ef db	2 . .
	call 0d65eh		;1f6a	cd 5e d6	. ^ .
	call nz,0d609h		;1f6d	c4 09 d6	. . .
	ld a,(0dbf0h)		;1f70	3a f0 db	: . .
	or a			;1f73	b7		.
	jp nz,0daa5h		;1f74	c2 a5 da	. . .
	call 0d72eh		;1f77	cd 2e d7	. . .
	ld hl,0d7c1h		;1f7a	21 c1 d7	! . .
	ld e,a			;1f7d	5f		_
	ld d,000h		;1f7e	16 00		. .
	add hl,de		;1f80	19		.
	add hl,de		;1f81	19		.
	ld a,(hl)		;1f82	7e		~
	inc hl			;1f83	23		#
	ld h,(hl)		;1f84	66		f
	ld l,a			;1f85	6f		o
	jp (hl)			;1f86	e9		.
	ld (hl),a		;1f87	77		w
	ret c			;1f88	d8		.
	rra			;1f89	1f		.
	exx			;1f8a	d9		.
	ld e,l			;1f8b	5d		]
	exx			;1f8c	d9		.
	xor l			;1f8d	ad		.
	exx			;1f8e	d9		.
	djnz $-36		;1f8f	10 da		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpTable1F91 - CCP lookup island.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpTable1F91' (start 0x1f91 end 0x1f95)
CcpTable1F91_start:
	defb 08eh		;1f91	8e		.
	defb 0dah		;1f92	da		.
	defb 0a5h		;1f93	a5		.
	defb 0dah		;1f94	da		.
CcpTable1F91_end:
	ld hl,076f3h		;1f95	21 f3 76	! . v
	ld (0d400h),hl		;1f98	22 00 d4	" . .
	ld hl,0d400h		;1f9b	21 00 d4	! . .
	jp (hl)			;1f9e	e9		.
	ld bc,0d7dfh		;1f9f	01 df d7	. . .
	jp 0d4a7h		;1fa2	c3 a7 d4	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpMsgReadError - "READ ERROR" message.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpMsgReadError' (start 0x1fa5 end 0x1fb0)
CcpMsgReadError_start:
	defb 052h		;1fa5	52		R
	defb 045h		;1fa6	45		E
	defb 041h		;1fa7	41		A
	defb 044h		;1fa8	44		D
	defb 020h		;1fa9	20		 
	defb 045h		;1faa	45		E
	defb 052h		;1fab	52		R
	defb 052h		;1fac	52		R
	defb 04fh		;1fad	4f		O
	defb 052h		;1fae	52		R
	defb 000h		;1faf	00		.
CcpMsgReadError_end:
	ld bc,0d7f0h		;1fb0	01 f0 d7	. . .
	jp 0d4a7h		;1fb3	c3 a7 d4	. . .
	ld c,(hl)		;1fb6	4e		N
	ld c,a			;1fb7	4f		O
	jr nz,l2000h		;1fb8	20 46		  F
	ld c,c			;1fba	49		I
	ld c,h			;1fbb	4c		L
	ld b,l			;1fbc	45		E
	nop			;1fbd	00		.
	call 0d65eh		;1fbe	cd 5e d6	. ^ .
	ld a,(0dbf0h)		;1fc1	3a f0 db	: . .
	or a			;1fc4	b7		.
	jp nz,0d609h		;1fc5	c2 09 d6	. . .
	ld hl,0dbceh		;1fc8	21 ce db	! . .
	ld bc,ZeroPageRst10Pad_start	;1fcb	01 0b 00	. . .
	ld a,(hl)		;1fce	7e		~
	cp 020h			;1fcf	fe 20		.  
	jp z,0d833h		;1fd1	ca 33 d8	. 3 .
	inc hl			;1fd4	23		#
	sub 030h		;1fd5	d6 30		. 0
	cp 00ah			;1fd7	fe 0a		. .
	jp nc,0d609h		;1fd9	d2 09 d6	. . .
	ld d,a			;1fdc	57		W
	ld a,b			;1fdd	78		x
	and 0e0h		;1fde	e6 e0		. .
	jp nz,0d609h		;1fe0	c2 09 d6	. . .
	ld a,b			;1fe3	78		x
	rlca			;1fe4	07		.
	rlca			;1fe5	07		.
	rlca			;1fe6	07		.
	add a,b			;1fe7	80		.
	jp c,0d609h		;1fe8	da 09 d6	. . .
	add a,b			;1feb	80		.
	jp c,0d609h		;1fec	da 09 d6	. . .
	add a,d			;1fef	82		.
	jp c,0d609h		;1ff0	da 09 d6	. . .
	ld b,a			;1ff3	47		G
	dec c			;1ff4	0d		.
	jp nz,0d808h		;1ff5	c2 08 d8	. . .
	ret			;1ff8	c9		.
	ld a,(hl)		;1ff9	7e		~
	cp 020h			;1ffa	fe 20		.  
	jp nz,0d609h		;1ffc	c2 09 d6	. . .
	inc hl			;1fff	23		#
l2000h:
	dec c			;2000	0d		.
	jp nz,0d833h		;2001	c2 33 d8	. 3 .
	ld a,b			;2004	78		x
	ret			;2005	c9		.
	ld b,003h		;2006	06 03		. .
	ld a,(hl)		;2008	7e		~
	ld (de),a		;2009	12		.
	inc hl			;200a	23		#
	inc de			;200b	13		.
	dec b			;200c	05		.
	jp nz,0d842h		;200d	c2 42 d8	. B .
	ret			;2010	c9		.
	ld hl,l0080h		;2011	21 80 00	! . .
	add a,c			;2014	81		.
	call 0d659h		;2015	cd 59 d6	. Y .
	ld a,(hl)		;2018	7e		~
	ret			;2019	c9		.
	xor a			;201a	af		.
	ld (0dbcdh),a		;201b	32 cd db	2 . .
	ld a,(0dbf0h)		;201e	3a f0 db	: . .
	or a			;2021	b7		.
	ret z			;2022	c8		.
	dec a			;2023	3d		=
	ld hl,0dbefh		;2024	21 ef db	! . .
	cp (hl)			;2027	be		.
	ret z			;2028	c8		.
	jp 0d4bdh		;2029	c3 bd d4	. . .
	ld a,(0dbf0h)		;202c	3a f0 db	: . .
	or a			;202f	b7		.
	ret z			;2030	c8		.
	dec a			;2031	3d		=
	ld hl,0dbefh		;2032	21 ef db	! . .
	cp (hl)			;2035	be		.
	ret z			;2036	c8		.
	ld a,(0dbefh)		;2037	3a ef db	: . .
	jp 0d4bdh		;203a	c3 bd d4	. . .
	call 0d65eh		;203d	cd 5e d6	. ^ .
	call 0d854h		;2040	cd 54 d8	. T .
	ld hl,0dbceh		;2043	21 ce db	! . .
	ld a,(hl)		;2046	7e		~
	cp 020h			;2047	fe 20		.  
	jp nz,0d88fh		;2049	c2 8f d8	. . .
	ld b,00bh		;204c	06 0b		. .
	ld (hl),03fh		;204e	36 3f		6 ?
	inc hl			;2050	23		#
	dec b			;2051	05		.
	jp nz,0d888h		;2052	c2 88 d8	. . .
	ld e,000h		;2055	1e 00		. .
	push de			;2057	d5		.
	call 0d4e9h		;2058	cd e9 d4	. . .
	call z,0d7eah		;205b	cc ea d7	. . .
	jp z,0d91bh		;205e	ca 1b d9	. . .
	ld a,(0dbeeh)		;2061	3a ee db	: . .
	rrca			;2064	0f		.
	rrca			;2065	0f		.
	rrca			;2066	0f		.
	and 060h		;2067	e6 60		. `
	ld c,a			;2069	4f		O
	ld a,00ah		;206a	3e 0a		> .
	call 0d84bh		;206c	cd 4b d8	. K .
	rla			;206f	17		.
	jp c,0d90fh		;2070	da 0f d9	. . .
	pop de			;2073	d1		.
	ld a,e			;2074	7b		{
	inc e			;2075	1c		.
	push de			;2076	d5		.
	and 003h		;2077	e6 03		. .
	push af			;2079	f5		.
	jp nz,0d8cch		;207a	c2 cc d8	. . .
	call 0d498h		;207d	cd 98 d4	. . .
	push bc			;2080	c5		.
	call 0d5d0h		;2081	cd d0 d5	. . .
	pop bc			;2084	c1		.
	add a,041h		;2085	c6 41		. A
	call 0d492h		;2087	cd 92 d4	. . .
	ld a,03ah		;208a	3e 3a		> :
	call 0d492h		;208c	cd 92 d4	. . .
	jp 0d8d4h		;208f	c3 d4 d8	. . .
	call 0d4a2h		;2092	cd a2 d4	. . .
	ld a,03ah		;2095	3e 3a		> :
	call 0d492h		;2097	cd 92 d4	. . .
	call 0d4a2h		;209a	cd a2 d4	. . .
	ld b,001h		;209d	06 01		. .
	ld a,b			;209f	78		x
	call 0d84bh		;20a0	cd 4b d8	. K .
	and 07fh		;20a3	e6 7f		. .
	cp 020h			;20a5	fe 20		.  
	jp nz,0d8f9h		;20a7	c2 f9 d8	. . .
	pop af			;20aa	f1		.
	push af			;20ab	f5		.
	cp 003h			;20ac	fe 03		. .
	jp nz,0d8f7h		;20ae	c2 f7 d8	. . .
	ld a,009h		;20b1	3e 09		> .
	call 0d84bh		;20b3	cd 4b d8	. K .
	and 07fh		;20b6	e6 7f		. .
	cp 020h			;20b8	fe 20		.  
	jp z,0d90eh		;20ba	ca 0e d9	. . .
	ld a,020h		;20bd	3e 20		>  
	call 0d492h		;20bf	cd 92 d4	. . .
	inc b			;20c2	04		.
	ld a,b			;20c3	78		x
	cp 00ch			;20c4	fe 0c		. .
	jp nc,0d90eh		;20c6	d2 0e d9	. . .
	cp 009h			;20c9	fe 09		. .
	jp nz,0d8d9h		;20cb	c2 d9 d8	. . .
	call 0d4a2h		;20ce	cd a2 d4	. . .
	jp 0d8d9h		;20d1	c3 d9 d8	. . .
	pop af			;20d4	f1		.
	call 0d5c2h		;20d5	cd c2 d5	. . .
	jp nz,0d91bh		;20d8	c2 1b d9	. . .
	call 0d4e4h		;20db	cd e4 d4	. . .
	jp 0d898h		;20de	c3 98 d8	. . .
	pop de			;20e1	d1		.
	jp 0db86h		;20e2	c3 86 db	. . .
	call 0d65eh		;20e5	cd 5e d6	. ^ .
	cp 00bh			;20e8	fe 0b		. .
	jp nz,0d942h		;20ea	c2 42 d9	. B .
	ld bc,0d952h		;20ed	01 52 d9	. R .
	call 0d4a7h		;20f0	cd a7 d4	. . .
	call 0d539h		;20f3	cd 39 d5	. 9 .
	ld hl,0d407h		;20f6	21 07 d4	! . .
	dec (hl)		;20f9	35		5
	jp nz,0d782h		;20fa	c2 82 d7	. . .
	inc hl			;20fd	23		#
	ld a,(hl)		;20fe	7e		~
	cp 059h			;20ff	fe 59		. Y
	jp nz,0d782h		;2101	c2 82 d7	. . .
	inc hl			;2104	23		#
	ld (0d488h),hl		;2105	22 88 d4	" . .
	call 0d854h		;2108	cd 54 d8	. T .
	ld de,0dbcdh		;210b	11 cd db	. . .
	call 0d4efh		;210e	cd ef d4	. . .
	inc a			;2111	3c		<
	call z,0d7eah		;2112	cc ea d7	. . .
	jp 0db86h		;2115	c3 86 db	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpMsgAllYn - "ALL (Y/N)?" confirmation prompt.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpMsgAllYn' (start 0x2118 end 0x2123)
CcpMsgAllYn_start:
	defb 041h		;2118	41		A
	defb 04ch		;2119	4c		L
	defb 04ch		;211a	4c		L
	defb 020h		;211b	20		 
	defb 028h		;211c	28		(
	defb 059h		;211d	59		Y
	defb 02fh		;211e	2f		/
	defb 04eh		;211f	4e		N
	defb 029h		;2120	29		)
	defb 03fh		;2121	3f		?
	defb 000h		;2122	00		.
CcpMsgAllYn_end:
	call 0d65eh		;2123	cd 5e d6	. ^ .
	jp nz,0d609h		;2126	c2 09 d6	. . .
	call 0d854h		;2129	cd 54 d8	. T .
	call 0d4d0h		;212c	cd d0 d4	. . .
	jp z,0d9a7h		;212f	ca a7 d9	. . .
	call 0d498h		;2132	cd 98 d4	. . .
	ld hl,0dbf1h		;2135	21 f1 db	! . .
	ld (hl),0ffh		;2138	36 ff		6 .
	ld hl,0dbf1h		;213a	21 f1 db	! . .
	ld a,(hl)		;213d	7e		~
	cp 080h			;213e	fe 80		. .
	jp c,0d987h		;2140	da 87 d9	. . .
	push hl			;2143	e5		.
	call 0d4feh		;2144	cd fe d4	. . .
	pop hl			;2147	e1		.
	jp nz,0d9a0h		;2148	c2 a0 d9	. . .
	xor a			;214b	af		.
	ld (hl),a		;214c	77		w
	inc (hl)		;214d	34		4
	ld hl,l0080h		;214e	21 80 00	! . .
	call 0d659h		;2151	cd 59 d6	. Y .
	ld a,(hl)		;2154	7e		~
	cp 01ah			;2155	fe 1a		. .
	jp z,0db86h		;2157	ca 86 db	. . .
	call 0d48ch		;215a	cd 8c d4	. . .
	call 0d5c2h		;215d	cd c2 d5	. . .
	jp nz,0db86h		;2160	c2 86 db	. . .
	jp 0d974h		;2163	c3 74 d9	. t .
	dec a			;2166	3d		=
	jp z,0db86h		;2167	ca 86 db	. . .
	call 0d7d9h		;216a	cd d9 d7	. . .
	call 0d866h		;216d	cd 66 d8	. f .
	jp 0d609h		;2170	c3 09 d6	. . .
	call 0d7f8h		;2173	cd f8 d7	. . .
	push af			;2176	f5		.
	call 0d65eh		;2177	cd 5e d6	. ^ .
	jp nz,0d609h		;217a	c2 09 d6	. . .
	call 0d854h		;217d	cd 54 d8	. T .
	ld de,0dbcdh		;2180	11 cd db	. . .
	push de			;2183	d5		.
	call 0d4efh		;2184	cd ef d4	. . .
	pop de			;2187	d1		.
	call 0d509h		;2188	cd 09 d5	. . .
	jp z,0d9fbh		;218b	ca fb d9	. . .
	xor a			;218e	af		.
	ld (0dbedh),a		;218f	32 ed db	2 . .
	pop af			;2192	f1		.
	ld l,a			;2193	6f		o
	ld h,000h		;2194	26 00		& .
	add hl,hl		;2196	29		)
	ld de,l00ffh+1		;2197	11 00 01	. . .
	ld a,h			;219a	7c		|
	or l			;219b	b5		.
	jp z,0d9f1h		;219c	ca f1 d9	. . .
	dec hl			;219f	2b		+
	push hl			;21a0	e5		.
	ld hl,l0080h		;21a1	21 80 00	! . .
	add hl,de		;21a4	19		.
	push hl			;21a5	e5		.
	call 0d5d8h		;21a6	cd d8 d5	. . .
	ld de,0dbcdh		;21a9	11 cd db	. . .
	call 0d504h		;21ac	cd 04 d5	. . .
	pop de			;21af	d1		.
	pop hl			;21b0	e1		.
	jp nz,0d9fbh		;21b1	c2 fb d9	. . .
	jp 0d9d4h		;21b4	c3 d4 d9	. . .
	ld de,0dbcdh		;21b7	11 cd db	. . .
	call 0d4dah		;21ba	cd da d4	. . .
	inc a			;21bd	3c		<
	jp nz,0da01h		;21be	c2 01 da	. . .
	ld bc,0da07h		;21c1	01 07 da	. . .
	call 0d4a7h		;21c4	cd a7 d4	. . .
	call 0d5d5h		;21c7	cd d5 d5	. . .
	jp 0db86h		;21ca	c3 86 db	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpMsgNoSpace - "NO SPAC" message fragment.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpMsgNoSpace' (start 0x21cd end 0x21d4)
CcpMsgNoSpace_start:
	defb 04eh		;21cd	4e		N
	defb 04fh		;21ce	4f		O
	defb 020h		;21cf	20		 
	defb 053h		;21d0	53		S
	defb 050h		;21d1	50		P
	defb 041h		;21d2	41		A
	defb 043h		;21d3	43		C
CcpMsgNoSpace_end:
	ld b,l			;21d4	45		E
	nop			;21d5	00		.
	call 0d65eh		;21d6	cd 5e d6	. ^ .
	jp nz,0d609h		;21d9	c2 09 d6	. . .
	ld a,(0dbf0h)		;21dc	3a f0 db	: . .
	push af			;21df	f5		.
	call 0d854h		;21e0	cd 54 d8	. T .
	call 0d4e9h		;21e3	cd e9 d4	. . .
	jp nz,0da79h		;21e6	c2 79 da	. y .
	ld hl,0dbcdh		;21e9	21 cd db	! . .
	ld de,0dbddh		;21ec	11 dd db	. . .
	ld b,010h		;21ef	06 10		. .
	call 0d842h		;21f1	cd 42 d8	. B .
	ld hl,(0d488h)		;21f4	2a 88 d4	* . .
	ex de,hl		;21f7	eb		.
	call 0d64fh		;21f8	cd 4f d6	. O .
	cp 03dh			;21fb	fe 3d		. =
	jp z,0da3fh		;21fd	ca 3f da	. ? .
	cp 05fh			;2200	fe 5f		. _
	jp nz,0da73h		;2202	c2 73 da	. s .
	ex de,hl		;2205	eb		.
	inc hl			;2206	23		#
	ld (0d488h),hl		;2207	22 88 d4	" . .
	call 0d65eh		;220a	cd 5e d6	. ^ .
	jp nz,0da73h		;220d	c2 73 da	. s .
	pop af			;2210	f1		.
	ld b,a			;2211	47		G
	ld hl,0dbf0h		;2212	21 f0 db	! . .
	ld a,(hl)		;2215	7e		~
	or a			;2216	b7		.
	jp z,0da59h		;2217	ca 59 da	. Y .
	cp b			;221a	b8		.
	ld (hl),b		;221b	70		p
	jp nz,0da73h		;221c	c2 73 da	. s .
	ld (hl),b		;221f	70		p
	xor a			;2220	af		.
	ld (0dbcdh),a		;2221	32 cd db	2 . .
	call 0d4e9h		;2224	cd e9 d4	. . .
	jp z,0da6dh		;2227	ca 6d da	. m .
	ld de,0dbcdh		;222a	11 cd db	. . .
	call 0d50eh		;222d	cd 0e d5	. . .
	jp 0db86h		;2230	c3 86 db	. . .
	call 0d7eah		;2233	cd ea d7	. . .
	jp 0db86h		;2236	c3 86 db	. . .
	call 0d866h		;2239	cd 66 d8	. f .
	jp 0d609h		;223c	c3 09 d6	. . .
	ld bc,0da82h		;223f	01 82 da	. . .
	call 0d4a7h		;2242	cd a7 d4	. . .
	jp 0db86h		;2245	c3 86 db	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpMsgFileExists - "FILE EXISTS" plus trailing bytes.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpMsgFileExists' (start 0x2248 end 0x2256)
CcpMsgFileExists_start:
	defb 046h		;2248	46		F
	defb 049h		;2249	49		I
	defb 04ch		;224a	4c		L
	defb 045h		;224b	45		E
	defb 020h		;224c	20		 
	defb 045h		;224d	45		E
	defb 058h		;224e	58		X
	defb 049h		;224f	49		I
	defb 053h		;2250	53		S
	defb 054h		;2251	54		T
	defb 053h		;2252	53		S
	defb 000h		;2253	00		.
	defb 0cdh		;2254	cd		.
	defb 0f8h		;2255	f8		.
CcpMsgFileExists_end:
	rst 10h			;2256	d7		.
	cp 010h			;2257	fe 10		. .
	jp nc,0d609h		;2259	d2 09 d6	. . .
	ld e,a			;225c	5f		_
	ld a,(0dbceh)		;225d	3a ce db	: . .
	cp 020h			;2260	fe 20		.  
	jp z,0d609h		;2262	ca 09 d6	. . .
	call 0d515h		;2265	cd 15 d5	. . .
	jp 0db89h		;2268	c3 89 db	. . .
	call 0d5f5h		;226b	cd f5 d5	. . .
	ld a,(0dbceh)		;226e	3a ce db	: . .
	cp 020h			;2271	fe 20		.  
	jp nz,0dac4h		;2273	c2 c4 da	. . .
	ld a,(0dbf0h)		;2276	3a f0 db	: . .
	or a			;2279	b7		.
	jp z,0db89h		;227a	ca 89 db	. . .
	dec a			;227d	3d		=
	ld (0dbefh),a		;227e	32 ef db	2 . .
	call 0d529h		;2281	cd 29 d5	. ) .
	call 0d4bdh		;2284	cd bd d4	. . .
	jp 0db89h		;2287	c3 89 db	. . .
	ld de,0dbd6h		;228a	11 d6 db	. . .
	ld a,(de)		;228d	1a		.
	cp 020h			;228e	fe 20		.  
	jp nz,0d609h		;2290	c2 09 d6	. . .
	push de			;2293	d5		.
	call 0d854h		;2294	cd 54 d8	. T .
	pop de			;2297	d1		.
	ld hl,0db83h		;2298	21 83 db	! . .
	call 0d840h		;229b	cd 40 d8	. @ .
	call 0d4d0h		;229e	cd d0 d4	. . .
	jp z,0db6bh		;22a1	ca 6b db	. k .
	ld hl,l00ffh+1		;22a4	21 00 01	! . .
	push hl			;22a7	e5		.
	ex de,hl		;22a8	eb		.
	call 0d5d8h		;22a9	cd d8 d5	. . .
	ld de,0dbcdh		;22ac	11 cd db	. . .
	call 0d4f9h		;22af	cd f9 d4	. . .
	jp nz,0db01h		;22b2	c2 01 db	. . .
	pop hl			;22b5	e1		.
	ld de,l0080h		;22b6	11 80 00	. . .
	add hl,de		;22b9	19		.
	ld de,0d400h		;22ba	11 00 d4	. . .
	ld a,l			;22bd	7d		}
	sub e			;22be	93		.
	ld a,h			;22bf	7c		|
	sbc a,d			;22c0	9a		.
	jp nc,0db71h		;22c1	d2 71 db	. q .
	jp 0dae1h		;22c4	c3 e1 da	. . .
	pop hl			;22c7	e1		.
	dec a			;22c8	3d		=
	jp nz,0db71h		;22c9	c2 71 db	. q .
	call 0d866h		;22cc	cd 66 d8	. f .
	call 0d65eh		;22cf	cd 5e d6	. ^ .
	ld hl,0dbf0h		;22d2	21 f0 db	! . .
	push hl			;22d5	e5		.
	ld a,(hl)		;22d6	7e		~
	ld (0dbcdh),a		;22d7	32 cd db	2 . .
	ld a,010h		;22da	3e 10		> .
	call 0d660h		;22dc	cd 60 d6	. ` .
	pop hl			;22df	e1		.
	ld a,(hl)		;22e0	7e		~
	ld (0dbddh),a		;22e1	32 dd db	2 . .
	xor a			;22e4	af		.
	ld (0dbedh),a		;22e5	32 ed db	2 . .
	ld de,l005ch		;22e8	11 5c 00	. \ .
	ld hl,0dbcdh		;22eb	21 cd db	! . .
	ld b,021h		;22ee	06 21		. !
	call 0d842h		;22f0	cd 42 d8	. B .
	ld hl,0d408h		;22f3	21 08 d4	! . .
	ld a,(hl)		;22f6	7e		~
	or a			;22f7	b7		.
	jp z,0db3eh		;22f8	ca 3e db	. > .
	cp 020h			;22fb	fe 20		.  
	jp z,0db3eh		;22fd	ca 3e db	. > .
	inc hl			;2300	23		#
sub_2301h:
	jp 0db30h		;2301	c3 30 db	. 0 .
	ld b,000h		;2304	06 00		. .
	ld de,l0080h+1		;2306	11 81 00	. . .
	ld a,(hl)		;2309	7e		~
	ld (de),a		;230a	12		.
	or a			;230b	b7		.
	jp z,0db4fh		;230c	ca 4f db	. O .
	inc b			;230f	04		.
	inc hl			;2310	23		#
	inc de			;2311	13		.
	jp 0db43h		;2312	c3 43 db	. C .
	ld a,b			;2315	78		x
	ld (l0080h),a		;2316	32 80 00	2 . .
	call 0d498h		;2319	cd 98 d4	. . .
	call 0d5d5h		;231c	cd d5 d5	. . .
	call 0d51ah		;231f	cd 1a d5	. . .
	call l00ffh+1		;2322	cd 00 01	. . .
	ld sp,0dbabh		;2325	31 ab db	1 . .
	call 0d529h		;2328	cd 29 d5	. ) .
	call 0d4bdh		;232b	cd bd d4	. . .
	jp 0d782h		;232e	c3 82 d7	. . .
	call 0d866h		;2331	cd 66 d8	. f .
	jp 0d609h		;2334	c3 09 d6	. . .
	ld bc,0db7ah		;2337	01 7a db	. z .
	call 0d4a7h		;233a	cd a7 d4	. . .
	jp 0db86h		;233d	c3 86 db	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CcpMsgBadLoad - "BAD LOAD" and the "COM" default extension.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CcpMsgBadLoad' (start 0x2340 end 0x234c)
CcpMsgBadLoad_start:
	defb 042h		;2340	42		B
	defb 041h		;2341	41		A
	defb 044h		;2342	44		D
	defb 020h		;2343	20		 
	defb 04ch		;2344	4c		L
	defb 04fh		;2345	4f		O
	defb 041h		;2346	41		A
	defb 044h		;2347	44		D
	defb 000h		;2348	00		.
	defb 043h		;2349	43		C
	defb 04fh		;234a	4f		O
	defb 04dh		;234b	4d		M
CcpMsgBadLoad_end:
	call 0d866h		;234c	cd 66 d8	. f .
	call 0d65eh		;234f	cd 5e d6	. ^ .
	ld a,(0dbceh)		;2352	3a ce db	: . .
	sub 020h		;2355	d6 20		.  
	ld hl,0dbf0h		;2357	21 f0 db	! . .
	or (hl)			;235a	b6		.
	jp nz,0d609h		;235b	c2 09 d6	. . .
	jp 0d782h		;235e	c3 82 d7	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BdosPad2361 - zero padding ahead of the BDOS error strings.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'BdosPad2361' (start 0x2361 end 0x2373)
BdosPad2361_start:
	defb 000h		;2361	00		.
	defb 000h		;2362	00		.
	defb 000h		;2363	00		.
	defb 000h		;2364	00		.
	defb 000h		;2365	00		.
	defb 000h		;2366	00		.
	defb 000h		;2367	00		.
	defb 000h		;2368	00		.
	defb 000h		;2369	00		.
	defb 000h		;236a	00		.
	defb 000h		;236b	00		.
	defb 000h		;236c	00		.
	defb 000h		;236d	00		.
	defb 000h		;236e	00		.
	defb 000h		;236f	00		.
	defb 000h		;2370	00		.
	defb 000h		;2371	00		.
	defb 000h		;2372	00		.
BdosPad2361_end:
	inc h			;2373	24		$
	inc h			;2374	24		$
	inc h			;2375	24		$
	jr nz,l2398h		;2376	20 20		   
	jr nz,l239ah		;2378	20 20		   
	jr nz,l23cfh		;237a	20 53		  S
	ld d,l			;237c	55		U
	ld b,d			;237d	42		B
	nop			;237e	00		.
	nop			;237f	00		.
	nop			;2380	00		.
	nop			;2381	00		.
	nop			;2382	00		.
	nop			;2383	00		.
	nop			;2384	00		.
	nop			;2385	00		.
	nop			;2386	00		.
	nop			;2387	00		.
	nop			;2388	00		.
	nop			;2389	00		.
	nop			;238a	00		.
	nop			;238b	00		.
	nop			;238c	00		.
	nop			;238d	00		.
	nop			;238e	00		.
	nop			;238f	00		.
	nop			;2390	00		.
	nop			;2391	00		.
	nop			;2392	00		.
	nop			;2393	00		.
	nop			;2394	00		.
	nop			;2395	00		.
	nop			;2396	00		.
	nop			;2397	00		.
l2398h:
	nop			;2398	00		.
	nop			;2399	00		.
l239ah:
	nop			;239a	00		.
	nop			;239b	00		.
	nop			;239c	00		.
	nop			;239d	00		.
	nop			;239e	00		.
	nop			;239f	00		.
	nop			;23a0	00		.
	nop			;23a1	00		.
	nop			;23a2	00		.
	nop			;23a3	00		.
	nop			;23a4	00		.
	nop			;23a5	00		.
	nop			;23a6	00		.
	nop			;23a7	00		.
	nop			;23a8	00		.
	nop			;23a9	00		.
	nop			;23aa	00		.
	nop			;23ab	00		.
	nop			;23ac	00		.
	nop			;23ad	00		.
	nop			;23ae	00		.
	nop			;23af	00		.
	nop			;23b0	00		.
	nop			;23b1	00		.
	nop			;23b2	00		.
	nop			;23b3	00		.
	nop			;23b4	00		.
	nop			;23b5	00		.
	nop			;23b6	00		.
	nop			;23b7	00		.
	nop			;23b8	00		.
	nop			;23b9	00		.
	nop			;23ba	00		.
	nop			;23bb	00		.
	nop			;23bc	00		.
	nop			;23bd	00		.
	nop			;23be	00		.
	nop			;23bf	00		.
	nop			;23c0	00		.
	nop			;23c1	00		.
	nop			;23c2	00		.
	nop			;23c3	00		.
	nop			;23c4	00		.
	nop			;23c5	00		.
	ld sp,hl		;23c6	f9		.
	ld d,000h		;23c7	16 00		. .
	nop			;23c9	00		.
	nop			;23ca	00		.
	ld l,e			;23cb	6b		k
	jp 0dc11h		;23cc	c3 11 dc	. . .
l23cfh:
	sbc a,c			;23cf	99		.
	call c,0dca5h		;23d0	dc a5 dc	. . .
	xor e			;23d3	ab		.
	call c,0dcb1h		;23d4	dc b1 dc	. . .
	ex de,hl		;23d7	eb		.
	ld (0df43h),hl		;23d8	22 43 df	" C .
	ex de,hl		;23db	eb		.
	ld a,e			;23dc	7b		{
	ld (0e9d6h),a		;23dd	32 d6 e9	2 . .
	ld hl,WbootStub		;23e0	21 00 00	! . .
	ld (0df45h),hl		;23e3	22 45 df	" E .
	add hl,sp		;23e6	39		9
	ld (0df0fh),hl		;23e7	22 0f df	" . .
	ld sp,0df41h		;23ea	31 41 df	1 A .
	xor a			;23ed	af		.
	ld (0e9e0h),a		;23ee	32 e0 e9	2 . .
	ld (0e9deh),a		;23f1	32 de e9	2 . .
	ld hl,0e974h		;23f4	21 74 e9	! t .
	push hl			;23f7	e5		.
	ld a,c			;23f8	79		y
	cp 029h			;23f9	fe 29		. )
	ret nc			;23fb	d0		.
	ld c,e			;23fc	4b		K
	ld hl,0dc47h		;23fd	21 47 dc	! G .
	ld e,a			;2400	5f		_
	ld d,000h		;2401	16 00		. .
	add hl,de		;2403	19		.
	add hl,de		;2404	19		.
	ld e,(hl)		;2405	5e		^
	inc hl			;2406	23		#
	ld d,(hl)		;2407	56		V
	ld hl,(0df43h)		;2408	2a 43 df	* C .
	ex de,hl		;240b	eb		.
	jp (hl)			;240c	e9		.
	inc bc			;240d	03		.
	jp pe,0dec8h		;240e	ea c8 de	. . .
	sub b			;2411	90		.
	defb 0ddh,0ceh,0deh ;illegal sequence	;2412	dd ce de	. . .
	ld (de),a		;2415	12		.
	jp pe,0ea0fh		;2416	ea 0f ea	. . .
	call nc,0eddeh		;2419	d4 de ed	. . .
	sbc a,0f3h		;241c	de f3		. .
	sbc a,0f8h		;241e	de f8		. .
	sbc a,0e1h		;2420	de e1		. .
	defb 0ddh,0feh,0deh ;illegal sequence	;2422	dd fe de	. . .
	ld a,(hl)		;2425	7e		~
	ret pe			;2426	e8		.
	add a,e			;2427	83		.
	ret pe			;2428	e8		.
	ld b,l			;2429	45		E
	ret pe			;242a	e8		.
	sbc a,h			;242b	9c		.
	ret pe			;242c	e8		.
	and l			;242d	a5		.
	ret pe			;242e	e8		.
	xor e			;242f	ab		.
	ret pe			;2430	e8		.
	ret z			;2431	c8		.
	ret pe			;2432	e8		.
	rst 10h			;2433	d7		.
	ret pe			;2434	e8		.
	ret po			;2435	e0		.
	ret pe			;2436	e8		.
	and 0e8h		;2437	e6 e8		. .
	call pe,0f5e8h		;2439	ec e8 f5	. . .
	ret pe			;243c	e8		.
	cp 0e8h			;243d	fe e8		. .
	inc b			;243f	04		.
	jp (hl)			;2440	e9		.
	ld a,(bc)		;2441	0a		.
	jp (hl)			;2442	e9		.
	ld de,l2ce9h		;2443	11 e9 2c	. . ,
	pop hl			;2446	e1		.
	rla			;2447	17		.
	jp (hl)			;2448	e9		.
	dec e			;2449	1d		.
	jp (hl)			;244a	e9		.
	ld h,0e9h		;244b	26 e9		& .
	dec l			;244d	2d		-
	jp (hl)			;244e	e9		.
	ld b,c			;244f	41		A
	jp (hl)			;2450	e9		.
	ld b,a			;2451	47		G
	jp (hl)			;2452	e9		.
	ld c,l			;2453	4d		M
	jp (hl)			;2454	e9		.
	ld c,0e8h		;2455	0e e8		. .
	ld d,e			;2457	53		S
	jp (hl)			;2458	e9		.
	inc b			;2459	04		.
	rst 18h			;245a	df		.
	inc b			;245b	04		.
	rst 18h			;245c	df		.
	sbc a,e			;245d	9b		.
	jp (hl)			;245e	e9		.
	ld hl,0dccah		;245f	21 ca dc	! . .
	call 0dce5h		;2462	cd e5 dc	. . .
	cp 003h			;2465	fe 03		. .
	jp z,WbootStub		;2467	ca 00 00	. . .
	ret			;246a	c9		.
	ld hl,0dcd5h		;246b	21 d5 dc	! . .
	jp 0dcb4h		;246e	c3 b4 dc	. . .
	ld hl,0dce1h		;2471	21 e1 dc	! . .
	jp 0dcb4h		;2474	c3 b4 dc	. . .
	ld hl,0dcdch		;2477	21 dc dc	! . .
	call 0dce5h		;247a	cd e5 dc	. . .
	defb 0c3h		;247d	c3		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BdosErrMessages - "Bdos Err On : $", "Bad Sector$", "Select$",
; "File R/O$" (classic CP/M 2.2).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'BdosErrMessages' (start 0x247e end 0x24ab)
BdosErrMessages_start:
	defb 000h		;247e	00		.
	defb 000h		;247f	00		.
	defb 042h		;2480	42		B
	defb 064h		;2481	64		d
	defb 06fh		;2482	6f		o
	defb 073h		;2483	73		s
	defb 020h		;2484	20		 
	defb 045h		;2485	45		E
	defb 072h		;2486	72		r
	defb 072h		;2487	72		r
	defb 020h		;2488	20		 
	defb 04fh		;2489	4f		O
	defb 06eh		;248a	6e		n
	defb 020h		;248b	20		 
	defb 020h		;248c	20		 
	defb 03ah		;248d	3a		:
	defb 020h		;248e	20		 
	defb 024h		;248f	24		$
	defb 042h		;2490	42		B
	defb 061h		;2491	61		a
	defb 064h		;2492	64		d
	defb 020h		;2493	20		 
	defb 053h		;2494	53		S
	defb 065h		;2495	65		e
	defb 063h		;2496	63		c
	defb 074h		;2497	74		t
	defb 06fh		;2498	6f		o
	defb 072h		;2499	72		r
	defb 024h		;249a	24		$
	defb 053h		;249b	53		S
	defb 065h		;249c	65		e
	defb 06ch		;249d	6c		l
	defb 065h		;249e	65		e
	defb 063h		;249f	63		c
	defb 074h		;24a0	74		t
	defb 024h		;24a1	24		$
	defb 046h		;24a2	46		F
	defb 069h		;24a3	69		i
	defb 06ch		;24a4	6c		l
	defb 065h		;24a5	65		e
	defb 020h		;24a6	20		 
	defb 052h		;24a7	52		R
	defb 02fh		;24a8	2f		/
	defb 04fh		;24a9	4f		O
	defb 024h		;24aa	24		$
BdosErrMessages_end:
	push hl			;24ab	e5		.
	call 0ddc9h		;24ac	cd c9 dd	. . .
	ld a,(0df42h)		;24af	3a 42 df	: B .
	add a,041h		;24b2	c6 41		. A
	ld (0dcc6h),a		;24b4	32 c6 dc	2 . .
	ld bc,0dcbah		;24b7	01 ba dc	. . .
	call 0ddd3h		;24ba	cd d3 dd	. . .
	pop bc			;24bd	c1		.
	call 0ddd3h		;24be	cd d3 dd	. . .
	ld hl,0df0eh		;24c1	21 0e df	! . .
	ld a,(hl)		;24c4	7e		~
	ld (hl),000h		;24c5	36 00		6 .
	or a			;24c7	b7		.
	ret nz			;24c8	c0		.
	jp 0ea09h		;24c9	c3 09 ea	. . .
	call 0dcfbh		;24cc	cd fb dc	. . .
	call 0dd14h		;24cf	cd 14 dd	. . .
	ret c			;24d2	d8		.
	push af			;24d3	f5		.
	ld c,a			;24d4	4f		O
	call 0dd90h		;24d5	cd 90 dd	. . .
	pop af			;24d8	f1		.
	ret			;24d9	c9		.
	cp 00dh			;24da	fe 0d		. .
	ret z			;24dc	c8		.
	cp 00ah			;24dd	fe 0a		. .
	ret z			;24df	c8		.
	cp 009h			;24e0	fe 09		. .
	ret z			;24e2	c8		.
	cp 008h			;24e3	fe 08		. .
	ret z			;24e5	c8		.
	cp 020h			;24e6	fe 20		.  
	ret			;24e8	c9		.
	ld a,(0df0eh)		;24e9	3a 0e df	: . .
	or a			;24ec	b7		.
	jp nz,0dd45h		;24ed	c2 45 dd	. E .
	call 0ea06h		;24f0	cd 06 ea	. . .
	and 001h		;24f3	e6 01		. .
	ret z			;24f5	c8		.
	call 0ea09h		;24f6	cd 09 ea	. . .
	cp 013h			;24f9	fe 13		. .
	jp nz,0dd42h		;24fb	c2 42 dd	. B .
	call 0ea09h		;24fe	cd 09 ea	. . .
	cp 003h			;2501	fe 03		. .
	jp z,WbootStub		;2503	ca 00 00	. . .
	xor a			;2506	af		.
	ret			;2507	c9		.
	ld (0df0eh),a		;2508	32 0e df	2 . .
	ld a,001h		;250b	3e 01		> .
	ret			;250d	c9		.
	ld a,(0df0ah)		;250e	3a 0a df	: . .
	or a			;2511	b7		.
	jp nz,0dd62h		;2512	c2 62 dd	. b .
	push bc			;2515	c5		.
	call 0dd23h		;2516	cd 23 dd	. # .
	pop bc			;2519	c1		.
	push bc			;251a	c5		.
	call 0ea0ch		;251b	cd 0c ea	. . .
	pop bc			;251e	c1		.
	push bc			;251f	c5		.
	ld a,(0df0dh)		;2520	3a 0d df	: . .
	or a			;2523	b7		.
	call nz,0ea0fh		;2524	c4 0f ea	. . .
	pop bc			;2527	c1		.
	ld a,c			;2528	79		y
	ld hl,0df0ch		;2529	21 0c df	! . .
	cp 07fh			;252c	fe 7f		. .
	ret z			;252e	c8		.
	inc (hl)		;252f	34		4
	cp 020h			;2530	fe 20		.  
	ret nc			;2532	d0		.
	dec (hl)		;2533	35		5
	ld a,(hl)		;2534	7e		~
	or a			;2535	b7		.
	ret z			;2536	c8		.
	ld a,c			;2537	79		y
	cp 008h			;2538	fe 08		. .
	jp nz,0dd79h		;253a	c2 79 dd	. y .
	dec (hl)		;253d	35		5
	ret			;253e	c9		.
	cp 00ah			;253f	fe 0a		. .
	ret nz			;2541	c0		.
	ld (hl),000h		;2542	36 00		6 .
	ret			;2544	c9		.
	ld a,c			;2545	79		y
	call 0dd14h		;2546	cd 14 dd	. . .
	jp nc,0dd90h		;2549	d2 90 dd	. . .
	push af			;254c	f5		.
	ld c,05eh		;254d	0e 5e		. ^
	call 0dd48h		;254f	cd 48 dd	. H .
	pop af			;2552	f1		.
	or 040h			;2553	f6 40		. @
	ld c,a			;2555	4f		O
	ld a,c			;2556	79		y
	cp 009h			;2557	fe 09		. .
	jp nz,0dd48h		;2559	c2 48 dd	. H .
	ld c,020h		;255c	0e 20		.  
	call 0dd48h		;255e	cd 48 dd	. H .
	ld a,(0df0ch)		;2561	3a 0c df	: . .
	and 007h		;2564	e6 07		. .
	jp nz,0dd96h		;2566	c2 96 dd	. . .
	ret			;2569	c9		.
	call 0ddach		;256a	cd ac dd	. . .
	ld c,020h		;256d	0e 20		.  
	call 0ea0ch		;256f	cd 0c ea	. . .
	ld c,008h		;2572	0e 08		. .
	jp 0ea0ch		;2574	c3 0c ea	. . .
	ld c,023h		;2577	0e 23		. #
	call 0dd48h		;2579	cd 48 dd	. H .
	call 0ddc9h		;257c	cd c9 dd	. . .
	ld a,(0df0ch)		;257f	3a 0c df	: . .
	ld hl,0df0bh		;2582	21 0b df	! . .
	cp (hl)			;2585	be		.
	ret nc			;2586	d0		.
	ld c,020h		;2587	0e 20		.  
	call 0dd48h		;2589	cd 48 dd	. H .
	jp 0ddb9h		;258c	c3 b9 dd	. . .
	ld c,00dh		;258f	0e 0d		. .
	call 0dd48h		;2591	cd 48 dd	. H .
	ld c,00ah		;2594	0e 0a		. .
	jp 0dd48h		;2596	c3 48 dd	. H .
	ld a,(bc)		;2599	0a		.
	cp 024h			;259a	fe 24		. $
	ret z			;259c	c8		.
	inc bc			;259d	03		.
	push bc			;259e	c5		.
	ld c,a			;259f	4f		O
	call 0dd90h		;25a0	cd 90 dd	. . .
	pop bc			;25a3	c1		.
	jp 0ddd3h		;25a4	c3 d3 dd	. . .
	ld a,(0df0ch)		;25a7	3a 0c df	: . .
	ld (0df0bh),a		;25aa	32 0b df	2 . .
	ld hl,(0df43h)		;25ad	2a 43 df	* C .
	ld c,(hl)		;25b0	4e		N
	inc hl			;25b1	23		#
	push hl			;25b2	e5		.
	ld b,000h		;25b3	06 00		. .
	push bc			;25b5	c5		.
	push hl			;25b6	e5		.
	call 0dcfbh		;25b7	cd fb dc	. . .
	and 07fh		;25ba	e6 7f		. .
	pop hl			;25bc	e1		.
	pop bc			;25bd	c1		.
	cp 00dh			;25be	fe 0d		. .
	jp z,0dec1h		;25c0	ca c1 de	. . .
	cp 00ah			;25c3	fe 0a		. .
	jp z,0dec1h		;25c5	ca c1 de	. . .
	cp 008h			;25c8	fe 08		. .
	jp nz,0de16h		;25ca	c2 16 de	. . .
	ld a,b			;25cd	78		x
	or a			;25ce	b7		.
	jp z,0ddefh		;25cf	ca ef dd	. . .
	dec b			;25d2	05		.
	ld a,(0df0ch)		;25d3	3a 0c df	: . .
	ld (0df0ah),a		;25d6	32 0a df	2 . .
	jp 0de70h		;25d9	c3 70 de	. p .
	cp 07fh			;25dc	fe 7f		. .
	jp nz,0de26h		;25de	c2 26 de	. & .
	ld a,b			;25e1	78		x
	or a			;25e2	b7		.
	jp z,0ddefh		;25e3	ca ef dd	. . .
	ld a,(hl)		;25e6	7e		~
	dec b			;25e7	05		.
	dec hl			;25e8	2b		+
	jp 0dea9h		;25e9	c3 a9 de	. . .
	cp 005h			;25ec	fe 05		. .
	jp nz,0de37h		;25ee	c2 37 de	. 7 .
	push bc			;25f1	c5		.
	push hl			;25f2	e5		.
	call 0ddc9h		;25f3	cd c9 dd	. . .
	xor a			;25f6	af		.
	ld (0df0bh),a		;25f7	32 0b df	2 . .
	jp 0ddf1h		;25fa	c3 f1 dd	. . .
	cp 010h			;25fd	fe 10		. .
	jp nz,0de48h		;25ff	c2 48 de	. H .
	push hl			;2602	e5		.
	ld hl,0df0dh		;2603	21 0d df	! . .
	ld a,001h		;2606	3e 01		> .
	sub (hl)		;2608	96		.
	ld (hl),a		;2609	77		w
	pop hl			;260a	e1		.
	jp 0ddefh		;260b	c3 ef dd	. . .
	cp 018h			;260e	fe 18		. .
	jp nz,0de5fh		;2610	c2 5f de	. _ .
	pop hl			;2613	e1		.
	ld a,(0df0bh)		;2614	3a 0b df	: . .
	ld hl,0df0ch		;2617	21 0c df	! . .
	cp (hl)			;261a	be		.
	jp nc,0dde1h		;261b	d2 e1 dd	. . .
	dec (hl)		;261e	35		5
	call 0dda4h		;261f	cd a4 dd	. . .
	jp 0de4eh		;2622	c3 4e de	. N .
	cp 015h			;2625	fe 15		. .
	jp nz,0de6bh		;2627	c2 6b de	. k .
	call 0ddb1h		;262a	cd b1 dd	. . .
	pop hl			;262d	e1		.
	jp 0dde1h		;262e	c3 e1 dd	. . .
	cp 012h			;2631	fe 12		. .
	jp nz,0dea6h		;2633	c2 a6 de	. . .
	push bc			;2636	c5		.
	call 0ddb1h		;2637	cd b1 dd	. . .
	pop bc			;263a	c1		.
	pop hl			;263b	e1		.
	push hl			;263c	e5		.
	push bc			;263d	c5		.
	ld a,b			;263e	78		x
	or a			;263f	b7		.
	jp z,0de8ah		;2640	ca 8a de	. . .
	inc hl			;2643	23		#
	ld c,(hl)		;2644	4e		N
	dec b			;2645	05		.
	push bc			;2646	c5		.
	push hl			;2647	e5		.
	call 0dd7fh		;2648	cd 7f dd	. . .
	pop hl			;264b	e1		.
	pop bc			;264c	c1		.
	jp 0de78h		;264d	c3 78 de	. x .
	push hl			;2650	e5		.
	ld a,(0df0ah)		;2651	3a 0a df	: . .
	or a			;2654	b7		.
	jp z,0ddf1h		;2655	ca f1 dd	. . .
	ld hl,0df0ch		;2658	21 0c df	! . .
	sub (hl)		;265b	96		.
	ld (0df0ah),a		;265c	32 0a df	2 . .
	call 0dda4h		;265f	cd a4 dd	. . .
	ld hl,0df0ah		;2662	21 0a df	! . .
	dec (hl)		;2665	35		5
	jp nz,0de99h		;2666	c2 99 de	. . .
	jp 0ddf1h		;2669	c3 f1 dd	. . .
	inc hl			;266c	23		#
	ld (hl),a		;266d	77		w
	inc b			;266e	04		.
	push bc			;266f	c5		.
	push hl			;2670	e5		.
	ld c,a			;2671	4f		O
	call 0dd7fh		;2672	cd 7f dd	. . .
	pop hl			;2675	e1		.
	pop bc			;2676	c1		.
	ld a,(hl)		;2677	7e		~
	cp 003h			;2678	fe 03		. .
	ld a,b			;267a	78		x
	jp nz,0debdh		;267b	c2 bd de	. . .
	cp 001h			;267e	fe 01		. .
	jp z,WbootStub		;2680	ca 00 00	. . .
	cp c			;2683	b9		.
	jp c,0ddefh		;2684	da ef dd	. . .
	pop hl			;2687	e1		.
	ld (hl),b		;2688	70		p
	ld c,00dh		;2689	0e 0d		. .
	jp 0dd48h		;268b	c3 48 dd	. H .
	call 0dd06h		;268e	cd 06 dd	. . .
	jp 0df01h		;2691	c3 01 df	. . .
	call 0ea15h		;2694	cd 15 ea	. . .
	jp 0df01h		;2697	c3 01 df	. . .
	ld a,c			;269a	79		y
	inc a			;269b	3c		<
	jp z,0dee0h		;269c	ca e0 de	. . .
	inc a			;269f	3c		<
	jp z,0ea06h		;26a0	ca 06 ea	. . .
	jp 0ea0ch		;26a3	c3 0c ea	. . .
	call 0ea06h		;26a6	cd 06 ea	. . .
	or a			;26a9	b7		.
	jp z,0e991h		;26aa	ca 91 e9	. . .
	call 0ea09h		;26ad	cd 09 ea	. . .
	jp 0df01h		;26b0	c3 01 df	. . .
	ld a,(l0001h+2)		;26b3	3a 03 00	: . .
	jp 0df01h		;26b6	c3 01 df	. . .
	ld hl,l0001h+2		;26b9	21 03 00	! . .
	ld (hl),c		;26bc	71		q
	ret			;26bd	c9		.
	ex de,hl		;26be	eb		.
	ld c,l			;26bf	4d		M
	ld b,h			;26c0	44		D
	jp 0ddd3h		;26c1	c3 d3 dd	. . .
	call 0dd23h		;26c4	cd 23 dd	. # .
	ld (0df45h),a		;26c7	32 45 df	2 E .
	ret			;26ca	c9		.
	ld a,001h		;26cb	3e 01		> .
	jp 0df01h		;26cd	c3 01 df	. . .
	nop			;26d0	00		.
	nop			;26d1	00		.
	nop			;26d2	00		.
	nop			;26d3	00		.
	nop			;26d4	00		.
	nop			;26d5	00		.
	nop			;26d6	00		.
	nop			;26d7	00		.
	nop			;26d8	00		.
	nop			;26d9	00		.
	nop			;26da	00		.
	nop			;26db	00		.
	nop			;26dc	00		.
	nop			;26dd	00		.
	nop			;26de	00		.
	nop			;26df	00		.
	nop			;26e0	00		.
	nop			;26e1	00		.
	nop			;26e2	00		.
	nop			;26e3	00		.
	nop			;26e4	00		.
	nop			;26e5	00		.
	nop			;26e6	00		.
	nop			;26e7	00		.
	nop			;26e8	00		.
	nop			;26e9	00		.
	nop			;26ea	00		.
	nop			;26eb	00		.
	nop			;26ec	00		.
	nop			;26ed	00		.
	nop			;26ee	00		.
	nop			;26ef	00		.
	nop			;26f0	00		.
	nop			;26f1	00		.
	nop			;26f2	00		.
	nop			;26f3	00		.
	nop			;26f4	00		.
	nop			;26f5	00		.
	nop			;26f6	00		.
	nop			;26f7	00		.
	nop			;26f8	00		.
	nop			;26f9	00		.
	nop			;26fa	00		.
	nop			;26fb	00		.
	nop			;26fc	00		.
	nop			;26fd	00		.
	nop			;26fe	00		.
	nop			;26ff	00		.
	nop			;2700	00		.
	nop			;2701	00		.
	nop			;2702	00		.
	nop			;2703	00		.
	nop			;2704	00		.
	nop			;2705	00		.
	nop			;2706	00		.
	nop			;2707	00		.
	nop			;2708	00		.
	nop			;2709	00		.
	nop			;270a	00		.
	nop			;270b	00		.
	nop			;270c	00		.
	ld hl,0dc0bh		;270d	21 0b dc	! . .
	ld e,(hl)		;2710	5e		^
	inc hl			;2711	23		#
	ld d,(hl)		;2712	56		V
	ex de,hl		;2713	eb		.
	jp (hl)			;2714	e9		.
	inc c			;2715	0c		.
	dec c			;2716	0d		.
	ret z			;2717	c8		.
	ld a,(de)		;2718	1a		.
	ld (hl),a		;2719	77		w
	inc de			;271a	13		.
	inc hl			;271b	23		#
	jp 0df50h		;271c	c3 50 df	. P .
	ld a,(0df42h)		;271f	3a 42 df	: B .
	ld c,a			;2722	4f		O
	call 0ea1bh		;2723	cd 1b ea	. . .
	ld a,h			;2726	7c		|
	or l			;2727	b5		.
	ret z			;2728	c8		.
	ld e,(hl)		;2729	5e		^
	inc hl			;272a	23		#
	ld d,(hl)		;272b	56		V
	inc hl			;272c	23		#
	ld (0e9b3h),hl		;272d	22 b3 e9	" . .
	inc hl			;2730	23		#
	inc hl			;2731	23		#
	ld (0e9b5h),hl		;2732	22 b5 e9	" . .
	inc hl			;2735	23		#
	inc hl			;2736	23		#
	ld (0e9b7h),hl		;2737	22 b7 e9	" . .
	inc hl			;273a	23		#
	inc hl			;273b	23		#
	ex de,hl		;273c	eb		.
	ld (0e9d0h),hl		;273d	22 d0 e9	" . .
	ld hl,0e9b9h		;2740	21 b9 e9	! . .
	ld c,008h		;2743	0e 08		. .
	call 0df4fh		;2745	cd 4f df	. O .
	ld hl,(0e9bbh)		;2748	2a bb e9	* . .
	ex de,hl		;274b	eb		.
	ld hl,0e9c1h		;274c	21 c1 e9	! . .
	ld c,00fh		;274f	0e 0f		. .
	call 0df4fh		;2751	cd 4f df	. O .
	ld hl,(0e9c6h)		;2754	2a c6 e9	* . .
	ld a,h			;2757	7c		|
	ld hl,0e9ddh		;2758	21 dd e9	! . .
	ld (hl),0ffh		;275b	36 ff		6 .
	or a			;275d	b7		.
	jp z,0df9dh		;275e	ca 9d df	. . .
	ld (hl),000h		;2761	36 00		6 .
	ld a,0ffh		;2763	3e ff		> .
	or a			;2765	b7		.
	ret			;2766	c9		.
	call 0ea18h		;2767	cd 18 ea	. . .
	xor a			;276a	af		.
	ld hl,(0e9b5h)		;276b	2a b5 e9	* . .
	ld (hl),a		;276e	77		w
	inc hl			;276f	23		#
	ld (hl),a		;2770	77		w
	ld hl,(0e9b7h)		;2771	2a b7 e9	* . .
	ld (hl),a		;2774	77		w
	inc hl			;2775	23		#
	ld (hl),a		;2776	77		w
	ret			;2777	c9		.
	call 0ea27h		;2778	cd 27 ea	. ' .
	jp 0dfbbh		;277b	c3 bb df	. . .
	call 0ea2ah		;277e	cd 2a ea	. * .
	or a			;2781	b7		.
	ret z			;2782	c8		.
	ld hl,0dc09h		;2783	21 09 dc	! . .
	jp 0df4ah		;2786	c3 4a df	. J .
	ld hl,(0e9eah)		;2789	2a ea e9	* . .
	ld c,002h		;278c	0e 02		. .
	call 0e0eah		;278e	cd ea e0	. . .
	ld (0e9e5h),hl		;2791	22 e5 e9	" . .
	ld (0e9ech),hl		;2794	22 ec e9	" . .
	ld hl,0e9e5h		;2797	21 e5 e9	! . .
	ld c,(hl)		;279a	4e		N
	inc hl			;279b	23		#
	ld b,(hl)		;279c	46		F
	ld hl,(0e9b7h)		;279d	2a b7 e9	* . .
	ld e,(hl)		;27a0	5e		^
	inc hl			;27a1	23		#
	ld d,(hl)		;27a2	56		V
	ld hl,(0e9b5h)		;27a3	2a b5 e9	* . .
	ld a,(hl)		;27a6	7e		~
	inc hl			;27a7	23		#
	ld h,(hl)		;27a8	66		f
	ld l,a			;27a9	6f		o
	ld a,c			;27aa	79		y
	sub e			;27ab	93		.
	ld a,b			;27ac	78		x
	sbc a,d			;27ad	9a		.
	jp nc,0dffah		;27ae	d2 fa df	. . .
	push hl			;27b1	e5		.
	ld hl,(0e9c1h)		;27b2	2a c1 e9	* . .
	ld a,e			;27b5	7b		{
	sub l			;27b6	95		.
	ld e,a			;27b7	5f		_
	ld a,d			;27b8	7a		z
	sbc a,h			;27b9	9c		.
	ld d,a			;27ba	57		W
	pop hl			;27bb	e1		.
	dec hl			;27bc	2b		+
	jp 0dfe4h		;27bd	c3 e4 df	. . .
	push hl			;27c0	e5		.
	ld hl,(0e9c1h)		;27c1	2a c1 e9	* . .
	add hl,de		;27c4	19		.
	jp c,0e00fh		;27c5	da 0f e0	. . .
	ld a,c			;27c8	79		y
	sub l			;27c9	95		.
	ld a,b			;27ca	78		x
	sbc a,h			;27cb	9c		.
	jp c,0e00fh		;27cc	da 0f e0	. . .
	ex de,hl		;27cf	eb		.
	pop hl			;27d0	e1		.
	inc hl			;27d1	23		#
	jp 0dffah		;27d2	c3 fa df	. . .
	pop hl			;27d5	e1		.
	push bc			;27d6	c5		.
	push de			;27d7	d5		.
	push hl			;27d8	e5		.
	ex de,hl		;27d9	eb		.
	ld hl,(0e9ceh)		;27da	2a ce e9	* . .
	add hl,de		;27dd	19		.
	ld b,h			;27de	44		D
	ld c,l			;27df	4d		M
	call 0ea1eh		;27e0	cd 1e ea	. . .
	pop de			;27e3	d1		.
	ld hl,(0e9b5h)		;27e4	2a b5 e9	* . .
	ld (hl),e		;27e7	73		s
	inc hl			;27e8	23		#
	ld (hl),d		;27e9	72		r
	pop de			;27ea	d1		.
	ld hl,(0e9b7h)		;27eb	2a b7 e9	* . .
	ld (hl),e		;27ee	73		s
	inc hl			;27ef	23		#
	ld (hl),d		;27f0	72		r
	pop bc			;27f1	c1		.
	ld a,c			;27f2	79		y
	sub e			;27f3	93		.
	ld c,a			;27f4	4f		O
	ld a,b			;27f5	78		x
	sbc a,d			;27f6	9a		.
	ld b,a			;27f7	47		G
	ld hl,(0e9d0h)		;27f8	2a d0 e9	* . .
	ex de,hl		;27fb	eb		.
	call 0ea30h		;27fc	cd 30 ea	. 0 .
	ld c,l			;27ff	4d		M
	ld b,h			;2800	44		D
	jp 0ea21h		;2801	c3 21 ea	. ! .
	ld hl,0e9c3h		;2804	21 c3 e9	! . .
	ld c,(hl)		;2807	4e		N
	ld a,(0e9e3h)		;2808	3a e3 e9	: . .
	or a			;280b	b7		.
	rra			;280c	1f		.
	dec c			;280d	0d		.
	jp nz,0e045h		;280e	c2 45 e0	. E .
	ld b,a			;2811	47		G
	ld a,008h		;2812	3e 08		> .
	sub (hl)		;2814	96		.
	ld c,a			;2815	4f		O
	ld a,(0e9e2h)		;2816	3a e2 e9	: . .
	dec c			;2819	0d		.
	jp z,0e05ch		;281a	ca 5c e0	. \ .
	or a			;281d	b7		.
	rla			;281e	17		.
	jp 0e053h		;281f	c3 53 e0	. S .
	add a,b			;2822	80		.
	ret			;2823	c9		.
	ld hl,(0df43h)		;2824	2a 43 df	* C .
	ld de,Rst10Vector	;2827	11 10 00	. . .
	add hl,de		;282a	19		.
	add hl,bc		;282b	09		.
	ld a,(0e9ddh)		;282c	3a dd e9	: . .
	or a			;282f	b7		.
	jp z,0e071h		;2830	ca 71 e0	. q .
	ld l,(hl)		;2833	6e		n
	ld h,000h		;2834	26 00		& .
	ret			;2836	c9		.
	add hl,bc		;2837	09		.
	ld e,(hl)		;2838	5e		^
	inc hl			;2839	23		#
	ld d,(hl)		;283a	56		V
	ex de,hl		;283b	eb		.
	ret			;283c	c9		.
	call 0e03eh		;283d	cd 3e e0	. > .
	ld c,a			;2840	4f		O
	ld b,000h		;2841	06 00		. .
	call 0e05eh		;2843	cd 5e e0	. ^ .
	ld (0e9e5h),hl		;2846	22 e5 e9	" . .
	ret			;2849	c9		.
	ld hl,(0e9e5h)		;284a	2a e5 e9	* . .
	ld a,l			;284d	7d		}
	or h			;284e	b4		.
	ret			;284f	c9		.
	ld a,(0e9c3h)		;2850	3a c3 e9	: . .
	ld hl,(0e9e5h)		;2853	2a e5 e9	* . .
	add hl,hl		;2856	29		)
	dec a			;2857	3d		=
	jp nz,0e090h		;2858	c2 90 e0	. . .
	ld (0e9e7h),hl		;285b	22 e7 e9	" . .
	ld a,(0e9c4h)		;285e	3a c4 e9	: . .
	ld c,a			;2861	4f		O
	ld a,(0e9e3h)		;2862	3a e3 e9	: . .
	and c			;2865	a1		.
	or l			;2866	b5		.
	ld l,a			;2867	6f		o
	ld (0e9e5h),hl		;2868	22 e5 e9	" . .
	ret			;286b	c9		.
	ld hl,(0df43h)		;286c	2a 43 df	* C .
	ld de,l000ch		;286f	11 0c 00	. . .
	add hl,de		;2872	19		.
	ret			;2873	c9		.
	ld hl,(0df43h)		;2874	2a 43 df	* C .
	ld de,l000fh		;2877	11 0f 00	. . .
	add hl,de		;287a	19		.
	ex de,hl		;287b	eb		.
	ld hl,l0011h		;287c	21 11 00	! . .
	add hl,de		;287f	19		.
	ret			;2880	c9		.
	call 0e0aeh		;2881	cd ae e0	. . .
	ld a,(hl)		;2884	7e		~
	ld (0e9e3h),a		;2885	32 e3 e9	2 . .
	ex de,hl		;2888	eb		.
	ld a,(hl)		;2889	7e		~
	ld (0e9e1h),a		;288a	32 e1 e9	2 . .
	call 0e0a6h		;288d	cd a6 e0	. . .
	ld a,(0e9c5h)		;2890	3a c5 e9	: . .
	and (hl)		;2893	a6		.
	ld (0e9e2h),a		;2894	32 e2 e9	2 . .
	ret			;2897	c9		.
	call 0e0aeh		;2898	cd ae e0	. . .
	ld a,(0e9d5h)		;289b	3a d5 e9	: . .
	cp 002h			;289e	fe 02		. .
	jp nz,0e0deh		;28a0	c2 de e0	. . .
	xor a			;28a3	af		.
	ld c,a			;28a4	4f		O
	ld a,(0e9e3h)		;28a5	3a e3 e9	: . .
	add a,c			;28a8	81		.
	ld (hl),a		;28a9	77		w
	ex de,hl		;28aa	eb		.
	ld a,(0e9e1h)		;28ab	3a e1 e9	: . .
	ld (hl),a		;28ae	77		w
	ret			;28af	c9		.
	inc c			;28b0	0c		.
	dec c			;28b1	0d		.
	ret z			;28b2	c8		.
	ld a,h			;28b3	7c		|
	or a			;28b4	b7		.
	rra			;28b5	1f		.
	ld h,a			;28b6	67		g
	ld a,l			;28b7	7d		}
	rra			;28b8	1f		.
	ld l,a			;28b9	6f		o
	jp 0e0ebh		;28ba	c3 eb e0	. . .
	ld c,080h		;28bd	0e 80		. .
	ld hl,(0e9b9h)		;28bf	2a b9 e9	* . .
	xor a			;28c2	af		.
	add a,(hl)		;28c3	86		.
	inc hl			;28c4	23		#
	dec c			;28c5	0d		.
	jp nz,0e0fdh		;28c6	c2 fd e0	. . .
	ret			;28c9	c9		.
	inc c			;28ca	0c		.
	dec c			;28cb	0d		.
	ret z			;28cc	c8		.
	add hl,hl		;28cd	29		)
	jp 0e105h		;28ce	c3 05 e1	. . .
	push bc			;28d1	c5		.
	ld a,(0df42h)		;28d2	3a 42 df	: B .
	ld c,a			;28d5	4f		O
	ld hl,l0001h		;28d6	21 01 00	! . .
	call 0e104h		;28d9	cd 04 e1	. . .
	pop bc			;28dc	c1		.
	ld a,c			;28dd	79		y
	or l			;28de	b5		.
	ld l,a			;28df	6f		o
	ld a,b			;28e0	78		x
	or h			;28e1	b4		.
	ld h,a			;28e2	67		g
	ret			;28e3	c9		.
	ld hl,(0e9adh)		;28e4	2a ad e9	* . .
	ld a,(0df42h)		;28e7	3a 42 df	: B .
	ld c,a			;28ea	4f		O
	call 0e0eah		;28eb	cd ea e0	. . .
	ld a,l			;28ee	7d		}
	and 001h		;28ef	e6 01		. .
	ret			;28f1	c9		.
	ld hl,0e9adh		;28f2	21 ad e9	! . .
	ld c,(hl)		;28f5	4e		N
	inc hl			;28f6	23		#
	ld b,(hl)		;28f7	46		F
	call 0e10bh		;28f8	cd 0b e1	. . .
	ld (0e9adh),hl		;28fb	22 ad e9	" . .
	ld hl,(0e9c8h)		;28fe	2a c8 e9	* . .
	inc hl			;2901	23		#
	ex de,hl		;2902	eb		.
	ld hl,(0e9b3h)		;2903	2a b3 e9	* . .
	ld (hl),e		;2906	73		s
	inc hl			;2907	23		#
	ld (hl),d		;2908	72		r
	ret			;2909	c9		.
	call 0e15eh		;290a	cd 5e e1	. ^ .
	ld de,Rst08Vector+1	;290d	11 09 00	. . .
	add hl,de		;2910	19		.
	ld a,(hl)		;2911	7e		~
	rla			;2912	17		.
	ret nc			;2913	d0		.
	ld hl,0dc0fh		;2914	21 0f dc	! . .
	jp 0df4ah		;2917	c3 4a df	. J .
	call 0e11eh		;291a	cd 1e e1	. . .
	ret z			;291d	c8		.
	ld hl,0dc0dh		;291e	21 0d dc	! . .
	jp 0df4ah		;2921	c3 4a df	. J .
	ld hl,(0e9b9h)		;2924	2a b9 e9	* . .
	ld a,(0e9e9h)		;2927	3a e9 e9	: . .
	add a,l			;292a	85		.
	ld l,a			;292b	6f		o
	ret nc			;292c	d0		.
	inc h			;292d	24		$
	ret			;292e	c9		.
	ld hl,(0df43h)		;292f	2a 43 df	* C .
	ld de,l000eh		;2932	11 0e 00	. . .
	add hl,de		;2935	19		.
	ld a,(hl)		;2936	7e		~
	ret			;2937	c9		.
	call 0e169h		;2938	cd 69 e1	. i .
	ld (hl),000h		;293b	36 00		6 .
	ret			;293d	c9		.
	call 0e169h		;293e	cd 69 e1	. i .
	or 080h			;2941	f6 80		. .
	ld (hl),a		;2943	77		w
	ret			;2944	c9		.
	ld hl,(0e9eah)		;2945	2a ea e9	* . .
	ex de,hl		;2948	eb		.
	ld hl,(0e9b3h)		;2949	2a b3 e9	* . .
	ld a,e			;294c	7b		{
	sub (hl)		;294d	96		.
	inc hl			;294e	23		#
	ld a,d			;294f	7a		z
	sbc a,(hl)		;2950	9e		.
	ret			;2951	c9		.
	call 0e17fh		;2952	cd 7f e1	. . .
	ret c			;2955	d8		.
	inc de			;2956	13		.
	ld (hl),d		;2957	72		r
	dec hl			;2958	2b		+
	ld (hl),e		;2959	73		s
	ret			;295a	c9		.
	ld a,e			;295b	7b		{
	sub l			;295c	95		.
	ld l,a			;295d	6f		o
	ld a,d			;295e	7a		z
	sbc a,h			;295f	9c		.
	ld h,a			;2960	67		g
	ret			;2961	c9		.
	ld c,0ffh		;2962	0e ff		. .
	ld hl,(0e9ech)		;2964	2a ec e9	* . .
	ex de,hl		;2967	eb		.
	ld hl,(0e9cch)		;2968	2a cc e9	* . .
	call 0e195h		;296b	cd 95 e1	. . .
	ret nc			;296e	d0		.
	push bc			;296f	c5		.
	call 0e0f7h		;2970	cd f7 e0	. . .
	ld hl,(0e9bdh)		;2973	2a bd e9	* . .
	ex de,hl		;2976	eb		.
	ld hl,(0e9ech)		;2977	2a ec e9	* . .
	add hl,de		;297a	19		.
	pop bc			;297b	c1		.
	inc c			;297c	0c		.
	jp z,0e1c4h		;297d	ca c4 e1	. . .
	cp (hl)			;2980	be		.
	ret z			;2981	c8		.
	call 0e17fh		;2982	cd 7f e1	. . .
	ret nc			;2985	d0		.
	call 0e12ch		;2986	cd 2c e1	. , .
	ret			;2989	c9		.
	ld (hl),a		;298a	77		w
	ret			;298b	c9		.
	call 0e19ch		;298c	cd 9c e1	. . .
	call 0e1e0h		;298f	cd e0 e1	. . .
	ld c,001h		;2992	0e 01		. .
	call 0dfb8h		;2994	cd b8 df	. . .
	jp 0e1dah		;2997	c3 da e1	. . .
	call 0e1e0h		;299a	cd e0 e1	. . .
	call 0dfb2h		;299d	cd b2 df	. . .
	ld hl,0e9b1h		;29a0	21 b1 e9	! . .
	jp 0e1e3h		;29a3	c3 e3 e1	. . .
	ld hl,0e9b9h		;29a6	21 b9 e9	! . .
	ld c,(hl)		;29a9	4e		N
	inc hl			;29aa	23		#
	ld b,(hl)		;29ab	46		F
	jp 0ea24h		;29ac	c3 24 ea	. $ .
	ld hl,(0e9b9h)		;29af	2a b9 e9	* . .
	ex de,hl		;29b2	eb		.
	ld hl,(0e9b1h)		;29b3	2a b1 e9	* . .
	ld c,080h		;29b6	0e 80		. .
	jp 0df4fh		;29b8	c3 4f df	. O .
	ld hl,0e9eah		;29bb	21 ea e9	! . .
	ld a,(hl)		;29be	7e		~
	inc hl			;29bf	23		#
	cp (hl)			;29c0	be		.
	ret nz			;29c1	c0		.
	inc a			;29c2	3c		<
	ret			;29c3	c9		.
	ld hl,0ffffh		;29c4	21 ff ff	! . .
	ld (0e9eah),hl		;29c7	22 ea e9	" . .
	ret			;29ca	c9		.
	ld hl,(0e9c8h)		;29cb	2a c8 e9	* . .
	ex de,hl		;29ce	eb		.
	ld hl,(0e9eah)		;29cf	2a ea e9	* . .
	inc hl			;29d2	23		#
	ld (0e9eah),hl		;29d3	22 ea e9	" . .
	call 0e195h		;29d6	cd 95 e1	. . .
	jp nc,0e219h		;29d9	d2 19 e2	. . .
	jp 0e1feh		;29dc	c3 fe e1	. . .
	ld a,(0e9eah)		;29df	3a ea e9	: . .
	and 003h		;29e2	e6 03		. .
	ld b,005h		;29e4	06 05		. .
	add a,a			;29e6	87		.
	dec b			;29e7	05		.
	jp nz,0e220h		;29e8	c2 20 e2	.   .
	ld (0e9e9h),a		;29eb	32 e9 e9	2 . .
	or a			;29ee	b7		.
	ret nz			;29ef	c0		.
	push bc			;29f0	c5		.
	call 0dfc3h		;29f1	cd c3 df	. . .
	call 0e1d4h		;29f4	cd d4 e1	. . .
	pop bc			;29f7	c1		.
	jp 0e19eh		;29f8	c3 9e e1	. . .
	ld a,c			;29fb	79		y
	and 007h		;29fc	e6 07		. .
	inc a			;29fe	3c		<
	ld e,a			;29ff	5f		_
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BdosRecordMath - FCB record/block arithmetic.
; LD D,A / LD A,C / RRCA x3 / AND $1F: sector-within-block split;
; LD A,B / ADD A,A x5 / OR C: block number assembly.  Feeds the
; sequential-read path that loads transients record by record
; (~176 reads for PR2.COM, ../bios-ram.md).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BdosRecordMath:
	ld d,a			;2a00	57		W
	ld a,c			;2a01	79		y
	rrca			;2a02	0f		.
	rrca			;2a03	0f		.
	rrca			;2a04	0f		.
	and 01fh		;2a05	e6 1f		. .
	ld c,a			;2a07	4f		O
	ld a,b			;2a08	78		x
	add a,a			;2a09	87		.
	add a,a			;2a0a	87		.
	add a,a			;2a0b	87		.
	add a,a			;2a0c	87		.
	add a,a			;2a0d	87		.
	or c			;2a0e	b1		.
	ld c,a			;2a0f	4f		O
	ld a,b			;2a10	78		x
	rrca			;2a11	0f		.
	rrca			;2a12	0f		.
	rrca			;2a13	0f		.
	and 01fh		;2a14	e6 1f		. .
	ld b,a			;2a16	47		G
	ld hl,(0e9bfh)		;2a17	2a bf e9	* . .
	add hl,bc		;2a1a	09		.
	ld a,(hl)		;2a1b	7e		~
	rlca			;2a1c	07		.
	dec e			;2a1d	1d		.
	jp nz,0e256h		;2a1e	c2 56 e2	. V .
	ret			;2a21	c9		.
	push de			;2a22	d5		.
	call 0e235h		;2a23	cd 35 e2	. 5 .
	and 0feh		;2a26	e6 fe		. .
	pop bc			;2a28	c1		.
	or c			;2a29	b1		.
	rrca			;2a2a	0f		.
	dec d			;2a2b	15		.
	jp nz,0e264h		;2a2c	c2 64 e2	. d .
	ld (hl),a		;2a2f	77		w
	ret			;2a30	c9		.
	call 0e15eh		;2a31	cd 5e e1	. ^ .
	ld de,Rst10Vector	;2a34	11 10 00	. . .
	add hl,de		;2a37	19		.
	push bc			;2a38	c5		.
	ld c,011h		;2a39	0e 11		. .
	pop de			;2a3b	d1		.
	dec c			;2a3c	0d		.
	ret z			;2a3d	c8		.
	push de			;2a3e	d5		.
	ld a,(0e9ddh)		;2a3f	3a dd e9	: . .
	or a			;2a42	b7		.
	jp z,0e288h		;2a43	ca 88 e2	. . .
	push bc			;2a46	c5		.
	push hl			;2a47	e5		.
	ld c,(hl)		;2a48	4e		N
	ld b,000h		;2a49	06 00		. .
	jp 0e28eh		;2a4b	c3 8e e2	. . .
	dec c			;2a4e	0d		.
	push bc			;2a4f	c5		.
	ld c,(hl)		;2a50	4e		N
	inc hl			;2a51	23		#
	ld b,(hl)		;2a52	46		F
	push hl			;2a53	e5		.
	ld a,c			;2a54	79		y
	or b			;2a55	b0		.
	jp z,0e29dh		;2a56	ca 9d e2	. . .
	ld hl,(0e9c6h)		;2a59	2a c6 e9	* . .
	ld a,l			;2a5c	7d		}
	sub c			;2a5d	91		.
	ld a,h			;2a5e	7c		|
	sbc a,b			;2a5f	98		.
	call nc,0e25ch		;2a60	d4 5c e2	. \ .
	pop hl			;2a63	e1		.
	inc hl			;2a64	23		#
	pop bc			;2a65	c1		.
	jp 0e275h		;2a66	c3 75 e2	. u .
	ld hl,(0e9c6h)		;2a69	2a c6 e9	* . .
	ld c,003h		;2a6c	0e 03		. .
	call 0e0eah		;2a6e	cd ea e0	. . .
	inc hl			;2a71	23		#
	ld b,h			;2a72	44		D
	ld c,l			;2a73	4d		M
	ld hl,(0e9bfh)		;2a74	2a bf e9	* . .
	ld (hl),000h		;2a77	36 00		6 .
	inc hl			;2a79	23		#
	dec bc			;2a7a	0b		.
	ld a,b			;2a7b	78		x
	or c			;2a7c	b1		.
	jp nz,0e2b1h		;2a7d	c2 b1 e2	. . .
	ld hl,(0e9cah)		;2a80	2a ca e9	* . .
	ex de,hl		;2a83	eb		.
	ld hl,(0e9bfh)		;2a84	2a bf e9	* . .
	ld (hl),e		;2a87	73		s
	inc hl			;2a88	23		#
	ld (hl),d		;2a89	72		r
	call 0dfa1h		;2a8a	cd a1 df	. . .
	ld hl,(0e9b3h)		;2a8d	2a b3 e9	* . .
	ld (hl),003h		;2a90	36 03		6 .
	inc hl			;2a92	23		#
	ld (hl),000h		;2a93	36 00		6 .
	call 0e1feh		;2a95	cd fe e1	. . .
	ld c,0ffh		;2a98	0e ff		. .
	call 0e205h		;2a9a	cd 05 e2	. . .
	call 0e1f5h		;2a9d	cd f5 e1	. . .
	ret z			;2aa0	c8		.
	call 0e15eh		;2aa1	cd 5e e1	. ^ .
	ld a,0e5h		;2aa4	3e e5		> .
	cp (hl)			;2aa6	be		.
	jp z,0e2d2h		;2aa7	ca d2 e2	. . .
	ld a,(0df41h)		;2aaa	3a 41 df	: A .
	cp (hl)			;2aad	be		.
	jp nz,0e2f6h		;2aae	c2 f6 e2	. . .
	inc hl			;2ab1	23		#
	ld a,(hl)		;2ab2	7e		~
	sub 024h		;2ab3	d6 24		. $
	jp nz,0e2f6h		;2ab5	c2 f6 e2	. . .
	dec a			;2ab8	3d		=
	ld (0df45h),a		;2ab9	32 45 df	2 E .
	ld c,001h		;2abc	0e 01		. .
	call 0e26bh		;2abe	cd 6b e2	. k .
	call 0e18ch		;2ac1	cd 8c e1	. . .
	jp 0e2d2h		;2ac4	c3 d2 e2	. . .
	ld a,(0e9d4h)		;2ac7	3a d4 e9	: . .
	jp 0df01h		;2aca	c3 01 df	. . .
	push bc			;2acd	c5		.
	push af			;2ace	f5		.
	ld a,(0e9c5h)		;2acf	3a c5 e9	: . .
	cpl			;2ad2	2f		/
	ld b,a			;2ad3	47		G
	ld a,c			;2ad4	79		y
	and b			;2ad5	a0		.
	ld c,a			;2ad6	4f		O
	pop af			;2ad7	f1		.
	and b			;2ad8	a0		.
	sub c			;2ad9	91		.
	and 01fh		;2ada	e6 1f		. .
	pop bc			;2adc	c1		.
	ret			;2add	c9		.
	ld a,0ffh		;2ade	3e ff		> .
	ld (0e9d4h),a		;2ae0	32 d4 e9	2 . .
	ld hl,0e9d8h		;2ae3	21 d8 e9	! . .
	ld (hl),c		;2ae6	71		q
	ld hl,(0df43h)		;2ae7	2a 43 df	* C .
	ld (0e9d9h),hl		;2aea	22 d9 e9	" . .
	call 0e1feh		;2aed	cd fe e1	. . .
	call 0dfa1h		;2af0	cd a1 df	. . .
	ld c,000h		;2af3	0e 00		. .
	call 0e205h		;2af5	cd 05 e2	. . .
	call 0e1f5h		;2af8	cd f5 e1	. . .
	jp z,0e394h		;2afb	ca 94 e3	. . .
	ld hl,(0e9d9h)		;2afe	2a d9 e9	* . .
	ex de,hl		;2b01	eb		.
	ld a,(de)		;2b02	1a		.
	cp 0e5h			;2b03	fe e5		. .
	jp z,0e34ah		;2b05	ca 4a e3	. J .
	push de			;2b08	d5		.
	call 0e17fh		;2b09	cd 7f e1	. . .
	pop de			;2b0c	d1		.
	jp nc,0e394h		;2b0d	d2 94 e3	. . .
	call 0e15eh		;2b10	cd 5e e1	. ^ .
	ld a,(0e9d8h)		;2b13	3a d8 e9	: . .
	ld c,a			;2b16	4f		O
	ld b,000h		;2b17	06 00		. .
	ld a,c			;2b19	79		y
	or a			;2b1a	b7		.
	jp z,0e383h		;2b1b	ca 83 e3	. . .
	ld a,(de)		;2b1e	1a		.
	cp 03fh			;2b1f	fe 3f		. ?
	jp z,0e37ch		;2b21	ca 7c e3	. | .
	ld a,b			;2b24	78		x
	cp 00dh			;2b25	fe 0d		. .
	jp z,0e37ch		;2b27	ca 7c e3	. | .
	cp 00ch			;2b2a	fe 0c		. .
	ld a,(de)		;2b2c	1a		.
	jp z,0e373h		;2b2d	ca 73 e3	. s .
	sub (hl)		;2b30	96		.
	and 07fh		;2b31	e6 7f		. .
	jp nz,0e32dh		;2b33	c2 2d e3	. - .
	jp 0e37ch		;2b36	c3 7c e3	. | .
	push bc			;2b39	c5		.
	ld c,(hl)		;2b3a	4e		N
	call 0e307h		;2b3b	cd 07 e3	. . .
	pop bc			;2b3e	c1		.
	jp nz,0e32dh		;2b3f	c2 2d e3	. - .
	inc de			;2b42	13		.
	inc hl			;2b43	23		#
	inc b			;2b44	04		.
	dec c			;2b45	0d		.
	jp 0e353h		;2b46	c3 53 e3	. S .
	ld a,(0e9eah)		;2b49	3a ea e9	: . .
	and 003h		;2b4c	e6 03		. .
	ld (0df45h),a		;2b4e	32 45 df	2 E .
	ld hl,0e9d4h		;2b51	21 d4 e9	! . .
	ld a,(hl)		;2b54	7e		~
	rla			;2b55	17		.
	ret nc			;2b56	d0		.
	xor a			;2b57	af		.
	ld (hl),a		;2b58	77		w
	ret			;2b59	c9		.
	call 0e1feh		;2b5a	cd fe e1	. . .
	ld a,0ffh		;2b5d	3e ff		> .
	jp 0df01h		;2b5f	c3 01 df	. . .
	call 0e154h		;2b62	cd 54 e1	. T .
	ld c,00ch		;2b65	0e 0c		. .
	call 0e318h		;2b67	cd 18 e3	. . .
	call 0e1f5h		;2b6a	cd f5 e1	. . .
	ret z			;2b6d	c8		.
	call 0e144h		;2b6e	cd 44 e1	. D .
	call 0e15eh		;2b71	cd 5e e1	. ^ .
	ld (hl),0e5h		;2b74	36 e5		6 .
	ld c,000h		;2b76	0e 00		. .
	call 0e26bh		;2b78	cd 6b e2	. k .
	call 0e1c6h		;2b7b	cd c6 e1	. . .
	call 0e32dh		;2b7e	cd 2d e3	. - .
	jp 0e3a4h		;2b81	c3 a4 e3	. . .
	ld d,b			;2b84	50		P
	ld e,c			;2b85	59		Y
	ld a,c			;2b86	79		y
	or b			;2b87	b0		.
	jp z,0e3d1h		;2b88	ca d1 e3	. . .
	dec bc			;2b8b	0b		.
	push de			;2b8c	d5		.
	push bc			;2b8d	c5		.
	call 0e235h		;2b8e	cd 35 e2	. 5 .
	rra			;2b91	1f		.
	jp nc,0e3ech		;2b92	d2 ec e3	. . .
	pop bc			;2b95	c1		.
	pop de			;2b96	d1		.
	ld hl,(0e9c6h)		;2b97	2a c6 e9	* . .
	ld a,e			;2b9a	7b		{
	sub l			;2b9b	95		.
	ld a,d			;2b9c	7a		z
	sbc a,h			;2b9d	9c		.
	jp nc,0e3f4h		;2b9e	d2 f4 e3	. . .
	inc de			;2ba1	13		.
	push bc			;2ba2	c5		.
	push de			;2ba3	d5		.
	ld b,d			;2ba4	42		B
	ld c,e			;2ba5	4b		K
	call 0e235h		;2ba6	cd 35 e2	. 5 .
	rra			;2ba9	1f		.
	jp nc,0e3ech		;2baa	d2 ec e3	. . .
	pop de			;2bad	d1		.
	pop bc			;2bae	c1		.
	jp 0e3c0h		;2baf	c3 c0 e3	. . .
	rla			;2bb2	17		.
	inc a			;2bb3	3c		<
	call 0e264h		;2bb4	cd 64 e2	. d .
	pop hl			;2bb7	e1		.
	pop de			;2bb8	d1		.
	ret			;2bb9	c9		.
	ld a,c			;2bba	79		y
	or b			;2bbb	b0		.
	jp nz,0e3c0h		;2bbc	c2 c0 e3	. . .
	ld hl,WbootStub		;2bbf	21 00 00	! . .
	ret			;2bc2	c9		.
	ld c,000h		;2bc3	0e 00		. .
	ld e,020h		;2bc5	1e 20		.  
	push de			;2bc7	d5		.
	ld b,000h		;2bc8	06 00		. .
	ld hl,(0df43h)		;2bca	2a 43 df	* C .
	add hl,bc		;2bcd	09		.
	ex de,hl		;2bce	eb		.
	call 0e15eh		;2bcf	cd 5e e1	. ^ .
	pop bc			;2bd2	c1		.
	call 0df4fh		;2bd3	cd 4f df	. O .
	call 0dfc3h		;2bd6	cd c3 df	. . .
	jp 0e1c6h		;2bd9	c3 c6 e1	. . .
	call 0e154h		;2bdc	cd 54 e1	. T .
	ld c,00ch		;2bdf	0e 0c		. .
	call 0e318h		;2be1	cd 18 e3	. . .
	ld hl,(0df43h)		;2be4	2a 43 df	* C .
	ld a,(hl)		;2be7	7e		~
	ld de,Rst10Vector	;2be8	11 10 00	. . .
	add hl,de		;2beb	19		.
	ld (hl),a		;2bec	77		w
	call 0e1f5h		;2bed	cd f5 e1	. . .
	ret z			;2bf0	c8		.
	call 0e144h		;2bf1	cd 44 e1	. D .
	ld c,010h		;2bf4	0e 10		. .
	ld e,00ch		;2bf6	1e 0c		. .
	call 0e401h		;2bf8	cd 01 e4	. . .
	call 0e32dh		;2bfb	cd 2d e3	. - .
	jp 0e427h		;2bfe	c3 27 e4	. ' .
	ld c,00ch		;2c01	0e 0c		. .
	call 0e318h		;2c03	cd 18 e3	. . .
	call 0e1f5h		;2c06	cd f5 e1	. . .
	ret z			;2c09	c8		.
	ld c,000h		;2c0a	0e 00		. .
	ld e,00ch		;2c0c	1e 0c		. .
	call 0e401h		;2c0e	cd 01 e4	. . .
	call 0e32dh		;2c11	cd 2d e3	. - .
	jp 0e440h		;2c14	c3 40 e4	. @ .
	ld c,00fh		;2c17	0e 0f		. .
	call 0e318h		;2c19	cd 18 e3	. . .
	call 0e1f5h		;2c1c	cd f5 e1	. . .
	ret z			;2c1f	c8		.
	call 0e0a6h		;2c20	cd a6 e0	. . .
	ld a,(hl)		;2c23	7e		~
	push af			;2c24	f5		.
	push hl			;2c25	e5		.
	call 0e15eh		;2c26	cd 5e e1	. ^ .
	ex de,hl		;2c29	eb		.
	ld hl,(0df43h)		;2c2a	2a 43 df	* C .
	ld c,020h		;2c2d	0e 20		.  
	push de			;2c2f	d5		.
	call 0df4fh		;2c30	cd 4f df	. O .
	call 0e178h		;2c33	cd 78 e1	. x .
	pop de			;2c36	d1		.
	ld hl,l000ch		;2c37	21 0c 00	! . .
	add hl,de		;2c3a	19		.
	ld c,(hl)		;2c3b	4e		N
	ld hl,l000fh		;2c3c	21 0f 00	! . .
	add hl,de		;2c3f	19		.
	ld b,(hl)		;2c40	46		F
	pop hl			;2c41	e1		.
	pop af			;2c42	f1		.
	ld (hl),a		;2c43	77		w
	ld a,c			;2c44	79		y
	cp (hl)			;2c45	be		.
	ld a,b			;2c46	78		x
	jp z,0e48bh		;2c47	ca 8b e4	. . .
	ld a,000h		;2c4a	3e 00		> .
	jp c,0e48bh		;2c4c	da 8b e4	. . .
	ld a,080h		;2c4f	3e 80		> .
	ld hl,(0df43h)		;2c51	2a 43 df	* C .
	ld de,l000fh		;2c54	11 0f 00	. . .
	add hl,de		;2c57	19		.
	ld (hl),a		;2c58	77		w
	ret			;2c59	c9		.
	ld a,(hl)		;2c5a	7e		~
	inc hl			;2c5b	23		#
	or (hl)			;2c5c	b6		.
	dec hl			;2c5d	2b		+
	ret nz			;2c5e	c0		.
	ld a,(de)		;2c5f	1a		.
	ld (hl),a		;2c60	77		w
	inc de			;2c61	13		.
	inc hl			;2c62	23		#
	ld a,(de)		;2c63	1a		.
	ld (hl),a		;2c64	77		w
	dec de			;2c65	1b		.
	dec hl			;2c66	2b		+
	ret			;2c67	c9		.
	xor a			;2c68	af		.
	ld (0df45h),a		;2c69	32 45 df	2 E .
	ld (0e9eah),a		;2c6c	32 ea e9	2 . .
	ld (0e9ebh),a		;2c6f	32 eb e9	2 . .
	call 0e11eh		;2c72	cd 1e e1	. . .
	ret nz			;2c75	c0		.
	call 0e169h		;2c76	cd 69 e1	. i .
	and 080h		;2c79	e6 80		. .
	ret nz			;2c7b	c0		.
	ld c,00fh		;2c7c	0e 0f		. .
	call 0e318h		;2c7e	cd 18 e3	. . .
	call 0e1f5h		;2c81	cd f5 e1	. . .
	ret z			;2c84	c8		.
	ld bc,Rst10Vector	;2c85	01 10 00	. . .
	call 0e15eh		;2c88	cd 5e e1	. ^ .
	add hl,bc		;2c8b	09		.
	ex de,hl		;2c8c	eb		.
	ld hl,(0df43h)		;2c8d	2a 43 df	* C .
	add hl,bc		;2c90	09		.
	ld c,010h		;2c91	0e 10		. .
	ld a,(0e9ddh)		;2c93	3a dd e9	: . .
	or a			;2c96	b7		.
	jp z,0e4e8h		;2c97	ca e8 e4	. . .
	ld a,(hl)		;2c9a	7e		~
	or a			;2c9b	b7		.
	ld a,(de)		;2c9c	1a		.
	jp nz,0e4dbh		;2c9d	c2 db e4	. . .
	ld (hl),a		;2ca0	77		w
	or a			;2ca1	b7		.
	jp nz,0e4e1h		;2ca2	c2 e1 e4	. . .
	ld a,(hl)		;2ca5	7e		~
	ld (de),a		;2ca6	12		.
	cp (hl)			;2ca7	be		.
	jp nz,0e51fh		;2ca8	c2 1f e5	. . .
	jp 0e4fdh		;2cab	c3 fd e4	. . .
	call 0e494h		;2cae	cd 94 e4	. . .
	ex de,hl		;2cb1	eb		.
	call 0e494h		;2cb2	cd 94 e4	. . .
	ex de,hl		;2cb5	eb		.
	ld a,(de)		;2cb6	1a		.
	cp (hl)			;2cb7	be		.
	jp nz,0e51fh		;2cb8	c2 1f e5	. . .
	inc de			;2cbb	13		.
	inc hl			;2cbc	23		#
	ld a,(de)		;2cbd	1a		.
	cp (hl)			;2cbe	be		.
	jp nz,0e51fh		;2cbf	c2 1f e5	. . .
	dec c			;2cc2	0d		.
	inc de			;2cc3	13		.
	inc hl			;2cc4	23		#
	dec c			;2cc5	0d		.
	jp nz,0e4cdh		;2cc6	c2 cd e4	. . .
	ld bc,0ffech		;2cc9	01 ec ff	. . .
	add hl,bc		;2ccc	09		.
	ex de,hl		;2ccd	eb		.
	add hl,bc		;2cce	09		.
	ld a,(de)		;2ccf	1a		.
	cp (hl)			;2cd0	be		.
	jp c,0e517h		;2cd1	da 17 e5	. . .
	ld (hl),a		;2cd4	77		w
	ld bc,l0001h+2		;2cd5	01 03 00	. . .
	add hl,bc		;2cd8	09		.
	ex de,hl		;2cd9	eb		.
	add hl,bc		;2cda	09		.
	ld a,(hl)		;2cdb	7e		~
	ld (de),a		;2cdc	12		.
	ld a,0ffh		;2cdd	3e ff		> .
	ld (0e9d2h),a		;2cdf	32 d2 e9	2 . .
	jp 0e410h		;2ce2	c3 10 e4	. . .
	ld hl,0df45h		;2ce5	21 45 df	! E .
	dec (hl)		;2ce8	35		5
l2ce9h:
	ret			;2ce9	c9		.
	call 0e154h		;2cea	cd 54 e1	. T .
	ld hl,(0df43h)		;2ced	2a 43 df	* C .
	push hl			;2cf0	e5		.
	ld hl,0e9ach		;2cf1	21 ac e9	! . .
	ld (0df43h),hl		;2cf4	22 43 df	" C .
	ld c,001h		;2cf7	0e 01		. .
	call 0e318h		;2cf9	cd 18 e3	. . .
	call 0e1f5h		;2cfc	cd f5 e1	. . .
	pop hl			;2cff	e1		.
	ld (0df43h),hl		;2d00	22 43 df	" C .
	ret z			;2d03	c8		.
	ex de,hl		;2d04	eb		.
	ld hl,l000fh		;2d05	21 0f 00	! . .
	add hl,de		;2d08	19		.
	ld c,011h		;2d09	0e 11		. .
	xor a			;2d0b	af		.
	ld (hl),a		;2d0c	77		w
	inc hl			;2d0d	23		#
	dec c			;2d0e	0d		.
	jp nz,0e546h		;2d0f	c2 46 e5	. F .
	ld hl,l000dh		;2d12	21 0d 00	! . .
	add hl,de		;2d15	19		.
	ld (hl),a		;2d16	77		w
	call 0e18ch		;2d17	cd 8c e1	. . .
	call 0e3fdh		;2d1a	cd fd e3	. . .
	jp 0e178h		;2d1d	c3 78 e1	. x .
	xor a			;2d20	af		.
	ld (0e9d2h),a		;2d21	32 d2 e9	2 . .
	call 0e4a2h		;2d24	cd a2 e4	. . .
	call 0e1f5h		;2d27	cd f5 e1	. . .
	ret z			;2d2a	c8		.
	ld hl,(0df43h)		;2d2b	2a 43 df	* C .
	ld bc,l000ch		;2d2e	01 0c 00	. . .
	add hl,bc		;2d31	09		.
	ld a,(hl)		;2d32	7e		~
	inc a			;2d33	3c		<
	and 01fh		;2d34	e6 1f		. .
	ld (hl),a		;2d36	77		w
	jp z,0e583h		;2d37	ca 83 e5	. . .
	ld b,a			;2d3a	47		G
	ld a,(0e9c5h)		;2d3b	3a c5 e9	: . .
	and b			;2d3e	a0		.
	ld hl,0e9d2h		;2d3f	21 d2 e9	! . .
	and (hl)		;2d42	a6		.
	jp z,0e58eh		;2d43	ca 8e e5	. . .
	jp 0e5ach		;2d46	c3 ac e5	. . .
	ld bc,l0001h+1		;2d49	01 02 00	. . .
	add hl,bc		;2d4c	09		.
	inc (hl)		;2d4d	34		4
	ld a,(hl)		;2d4e	7e		~
	and 00fh		;2d4f	e6 0f		. .
	jp z,0e5b6h		;2d51	ca b6 e5	. . .
	ld c,00fh		;2d54	0e 0f		. .
	call 0e318h		;2d56	cd 18 e3	. . .
	call 0e1f5h		;2d59	cd f5 e1	. . .
	jp nz,0e5ach		;2d5c	c2 ac e5	. . .
	ld a,(0e9d3h)		;2d5f	3a d3 e9	: . .
	inc a			;2d62	3c		<
	jp z,0e5b6h		;2d63	ca b6 e5	. . .
	call 0e524h		;2d66	cd 24 e5	. $ .
	call 0e1f5h		;2d69	cd f5 e1	. . .
	jp z,0e5b6h		;2d6c	ca b6 e5	. . .
	jp 0e5afh		;2d6f	c3 af e5	. . .
	call 0e45ah		;2d72	cd 5a e4	. Z .
	call 0e0bbh		;2d75	cd bb e0	. . .
	xor a			;2d78	af		.
	jp 0df01h		;2d79	c3 01 df	. . .
	call 0df05h		;2d7c	cd 05 df	. . .
	jp 0e178h		;2d7f	c3 78 e1	. x .
	ld a,001h		;2d82	3e 01		> .
	ld (0e9d5h),a		;2d84	32 d5 e9	2 . .
	ld a,0ffh		;2d87	3e ff		> .
	ld (0e9d3h),a		;2d89	32 d3 e9	2 . .
	call 0e0bbh		;2d8c	cd bb e0	. . .
	ld a,(0e9e3h)		;2d8f	3a e3 e9	: . .
	ld hl,0e9e1h		;2d92	21 e1 e9	! . .
	cp (hl)			;2d95	be		.
	jp c,0e5e6h		;2d96	da e6 e5	. . .
	cp 080h			;2d99	fe 80		. .
	jp nz,0e5fbh		;2d9b	c2 fb e5	. . .
	call 0e55ah		;2d9e	cd 5a e5	. Z .
	xor a			;2da1	af		.
	ld (0e9e3h),a		;2da2	32 e3 e9	2 . .
	ld a,(0df45h)		;2da5	3a 45 df	: E .
	or a			;2da8	b7		.
	jp nz,0e5fbh		;2da9	c2 fb e5	. . .
	call 0e077h		;2dac	cd 77 e0	. w .
	call 0e084h		;2daf	cd 84 e0	. . .
	jp z,0e5fbh		;2db2	ca fb e5	. . .
	call 0e08ah		;2db5	cd 8a e0	. . .
	call 0dfd1h		;2db8	cd d1 df	. . .
	call 0dfb2h		;2dbb	cd b2 df	. . .
	jp 0e0d2h		;2dbe	c3 d2 e0	. . .
	jp 0df05h		;2dc1	c3 05 df	. . .
	ld a,001h		;2dc4	3e 01		> .
	ld (0e9d5h),a		;2dc6	32 d5 e9	2 . .
	ld a,000h		;2dc9	3e 00		> .
	ld (0e9d3h),a		;2dcb	32 d3 e9	2 . .
	call 0e154h		;2dce	cd 54 e1	. T .
	ld hl,(0df43h)		;2dd1	2a 43 df	* C .
	call 0e147h		;2dd4	cd 47 e1	. G .
	call 0e0bbh		;2dd7	cd bb e0	. . .
	ld a,(0e9e3h)		;2dda	3a e3 e9	: . .
	cp 080h			;2ddd	fe 80		. .
	jp nc,0df05h		;2ddf	d2 05 df	. . .
	call 0e077h		;2de2	cd 77 e0	. w .
	call 0e084h		;2de5	cd 84 e0	. . .
	ld c,000h		;2de8	0e 00		. .
	jp nz,0e66eh		;2dea	c2 6e e6	. n .
	call 0e03eh		;2ded	cd 3e e0	. > .
	ld (0e9d7h),a		;2df0	32 d7 e9	2 . .
	ld bc,WbootStub		;2df3	01 00 00	. . .
	or a			;2df6	b7		.
	jp z,0e63bh		;2df7	ca 3b e6	. ; .
	ld c,a			;2dfa	4f		O
	dec bc			;2dfb	0b		.
	call 0e05eh		;2dfc	cd 5e e0	. ^ .
	ld b,h			;2dff	44		D
	ld c,l			;2e00	4d		M
	call 0e3beh		;2e01	cd be e3	. . .
	ld a,l			;2e04	7d		}
	or h			;2e05	b4		.
	jp nz,0e648h		;2e06	c2 48 e6	. H .
	ld a,002h		;2e09	3e 02		> .
	jp 0df01h		;2e0b	c3 01 df	. . .
	ld (0e9e5h),hl		;2e0e	22 e5 e9	" . .
	ex de,hl		;2e11	eb		.
	ld hl,(0df43h)		;2e12	2a 43 df	* C .
	ld bc,Rst10Vector	;2e15	01 10 00	. . .
	add hl,bc		;2e18	09		.
	ld a,(0e9ddh)		;2e19	3a dd e9	: . .
	or a			;2e1c	b7		.
	ld a,(0e9d7h)		;2e1d	3a d7 e9	: . .
	jp z,0e664h		;2e20	ca 64 e6	. d .
	call 0e164h		;2e23	cd 64 e1	. d .
	ld (hl),e		;2e26	73		s
	jp 0e66ch		;2e27	c3 6c e6	. l .
	ld c,a			;2e2a	4f		O
	ld b,000h		;2e2b	06 00		. .
	add hl,bc		;2e2d	09		.
	add hl,bc		;2e2e	09		.
	ld (hl),e		;2e2f	73		s
	inc hl			;2e30	23		#
	ld (hl),d		;2e31	72		r
	ld c,002h		;2e32	0e 02		. .
	ld a,(0df45h)		;2e34	3a 45 df	: E .
	or a			;2e37	b7		.
	ret nz			;2e38	c0		.
	push bc			;2e39	c5		.
	call 0e08ah		;2e3a	cd 8a e0	. . .
	ld a,(0e9d5h)		;2e3d	3a d5 e9	: . .
	dec a			;2e40	3d		=
	dec a			;2e41	3d		=
	jp nz,0e6bbh		;2e42	c2 bb e6	. . .
	pop bc			;2e45	c1		.
	push bc			;2e46	c5		.
	ld a,c			;2e47	79		y
	dec a			;2e48	3d		=
	dec a			;2e49	3d		=
	jp nz,0e6bbh		;2e4a	c2 bb e6	. . .
	push hl			;2e4d	e5		.
	ld hl,(0e9b9h)		;2e4e	2a b9 e9	* . .
	ld d,a			;2e51	57		W
	ld (hl),a		;2e52	77		w
	inc hl			;2e53	23		#
	inc d			;2e54	14		.
	jp p,0e68ch		;2e55	f2 8c e6	. . .
	call 0e1e0h		;2e58	cd e0 e1	. . .
	ld hl,(0e9e7h)		;2e5b	2a e7 e9	* . .
	ld c,002h		;2e5e	0e 02		. .
	ld (0e9e5h),hl		;2e60	22 e5 e9	" . .
	push bc			;2e63	c5		.
	call 0dfd1h		;2e64	cd d1 df	. . .
	pop bc			;2e67	c1		.
	call 0dfb8h		;2e68	cd b8 df	. . .
	ld hl,(0e9e5h)		;2e6b	2a e5 e9	* . .
	ld c,000h		;2e6e	0e 00		. .
	ld a,(0e9c4h)		;2e70	3a c4 e9	: . .
	ld b,a			;2e73	47		G
	and l			;2e74	a5		.
	cp b			;2e75	b8		.
	inc hl			;2e76	23		#
	jp nz,0e69ah		;2e77	c2 9a e6	. . .
	pop hl			;2e7a	e1		.
	ld (0e9e5h),hl		;2e7b	22 e5 e9	" . .
	call 0e1dah		;2e7e	cd da e1	. . .
	call 0dfd1h		;2e81	cd d1 df	. . .
	pop bc			;2e84	c1		.
	push bc			;2e85	c5		.
	call 0dfb8h		;2e86	cd b8 df	. . .
	pop bc			;2e89	c1		.
	ld a,(0e9e3h)		;2e8a	3a e3 e9	: . .
	ld hl,0e9e1h		;2e8d	21 e1 e9	! . .
	cp (hl)			;2e90	be		.
	jp c,0e6d2h		;2e91	da d2 e6	. . .
	ld (hl),a		;2e94	77		w
	inc (hl)		;2e95	34		4
	ld c,002h		;2e96	0e 02		. .
	dec c			;2e98	0d		.
	dec c			;2e99	0d		.
	jp nz,0e6dfh		;2e9a	c2 df e6	. . .
	push af			;2e9d	f5		.
	call 0e169h		;2e9e	cd 69 e1	. i .
	and 07fh		;2ea1	e6 7f		. .
	ld (hl),a		;2ea3	77		w
	pop af			;2ea4	f1		.
	cp 07fh			;2ea5	fe 7f		. .
	jp nz,0e700h		;2ea7	c2 00 e7	. . .
	ld a,(0e9d5h)		;2eaa	3a d5 e9	: . .
	cp 001h			;2ead	fe 01		. .
	jp nz,0e700h		;2eaf	c2 00 e7	. . .
	call 0e0d2h		;2eb2	cd d2 e0	. . .
	call 0e55ah		;2eb5	cd 5a e5	. Z .
	ld hl,0df45h		;2eb8	21 45 df	! E .
	ld a,(hl)		;2ebb	7e		~
	or a			;2ebc	b7		.
	jp nz,0e6feh		;2ebd	c2 fe e6	. . .
	dec a			;2ec0	3d		=
	ld (0e9e3h),a		;2ec1	32 e3 e9	2 . .
	ld (hl),000h		;2ec4	36 00		6 .
	jp 0e0d2h		;2ec6	c3 d2 e0	. . .
	xor a			;2ec9	af		.
	ld (0e9d5h),a		;2eca	32 d5 e9	2 . .
	push bc			;2ecd	c5		.
	ld hl,(0df43h)		;2ece	2a 43 df	* C .
	ex de,hl		;2ed1	eb		.
	ld hl,Rst20Vector+1	;2ed2	21 21 00	! ! .
	add hl,de		;2ed5	19		.
	ld a,(hl)		;2ed6	7e		~
	and 07fh		;2ed7	e6 7f		. .
	push af			;2ed9	f5		.
	ld a,(hl)		;2eda	7e		~
	rla			;2edb	17		.
	inc hl			;2edc	23		#
	ld a,(hl)		;2edd	7e		~
	rla			;2ede	17		.
	and 01fh		;2edf	e6 1f		. .
	ld c,a			;2ee1	4f		O
	ld a,(hl)		;2ee2	7e		~
	rra			;2ee3	1f		.
	rra			;2ee4	1f		.
	rra			;2ee5	1f		.
	rra			;2ee6	1f		.
	and 00fh		;2ee7	e6 0f		. .
	ld b,a			;2ee9	47		G
	pop af			;2eea	f1		.
	inc hl			;2eeb	23		#
	ld l,(hl)		;2eec	6e		n
	inc l			;2eed	2c		,
	dec l			;2eee	2d		-
	ld l,006h		;2eef	2e 06		. .
	jp nz,0e78bh		;2ef1	c2 8b e7	. . .
	ld hl,Rst20Vector	;2ef4	21 20 00	!   .
	add hl,de		;2ef7	19		.
	ld (hl),a		;2ef8	77		w
	ld hl,l000ch		;2ef9	21 0c 00	! . .
	add hl,de		;2efc	19		.
	ld a,c			;2efd	79		y
	sub (hl)		;2efe	96		.
	jp nz,0e747h		;2eff	c2 47 e7	. G .
	ld hl,l000eh		;2f02	21 0e 00	! . .
	add hl,de		;2f05	19		.
	ld a,b			;2f06	78		x
	sub (hl)		;2f07	96		.
	and 07fh		;2f08	e6 7f		. .
	jp z,0e77fh		;2f0a	ca 7f e7	. . .
	push bc			;2f0d	c5		.
	push de			;2f0e	d5		.
	call 0e4a2h		;2f0f	cd a2 e4	. . .
	pop de			;2f12	d1		.
	pop bc			;2f13	c1		.
	ld l,003h		;2f14	2e 03		. .
	ld a,(0df45h)		;2f16	3a 45 df	: E .
	inc a			;2f19	3c		<
	jp z,0e784h		;2f1a	ca 84 e7	. . .
	ld hl,l000ch		;2f1d	21 0c 00	! . .
	add hl,de		;2f20	19		.
	ld (hl),c		;2f21	71		q
	ld hl,l000eh		;2f22	21 0e 00	! . .
	add hl,de		;2f25	19		.
	ld (hl),b		;2f26	70		p
	call 0e451h		;2f27	cd 51 e4	. Q .
	ld a,(0df45h)		;2f2a	3a 45 df	: E .
	inc a			;2f2d	3c		<
	jp nz,0e77fh		;2f2e	c2 7f e7	. . .
	pop bc			;2f31	c1		.
	push bc			;2f32	c5		.
	ld l,004h		;2f33	2e 04		. .
	inc c			;2f35	0c		.
	jp z,0e784h		;2f36	ca 84 e7	. . .
	call 0e524h		;2f39	cd 24 e5	. $ .
	ld l,005h		;2f3c	2e 05		. .
	ld a,(0df45h)		;2f3e	3a 45 df	: E .
	inc a			;2f41	3c		<
	jp z,0e784h		;2f42	ca 84 e7	. . .
	pop bc			;2f45	c1		.
	xor a			;2f46	af		.
	jp 0df01h		;2f47	c3 01 df	. . .
	push hl			;2f4a	e5		.
	call 0e169h		;2f4b	cd 69 e1	. i .
	ld (hl),0c0h		;2f4e	36 c0		6 .
	pop hl			;2f50	e1		.
	pop bc			;2f51	c1		.
	ld a,l			;2f52	7d		}
	ld (0df45h),a		;2f53	32 45 df	2 E .
	jp 0e178h		;2f56	c3 78 e1	. x .
	ld c,0ffh		;2f59	0e ff		. .
	call 0e703h		;2f5b	cd 03 e7	. . .
	call z,0e5c1h		;2f5e	cc c1 e5	. . .
	ret			;2f61	c9		.
	ld c,000h		;2f62	0e 00		. .
	call 0e703h		;2f64	cd 03 e7	. . .
	call z,0e603h		;2f67	cc 03 e6	. . .
	ret			;2f6a	c9		.
	ex de,hl		;2f6b	eb		.
	add hl,de		;2f6c	19		.
	ld c,(hl)		;2f6d	4e		N
	ld b,000h		;2f6e	06 00		. .
	ld hl,l000ch		;2f70	21 0c 00	! . .
	add hl,de		;2f73	19		.
	ld a,(hl)		;2f74	7e		~
	rrca			;2f75	0f		.
	and 080h		;2f76	e6 80		. .
	add a,c			;2f78	81		.
	ld c,a			;2f79	4f		O
	ld a,000h		;2f7a	3e 00		> .
	adc a,b			;2f7c	88		.
	ld b,a			;2f7d	47		G
	ld a,(hl)		;2f7e	7e		~
	rrca			;2f7f	0f		.
	and 00fh		;2f80	e6 0f		. .
	add a,b			;2f82	80		.
	ld b,a			;2f83	47		G
	ld hl,l000eh		;2f84	21 0e 00	! . .
	add hl,de		;2f87	19		.
	ld a,(hl)		;2f88	7e		~
	add a,a			;2f89	87		.
	add a,a			;2f8a	87		.
	add a,a			;2f8b	87		.
	add a,a			;2f8c	87		.
	push af			;2f8d	f5		.
	add a,b			;2f8e	80		.
	ld b,a			;2f8f	47		G
	push af			;2f90	f5		.
	pop hl			;2f91	e1		.
	ld a,l			;2f92	7d		}
	pop hl			;2f93	e1		.
	or l			;2f94	b5		.
	and 001h		;2f95	e6 01		. .
	ret			;2f97	c9		.
	ld c,00ch		;2f98	0e 0c		. .
	call 0e318h		;2f9a	cd 18 e3	. . .
	ld hl,(0df43h)		;2f9d	2a 43 df	* C .
	ld de,Rst20Vector+1	;2fa0	11 21 00	. ! .
	add hl,de		;2fa3	19		.
	push hl			;2fa4	e5		.
	ld (hl),d		;2fa5	72		r
	inc hl			;2fa6	23		#
	ld (hl),d		;2fa7	72		r
	inc hl			;2fa8	23		#
	ld (hl),d		;2fa9	72		r
	call 0e1f5h		;2faa	cd f5 e1	. . .
	jp z,0e80ch		;2fad	ca 0c e8	. . .
	call 0e15eh		;2fb0	cd 5e e1	. ^ .
	ld de,l000fh		;2fb3	11 0f 00	. . .
	call 0e7a5h		;2fb6	cd a5 e7	. . .
	pop hl			;2fb9	e1		.
	push hl			;2fba	e5		.
	ld e,a			;2fbb	5f		_
	ld a,c			;2fbc	79		y
	sub (hl)		;2fbd	96		.
	inc hl			;2fbe	23		#
	ld a,b			;2fbf	78		x
	sbc a,(hl)		;2fc0	9e		.
	inc hl			;2fc1	23		#
	ld a,e			;2fc2	7b		{
	sbc a,(hl)		;2fc3	9e		.
	jp c,0e806h		;2fc4	da 06 e8	. . .
	ld (hl),e		;2fc7	73		s
	dec hl			;2fc8	2b		+
	ld (hl),b		;2fc9	70		p
	dec hl			;2fca	2b		+
	ld (hl),c		;2fcb	71		q
	call 0e32dh		;2fcc	cd 2d e3	. - .
	jp 0e7e4h		;2fcf	c3 e4 e7	. . .
	pop hl			;2fd2	e1		.
	ret			;2fd3	c9		.
	ld hl,(0df43h)		;2fd4	2a 43 df	* C .
	ld de,Rst20Vector	;2fd7	11 20 00	.   .
	call 0e7a5h		;2fda	cd a5 e7	. . .
	ld hl,Rst20Vector+1	;2fdd	21 21 00	! ! .
	add hl,de		;2fe0	19		.
	ld (hl),c		;2fe1	71		q
	inc hl			;2fe2	23		#
	ld (hl),b		;2fe3	70		p
	inc hl			;2fe4	23		#
	ld (hl),a		;2fe5	77		w
	ret			;2fe6	c9		.
	ld hl,(0e9afh)		;2fe7	2a af e9	* . .
	ld a,(0df42h)		;2fea	3a 42 df	: B .
	ld c,a			;2fed	4f		O
	call 0e0eah		;2fee	cd ea e0	. . .
	push hl			;2ff1	e5		.
	ex de,hl		;2ff2	eb		.
	call 0df59h		;2ff3	cd 59 df	. Y .
	pop hl			;2ff6	e1		.
	call z,0df47h		;2ff7	cc 47 df	. G .
	ld a,l			;2ffa	7d		}
	rra			;2ffb	1f		.
	ret c			;2ffc	d8		.
	ld hl,(0e9afh)		;2ffd	2a af e9	* . .
	ld c,l			;3000	4d		M
	ld b,h			;3001	44		D
	call 0e10bh		;3002	cd 0b e1	. . .
	ld (0e9afh),hl		;3005	22 af e9	" . .
	jp 0e2a3h		;3008	c3 a3 e2	. . .
	ld a,(0e9d6h)		;300b	3a d6 e9	: . .
	ld hl,0df42h		;300e	21 42 df	! B .
	cp (hl)			;3011	be		.
	ret z			;3012	c8		.
	ld (hl),a		;3013	77		w
	jp 0e821h		;3014	c3 21 e8	. ! .
	ld a,0ffh		;3017	3e ff		> .
	ld (0e9deh),a		;3019	32 de e9	2 . .
	ld hl,(0df43h)		;301c	2a 43 df	* C .
	ld a,(hl)		;301f	7e		~
	and 01fh		;3020	e6 1f		. .
	dec a			;3022	3d		=
	ld (0e9d6h),a		;3023	32 d6 e9	2 . .
	cp 01eh			;3026	fe 1e		. .
	jp nc,0e875h		;3028	d2 75 e8	. u .
	ld a,(0df42h)		;302b	3a 42 df	: B .
	ld (0e9dfh),a		;302e	32 df e9	2 . .
	ld a,(hl)		;3031	7e		~
	ld (0e9e0h),a		;3032	32 e0 e9	2 . .
	and 0e0h		;3035	e6 e0		. .
l3037h:
	ld (hl),a		;3037	77		w
	call 0e845h		;3038	cd 45 e8	. E .
	ld a,(0df41h)		;303b	3a 41 df	: A .
	ld hl,(0df43h)		;303e	2a 43 df	* C .
	or (hl)			;3041	b6		.
	ld (hl),a		;3042	77		w
	ret			;3043	c9		.
	ld a,022h		;3044	3e 22		> "
	jp 0df01h		;3046	c3 01 df	. . .
	ld hl,WbootStub		;3049	21 00 00	! . .
	ld (0e9adh),hl		;304c	22 ad e9	" . .
	ld (0e9afh),hl		;304f	22 af e9	" . .
	xor a			;3052	af		.
	ld (0df42h),a		;3053	32 42 df	2 B .
	ld hl,l0080h		;3056	21 80 00	! . .
	ld (0e9b1h),hl		;3059	22 b1 e9	" . .
	call 0e1dah		;305c	cd da e1	. . .
	jp 0e821h		;305f	c3 21 e8	. ! .
	call 0e172h		;3062	cd 72 e1	. r .
	call 0e851h		;3065	cd 51 e8	. Q .
	jp 0e451h		;3068	c3 51 e4	. Q .
	call 0e851h		;306b	cd 51 e8	. Q .
	jp 0e4a2h		;306e	c3 a2 e4	. . .
	ld c,000h		;3071	0e 00		. .
	ex de,hl		;3073	eb		.
	ld a,(hl)		;3074	7e		~
	cp 03fh			;3075	fe 3f		. ?
	jp z,0e8c2h		;3077	ca c2 e8	. . .
	call 0e0a6h		;307a	cd a6 e0	. . .
	ld a,(hl)		;307d	7e		~
	cp 03fh			;307e	fe 3f		. ?
	call nz,0e172h		;3080	c4 72 e1	. r .
	call 0e851h		;3083	cd 51 e8	. Q .
	ld c,00fh		;3086	0e 0f		. .
	call 0e318h		;3088	cd 18 e3	. . .
	jp 0e1e9h		;308b	c3 e9 e1	. . .
	ld hl,(0e9d9h)		;308e	2a d9 e9	* . .
	ld (0df43h),hl		;3091	22 43 df	" C .
	call 0e851h		;3094	cd 51 e8	. Q .
	call 0e32dh		;3097	cd 2d e3	. - .
	jp 0e1e9h		;309a	c3 e9 e1	. . .
	call 0e851h		;309d	cd 51 e8	. Q .
	call 0e39ch		;30a0	cd 9c e3	. . .
	jp 0e301h		;30a3	c3 01 e3	. . .
	call 0e851h		;30a6	cd 51 e8	. Q .
	jp 0e5bch		;30a9	c3 bc e5	. . .
	call 0e851h		;30ac	cd 51 e8	. Q .
	jp 0e5feh		;30af	c3 fe e5	. . .
	call 0e172h		;30b2	cd 72 e1	. r .
	call 0e851h		;30b5	cd 51 e8	. Q .
	jp 0e524h		;30b8	c3 24 e5	. $ .
	call 0e851h		;30bb	cd 51 e8	. Q .
	call 0e416h		;30be	cd 16 e4	. . .
	jp 0e301h		;30c1	c3 01 e3	. . .
	ld hl,(0e9afh)		;30c4	2a af e9	* . .
	jp 0e929h		;30c7	c3 29 e9	. ) .
	ld a,(0df42h)		;30ca	3a 42 df	: B .
	jp 0df01h		;30cd	c3 01 df	. . .
	ex de,hl		;30d0	eb		.
	ld (0e9b1h),hl		;30d1	22 b1 e9	" . .
	jp 0e1dah		;30d4	c3 da e1	. . .
	ld hl,(0e9bfh)		;30d7	2a bf e9	* . .
	jp 0e929h		;30da	c3 29 e9	. ) .
	ld hl,(0e9adh)		;30dd	2a ad e9	* . .
	jp 0e929h		;30e0	c3 29 e9	. ) .
	call 0e851h		;30e3	cd 51 e8	. Q .
	call 0e43bh		;30e6	cd 3b e4	. ; .
	jp 0e301h		;30e9	c3 01 e3	. . .
	ld hl,(0e9bbh)		;30ec	2a bb e9	* . .
	ld (0df45h),hl		;30ef	22 45 df	" E .
	ret			;30f2	c9		.
	ld a,(0e9d6h)		;30f3	3a d6 e9	: . .
	cp 0ffh			;30f6	fe ff		. .
	jp nz,0e93bh		;30f8	c2 3b e9	. ; .
	ld a,(0df41h)		;30fb	3a 41 df	: A .
	jp 0df01h		;30fe	c3 01 df	. . .
	and 01fh		;3101	e6 1f		. .
	ld (0df41h),a		;3103	32 41 df	2 A .
	ret			;3106	c9		.
	call 0e851h		;3107	cd 51 e8	. Q .
	jp 0e793h		;310a	c3 93 e7	. . .
	call 0e851h		;310d	cd 51 e8	. Q .
	jp 0e79ch		;3110	c3 9c e7	. . .
	call 0e851h		;3113	cd 51 e8	. Q .
	jp 0e7d2h		;3116	c3 d2 e7	. . .
	ld hl,(0df43h)		;3119	2a 43 df	* C .
	ld a,l			;311c	7d		}
	cpl			;311d	2f		/
	ld e,a			;311e	5f		_
	ld a,h			;311f	7c		|
	cpl			;3120	2f		/
	ld hl,(0e9afh)		;3121	2a af e9	* . .
	and h			;3124	a4		.
	ld d,a			;3125	57		W
	ld a,l			;3126	7d		}
	and e			;3127	a3		.
	ld e,a			;3128	5f		_
	ld hl,(0e9adh)		;3129	2a ad e9	* . .
	ex de,hl		;312c	eb		.
	ld (0e9afh),hl		;312d	22 af e9	" . .
	ld a,l			;3130	7d		}
	and e			;3131	a3		.
	ld l,a			;3132	6f		o
	ld a,h			;3133	7c		|
	and d			;3134	a2		.
	ld h,a			;3135	67		g
	ld (0e9adh),hl		;3136	22 ad e9	" . .
	ret			;3139	c9		.
	ld a,(0e9deh)		;313a	3a de e9	: . .
	or a			;313d	b7		.
	jp z,0e991h		;313e	ca 91 e9	. . .
	ld hl,(0df43h)		;3141	2a 43 df	* C .
	ld (hl),000h		;3144	36 00		6 .
	ld a,(0e9e0h)		;3146	3a e0 e9	: . .
	or a			;3149	b7		.
	jp z,0e991h		;314a	ca 91 e9	. . .
	ld (hl),a		;314d	77		w
	ld a,(0e9dfh)		;314e	3a df e9	: . .
	ld (0e9d6h),a		;3151	32 d6 e9	2 . .
	call 0e845h		;3154	cd 45 e8	. E .
	ld hl,(0df0fh)		;3157	2a 0f df	* . .
	ld sp,hl		;315a	f9		.
	ld hl,(0df45h)		;315b	2a 45 df	* E .
	ld a,l			;315e	7d		}
	ld b,h			;315f	44		D
	ret			;3160	c9		.
	call 0e851h		;3161	cd 51 e8	. Q .
	ld a,002h		;3164	3e 02		> .
	ld (0e9d5h),a		;3166	32 d5 e9	2 . .
	ld c,000h		;3169	0e 00		. .
	call 0e707h		;316b	cd 07 e7	. . .
	call z,0e603h		;316e	cc 03 e6	. . .
	ret			;3171	c9		.
	push hl			;3172	e5		.
	nop			;3173	00		.
	nop			;3174	00		.
	nop			;3175	00		.
	nop			;3176	00		.
	add a,b			;3177	80		.
	nop			;3178	00		.
	nop			;3179	00		.
	nop			;317a	00		.
	nop			;317b	00		.
	nop			;317c	00		.
	nop			;317d	00		.
	nop			;317e	00		.
	nop			;317f	00		.
	nop			;3180	00		.
	nop			;3181	00		.
	nop			;3182	00		.
	nop			;3183	00		.
	nop			;3184	00		.
	nop			;3185	00		.
	nop			;3186	00		.
	nop			;3187	00		.
	nop			;3188	00		.
	nop			;3189	00		.
	nop			;318a	00		.
	nop			;318b	00		.
	nop			;318c	00		.
	nop			;318d	00		.
	nop			;318e	00		.
	nop			;318f	00		.
	nop			;3190	00		.
	nop			;3191	00		.
	nop			;3192	00		.
	nop			;3193	00		.
	nop			;3194	00		.
	nop			;3195	00		.
	nop			;3196	00		.
	nop			;3197	00		.
	nop			;3198	00		.
	nop			;3199	00		.
	nop			;319a	00		.
	nop			;319b	00		.
	nop			;319c	00		.
	nop			;319d	00		.
	nop			;319e	00		.
	nop			;319f	00		.
	nop			;31a0	00		.
	nop			;31a1	00		.
	nop			;31a2	00		.
	nop			;31a3	00		.
	nop			;31a4	00		.
	nop			;31a5	00		.
	nop			;31a6	00		.
	nop			;31a7	00		.
	nop			;31a8	00		.
	nop			;31a9	00		.
	nop			;31aa	00		.
	nop			;31ab	00		.
	nop			;31ac	00		.
	nop			;31ad	00		.
	nop			;31ae	00		.
	nop			;31af	00		.
	nop			;31b0	00		.
	nop			;31b1	00		.
	nop			;31b2	00		.
	nop			;31b3	00		.
	nop			;31b4	00		.
	nop			;31b5	00		.
	nop			;31b6	00		.
	nop			;31b7	00		.
	nop			;31b8	00		.
	nop			;31b9	00		.
	nop			;31ba	00		.
	nop			;31bb	00		.
	nop			;31bc	00		.
	nop			;31bd	00		.
	nop			;31be	00		.
	nop			;31bf	00		.
	nop			;31c0	00		.
	nop			;31c1	00		.
	nop			;31c2	00		.
	nop			;31c3	00		.
	nop			;31c4	00		.
	nop			;31c5	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BdosDispatch - 20 x JP $EAxx, the BDOS function dispatch.
; Lands exactly on the traced BDOS entry $EA00 in the $D400 copy
; (image = runtime - $B83A): the entries target image $3202-$33B5,
; the real BDOS function bodies.  Function 0 (system reset) enters at
; $3202 right behind the table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BdosDispatch:
	jp 0ea77h		;31c6	c3 77 ea	. w .
	jp 0ea96h		;31c9	c3 96 ea	. . .
	jp 0ea3ch		;31cc	c3 3c ea	. < .
	jp 0ea57h		;31cf	c3 57 ea	. W .
	jp 0ea5ah		;31d2	c3 5a ea	. Z .
	jp 0ea6eh		;31d5	c3 6e ea	. n .
	jp 0ea71h		;31d8	c3 71 ea	. q .
	jp 0ea74h		;31db	c3 74 ea	. t .
	jp 0eac0h		;31de	c3 c0 ea	. . .
	jp 0eacch		;31e1	c3 cc ea	. . .
	jp 0eafah		;31e4	c3 fa ea	. . .
	jp 0eb02h		;31e7	c3 02 eb	. . .
	jp 0eb0dh		;31ea	c3 0d eb	. . .
	jp 0eb19h		;31ed	c3 19 eb	. . .
	jp 0eb13h		;31f0	c3 13 eb	. . .
	jp 0ea68h		;31f3	c3 68 ea	. h .
	jp 0eb08h		;31f6	c3 08 eb	. . .
	jp 0ebefh		;31f9	c3 ef eb	. . .
	jp 0ebdfh		;31fc	c3 df eb	. . .
	jp 0ea3fh		;31ff	c3 3f ea	. ? .
	jp 0f812h		;3202	c3 12 f8	. . .
	bit 7,c			;3205	cb 79		. y
	res 7,c			;3207	cb b9		. .
	jp nz,0f830h		;3209	c2 30 f8	. 0 .
	inc c			;320c	0c		.
	dec c			;320d	0d		.
	jr z,l3213h		;320e	28 03		( .
	ld a,0ffh		;3210	3e ff		> .
	ret			;3212	c9		.
l3213h:
	ld de,(0eb3bh)		;3213	ed 5b 3b eb	. [ ; .
	ld (0eb3bh),hl		;3217	22 3b eb	" ; .
	ex de,hl		;321a	eb		.
	xor a			;321b	af		.
	ret			;321c	c9		.
	jp 0f803h		;321d	c3 03 f8	. . .
	jp 0f809h		;3220	c3 09 f8	. . .
	ld a,(hl)		;3223	7e		~
	or a			;3224	b7		.
	ret z			;3225	c8		.
	ld c,a			;3226	4f		O
	call 0ea5ah		;3227	cd 5a ea	. Z .
	inc hl			;322a	23		#
	jp 0ea5dh		;322b	c3 5d ea	. ] .
	in a,(0fbh)		;322e	db fb		. .
l3230h:
	rlca			;3230	07		.
	ccf			;3231	3f		?
	sbc a,a			;3232	9f		.
	ret			;3233	c9		.
	jp 0f80fh		;3234	c3 0f f8	. . .
	jp 0f80ch		;3237	c3 0c f8	. . .
	jp 0f806h		;323a	c3 06 f8	. . .
	ld sp,l0080h		;323d	31 80 00	1 . .
	ei			;3240	fb		.
	call 0ec0ch		;3241	cd 0c ec	. . .
	ld c,000h		;3244	0e 00		. .
	call 0eacch		;3246	cd cc ea	. . .
	xor a			;3249	af		.
	ld (l0001h+2),a		;324a	32 03 00	2 . .
	ld (l0004h),a		;324d	32 04 00	2 . .
	ld (0edddh),a		;3250	32 dd ed	2 . .
	ld hl,0ed2ch		;3253	21 2c ed	! , .
	call 0ea5dh		;3256	cd 5d ea	. ] .
	jp 0ea9fh		;3259	c3 9f ea	. . .
	ld sp,l0080h		;325c	31 80 00	1 . .
	ei			;325f	fb		.
	ld c,001h		;3260	0e 01		. .
	call 0f830h		;3262	cd 30 f8	. 0 .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BdosOpenCore - open/directory-search core.
; Traced at runtime $EA9F-$EAB6 during FCB OPEN processing; the
; return-path pieces $EB0D-$EB12 marshal the result back to the
; caller's TPA (../monitor-rom.md, "BDOS Implementation").
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BdosOpenCore:
	ld a,0c3h		;3265	3e c3		> .
	ld (WbootStub),a	;3267	32 00 00	2 . .
	ld hl,0ea03h		;326a	21 03 ea	! . .
	ld (l0001h),hl		;326d	22 01 00	" . .
	ld (BdosEntry),a	;3270	32 05 00	2 . .
	ld hl,0dc06h		;3273	21 06 dc	! . .
	ld (l0006h),hl		;3276	22 06 00	" . .
	ld bc,l0080h		;3279	01 80 00	. . .
	call 0eb0dh		;327c	cd 0d eb	. . .
	ld a,(l0004h)		;327f	3a 04 00	: . .
	ld c,a			;3282	4f		O
	jp 0d400h		;3283	c3 00 d4	. . .
	ld hl,WbootStub		;3286	21 00 00	! . .
	ld (0edd7h),hl		;3289	22 d7 ed	" . .
	ld bc,l00ffh+1		;328c	01 00 01	. . .
	jp 0eb27h		;328f	c3 27 eb	. ' .
	ld a,c			;3292	79		y
	cp 00ah			;3293	fe 0a		. .
	jp nc,0eaddh		;3295	d2 dd ea	. . .
	ld hl,0eddeh		;3298	21 de ed	! . .
	ld b,000h		;329b	06 00		. .
	add hl,bc		;329d	09		.
	ld a,(hl)		;329e	7e		~
	or a			;329f	b7		.
	jp p,0eae7h		;32a0	f2 e7 ea	. . .
	ld hl,WbootStub		;32a3	21 00 00	! . .
	ld a,(0edddh)		;32a6	3a dd ed	: . .
	ld (l0004h),a		;32a9	32 04 00	2 . .
	ret			;32ac	c9		.
	ld (0edd4h),a		;32ad	32 d4 ed	2 . .
	ld a,c			;32b0	79		y
	ld (0edddh),a		;32b1	32 dd ed	2 . .
	ld l,c			;32b4	69		i
	ld h,000h		;32b5	26 00		& .
	add hl,hl		;32b7	29		)
	add hl,hl		;32b8	29		)
	add hl,hl		;32b9	29		)
	add hl,hl		;32ba	29		)
	ld de,0ede8h		;32bb	11 e8 ed	. . .
	add hl,de		;32be	19		.
	ret			;32bf	c9		.
	ld hl,0edd7h		;32c0	21 d7 ed	! . .
	ld (hl),c		;32c3	71		q
	inc hl			;32c4	23		#
	ld (hl),b		;32c5	70		p
	xor a			;32c6	af		.
	ret			;32c7	c9		.
	ld a,c			;32c8	79		y
	ld (0edd9h),a		;32c9	32 d9 ed	2 . .
	xor a			;32cc	af		.
	ret			;32cd	c9		.
	ld hl,l0001h		;32ce	21 01 00	! . .
	add hl,bc		;32d1	09		.
	ret			;32d2	c9		.
	ld l,c			;32d3	69		i
	ld h,b			;32d4	60		`
	ld (0eddbh),hl		;32d5	22 db ed	" . .
	ret			;32d8	c9		.
	ld bc,00601h		;32d9	01 01 06	. . .
	jp 0eb27h		;32dc	c3 27 eb	. ' .
	ld bc,00401h		;32df	01 01 04	. . .
	call 0eb27h		;32e2	cd 27 eb	. ' .
	ret			;32e5	c9		.
	ex (sp),hl		;32e6	e3		.
	call 0ea5dh		;32e7	cd 5d ea	. ] .
	inc hl			;32ea	23		#
	ex (sp),hl		;32eb	e3		.
	ret			;32ec	c9		.
	ld hl,0edd5h		;32ed	21 d5 ed	! . .
	ld (hl),b		;32f0	70		p
	inc hl			;32f1	23		#
	ld (hl),c		;32f2	71		q
l32f3h:
	ld de,0edd4h		;32f3	11 d4 ed	. . .
	ld a,(de)		;32f6	1a		.
	ld c,a			;32f7	4f		O
	call 0ebdbh		;32f8	cd db eb	. . .
	or a			;32fb	b7		.
	ret z			;32fc	c8		.
	ld bc,0edd4h		;32fd	01 d4 ed	. . .
	call 0eb5ch		;3300	cd 5c eb	. \ .
	ld a,c			;3303	79		y
	or a			;3304	b7		.
	jp z,0ea03h		;3305	ca 03 ea	. . .
	dec a			;3308	3d		=
	jr z,l330dh		;3309	28 02		( .
	dec a			;330b	3d		=
	ret			;330c	c9		.
l330dh:
	ld hl,0edd5h		;330d	21 d5 ed	! . .
	ld a,(hl)		;3310	7e		~
	push af			;3311	f5		.
	ld (hl),003h		;3312	36 03		6 .
	ld de,0edd4h		;3314	11 d4 ed	. . .
	ld a,(de)		;3317	1a		.
	ld c,a			;3318	4f		O
	call 0ebdbh		;3319	cd db eb	. . .
	pop af			;331c	f1		.
	ld (0edd5h),a		;331d	32 d5 ed	2 . .
	jr l32f3h		;3320	18 d1		. .
	push af			;3322	f5		.
	ld hl,0ed90h		;3323	21 90 ed	! . .
	call 0ea5dh		;3326	cd 5d ea	. ] .
	pop af			;3329	f1		.
	call 0ebaeh		;332a	cd ae eb	. . .
	ld hl,0ed9eh		;332d	21 9e ed	! . .
	call 0ea5dh		;3330	cd 5d ea	. ] .
	ld a,(0edd4h)		;3333	3a d4 ed	: . .
	call 0ebaeh		;3336	cd ae eb	. . .
	ld a,(0edd5h)		;3339	3a d5 ed	: . .
	call 0ebaeh		;333c	cd ae eb	. . .
	ld a,(0edd7h)		;333f	3a d7 ed	: . .
	call 0ebaeh		;3342	cd ae eb	. . .
	ld a,(0edd9h)		;3345	3a d9 ed	: . .
	call 0ebaeh		;3348	cd ae eb	. . .
l334bh:
	ld hl,0eda3h		;334b	21 a3 ed	! . .
	call 0ea5dh		;334e	cd 5d ea	. ] .
	call 0ea57h		;3351	cd 57 ea	. W .
	cp 003h			;3354	fe 03		. .
	jr z,l336ah		;3356	28 12		( .
	call 0ebd5h		;3358	cd d5 eb	. . .
	ld c,a			;335b	4f		O
	call 0ea5ah		;335c	cd 5a ea	. Z .
	ld hl,0ebaah		;335f	21 aa eb	! . .
	ld bc,l0004h		;3362	01 04 00	. . .
	cpir			;3365	ed b1		. .
	jr nz,l334bh		;3367	20 e2		  .
	ret			;3369	c9		.
l336ah:
	xor a			;336a	af		.
	ld (l0004h),a		;336b	32 04 00	2 . .
	ld c,a			;336e	4f		O
	ret			;336f	c9		.
	ld b,(hl)		;3370	46		F
	ld c,c			;3371	49		I
	ld d,d			;3372	52		R
	ld b,c			;3373	41		A
	push af			;3374	f5		.
	rrca			;3375	0f		.
	rrca			;3376	0f		.
	rrca			;3377	0f		.
	rrca			;3378	0f		.
	call 0ebc9h		;3379	cd c9 eb	. . .
	pop af			;337c	f1		.
	call 0ebc9h		;337d	cd c9 eb	. . .
	ld c,03ah		;3380	0e 3a		. :
	jp 0ea5ah		;3382	c3 5a ea	. Z .
	ld a,(hl)		;3385	7e		~
	call 0ebaeh		;3386	cd ae eb	. . .
	inc hl			;3389	23		#
	dec b			;338a	05		.
	jp nz,0ebbfh		;338b	c2 bf eb	. . .
	ret			;338e	c9		.
	and 00fh		;338f	e6 0f		. .
	add a,090h		;3391	c6 90		. .
	daa			;3393	27		'
	adc a,040h		;3394	ce 40		. @
	daa			;3396	27		'
	ld c,a			;3397	4f		O
	jp 0ea5ah		;3398	c3 5a ea	. Z .
	cp 060h			;339b	fe 60		. `
	ret c			;339d	d8		.
	sub 020h		;339e	d6 20		.  
	ret			;33a0	c9		.
	ld a,c			;33a1	79		y
	jp 0f824h		;33a2	c3 24 f8	. $ .
	ld hl,0eddeh		;33a5	21 de ed	! . .
	ld b,000h		;33a8	06 00		. .
	add hl,bc		;33aa	09		.
	ld a,(hl)		;33ab	7e		~
	or a			;33ac	b7		.
	ret m			;33ad	f8		.
	ld c,a			;33ae	4f		O
	push bc			;33af	c5		.
	call 0f82dh		;33b0	cd 2d f8	. - .
	pop bc			;33b3	c1		.
	ret			;33b4	c9		.
	push bc			;33b5	c5		.
	push de			;33b6	d5		.
	ld a,d			;33b7	7a		z
	or e			;33b8	b3		.
	call nz,0f82ah		;33b9	c4 2a f8	. * .
	pop de			;33bc	d1		.
	pop bc			;33bd	c1		.
	or a			;33be	b7		.
	ret nz			;33bf	c0		.
	ld hl,0eddeh		;33c0	21 de ed	! . .
	ld a,c			;33c3	79		y
	bit 7,b			;33c4	cb 78		. x
	res 7,b			;33c6	cb b8		. .
	ld c,b			;33c8	48		H
	ld b,000h		;33c9	06 00		. .
	add hl,bc		;33cb	09		.
	ld (hl),a		;33cc	77		w
	rla			;33cd	17		.
	ld a,000h		;33ce	3e 00		> .
	ret nz			;33d0	c0		.
	ret c			;33d1	d8		.
	ld hl,0efcah		;33d2	21 ca ef	! . .
	ld (0ef20h),hl		;33d5	22 20 ef	"   .
	ld c,000h		;33d8	0e 00		. .
	ld hl,0eddeh		;33da	21 de ed	! . .
	ld (0ef22h),hl		;33dd	22 22 ef	" " .
	ld hl,0ede8h		;33e0	21 e8 ed	! . .
	ld (0ef24h),hl		;33e3	22 24 ef	" $ .
	push bc			;33e6	c5		.
	ld hl,(0ef22h)		;33e7	2a 22 ef	* " .
	ld a,(hl)		;33ea	7e		~
	or a			;33eb	b7		.
	jp m,0ed11h		;33ec	fa 11 ed	. . .
	ld c,a			;33ef	4f		O
	ld de,0ef28h		;33f0	11 28 ef	. ( .
	call 0f82dh		;33f3	cd 2d f8	. - .
	jp z,0ec38h		;33f6	ca 38 ec	. 8 .
	ld (hl),0ffh		;33f9	36 ff		6 .
	jp 0ed11h		;33fb	c3 11 ed	. . .
	ld hl,(0ef24h)		;33fe	2a 24 ef	* $ .
	ld c,008h		;3401	0e 08		. .
	xor a			;3403	af		.
	ld (hl),a		;3404	77		w
	inc hl			;3405	23		#
	dec c			;3406	0d		.
	jp nz,0ec3eh		;3407	c2 3e ec	. > .
	inc hl			;340a	23		#
	inc hl			;340b	23		#
	ld e,(hl)		;340c	5e		^
	inc hl			;340d	23		#
	ld d,(hl)		;340e	56		V
	inc hl			;340f	23		#
	push hl			;3410	e5		.
	ld hl,(0ef37h)		;3411	2a 37 ef	* 7 .
	ex de,hl		;3414	eb		.
	ld (hl),e		;3415	73		s
	inc hl			;3416	23		#
	ld (hl),d		;3417	72		r
	push hl			;3418	e5		.
	ld hl,WbootStub		;3419	21 00 00	! . .
	ld c,000h		;341c	0e 00		. .
	ld a,(0ef32h)		;341e	3a 32 ef	: 2 .
	or a			;3421	b7		.
	jp z,0ec65h		;3422	ca 65 ec	. e .
	add hl,de		;3425	19		.
	inc c			;3426	0c		.
	dec a			;3427	3d		=
	jp nz,0ec5fh		;3428	c2 5f ec	. _ .
	ex de,hl		;342b	eb		.
	pop hl			;342c	e1		.
	ld a,c			;342d	79		y
	ld bc,l000ch		;342e	01 0c 00	. . .
	add hl,bc		;3431	09		.
	ld (hl),a		;3432	77		w
	push hl			;3433	e5		.
	ld hl,(0ef35h)		;3434	2a 35 ef	* 5 .
	ld a,l			;3437	7d		}
	sub e			;3438	93		.
	ld l,a			;3439	6f		o
	ld a,h			;343a	7c		|
	sbc a,d			;343b	9a		.
	ld h,a			;343c	67		g
	ld a,l			;343d	7d		}
	rrca			;343e	0f		.
	rrca			;343f	0f		.
	rrca			;3440	0f		.
	rrca			;3441	0f		.
	and 00fh		;3442	e6 0f		. .
	ld l,a			;3444	6f		o
	ld a,h			;3445	7c		|
	rrca			;3446	0f		.
	rrca			;3447	0f		.
	rrca			;3448	0f		.
	rrca			;3449	0f		.
	ld h,a			;344a	67		g
	and 0f0h		;344b	e6 f0		. .
	or l			;344d	b5		.
	ld l,a			;344e	6f		o
	ld a,h			;344f	7c		|
	and 00fh		;3450	e6 0f		. .
	ld h,a			;3452	67		g
	dec hl			;3453	2b		+
	ex de,hl		;3454	eb		.
	pop hl			;3455	e1		.
	ld bc,0fff8h		;3456	01 f8 ff	. . .
	add hl,bc		;3459	09		.
	ld (hl),e		;345a	73		s
	inc hl			;345b	23		#
	ld (hl),d		;345c	72		r
	dec hl			;345d	2b		+
	dec hl			;345e	2b		+
	ld (hl),000h		;345f	36 00		6 .
	ld a,d			;3461	7a		z
	or a			;3462	b7		.
	jp nz,0eca1h		;3463	c2 a1 ec	. . .
	inc (hl)		;3466	34		4
	xor a			;3467	af		.
	ex de,hl		;3468	eb		.
	add hl,hl		;3469	29		)
	rla			;346a	17		.
	add hl,hl		;346b	29		)
	rla			;346c	17		.
	add hl,hl		;346d	29		)
	rla			;346e	17		.
	add hl,hl		;346f	29		)
	rla			;3470	17		.
	add hl,hl		;3471	29		)
	rla			;3472	17		.
	ld l,h			;3473	6c		l
	ld h,a			;3474	67		g
	inc hl			;3475	23		#
	ld (0ef26h),hl		;3476	22 26 ef	" & .
	ex de,hl		;3479	eb		.
	push hl			;347a	e5		.
	ld hl,(0ef3ch)		;347b	2a 3c ef	* < .
	dec hl			;347e	2b		+
	ex de,hl		;347f	eb		.
	pop hl			;3480	e1		.
	inc hl			;3481	23		#
	inc hl			;3482	23		#
	inc hl			;3483	23		#
	ld (hl),e		;3484	73		s
	inc hl			;3485	23		#
	ld (hl),d		;3486	72		r
	push hl			;3487	e5		.
	ex de,hl		;3488	eb		.
	add hl,hl		;3489	29		)
	add hl,hl		;348a	29		)
	ld de,WbootStub		;348b	11 00 00	. . .
	inc h			;348e	24		$
	scf			;348f	37		7
	ld a,d			;3490	7a		z
	rra			;3491	1f		.
	ld d,a			;3492	57		W
	ld a,e			;3493	7b		{
	rra			;3494	1f		.
	ld e,a			;3495	5f		_
	dec h			;3496	25		%
	jp nz,0ecc9h		;3497	c2 c9 ec	. . .
	pop hl			;349a	e1		.
	inc hl			;349b	23		#
	ld (hl),d		;349c	72		r
	inc hl			;349d	23		#
	ld (hl),e		;349e	73		s
	inc hl			;349f	23		#
	ex de,hl		;34a0	eb		.
	ld a,(0ef2ch)		;34a1	3a 2c ef	: , .
	or a			;34a4	b7		.
	ld hl,WbootStub		;34a5	21 00 00	! . .
	jp z,0ecf6h		;34a8	ca f6 ec	. . .
	ld hl,(0ef3ch)		;34ab	2a 3c ef	* < .
	xor a			;34ae	af		.
	ld a,h			;34af	7c		|
	rra			;34b0	1f		.
	ld h,a			;34b1	67		g
	ld a,l			;34b2	7d		}
	rra			;34b3	1f		.
	ld l,a			;34b4	6f		o
	xor a			;34b5	af		.
	ld a,h			;34b6	7c		|
	rra			;34b7	1f		.
	ld h,a			;34b8	67		g
	ld a,l			;34b9	7d		}
	rra			;34ba	1f		.
	ld l,a			;34bb	6f		o
	ex de,hl		;34bc	eb		.
	ld (hl),e		;34bd	73		s
	inc hl			;34be	23		#
	ld (hl),d		;34bf	72		r
	ld hl,(0ef20h)		;34c0	2a 20 ef	*   .
	ex de,hl		;34c3	eb		.
	ex (sp),hl		;34c4	e3		.
	ld (hl),e		;34c5	73		s
	inc hl			;34c6	23		#
	ld (hl),d		;34c7	72		r
	inc hl			;34c8	23		#
	ex (sp),hl		;34c9	e3		.
	add hl,de		;34ca	19		.
	pop de			;34cb	d1		.
	ex de,hl		;34cc	eb		.
	ld (hl),e		;34cd	73		s
	inc hl			;34ce	23		#
	ld (hl),d		;34cf	72		r
	ld hl,(0ef26h)		;34d0	2a 26 ef	* & .
	add hl,de		;34d3	19		.
	ld (0ef20h),hl		;34d4	22 20 ef	"   .
	ld hl,(0ef22h)		;34d7	2a 22 ef	* " .
	inc hl			;34da	23		#
	ld (0ef22h),hl		;34db	22 22 ef	" " .
	ld hl,(0ef24h)		;34de	2a 24 ef	* $ .
	ld bc,Rst10Vector	;34e1	01 10 00	. . .
	add hl,bc		;34e4	09		.
	ld (0ef24h),hl		;34e5	22 24 ef	" $ .
	pop bc			;34e8	c1		.
	inc c			;34e9	0c		.
	ld a,c			;34ea	79		y
	cp 00ah			;34eb	fe 0a		. .
	jp nz,0ec20h		;34ed	c2 20 ec	.   .
	xor a			;34f0	af		.
	ret			;34f1	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BiosBanners - CP/M V2.2 / XVR BIOS V1.07.13 sign-ons,
; "BIOS ERROR" and the RETRY/IGNORE/ABORT/FAIL select prompt.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'BiosBanners' (start 0x34f2 end 0x359a)
BiosBanners_start:
	defb 043h		;34f2	43		C
	defb 050h		;34f3	50		P
	defb 02fh		;34f4	2f		/
	defb 04dh		;34f5	4d		M
	defb 020h		;34f6	20		 
	defb 020h		;34f7	20		 
	defb 056h		;34f8	56		V
	defb 032h		;34f9	32		2
	defb 02eh		;34fa	2e		.
	defb 032h		;34fb	32		2
	defb 020h		;34fc	20		 
	defb 020h		;34fd	20		 
	defb 020h		;34fe	20		 
	defb 020h		;34ff	20		 
	defb 020h		;3500	20		 
	defb 043h		;3501	43		C
	defb 04fh		;3502	4f		O
	defb 050h		;3503	50		P
	defb 059h		;3504	59		Y
	defb 052h		;3505	52		R
	defb 049h		;3506	49		I
	defb 047h		;3507	47		G
	defb 048h		;3508	48		H
	defb 054h		;3509	54		T
	defb 028h		;350a	28		(
	defb 043h		;350b	43		C
	defb 029h		;350c	29		)
	defb 020h		;350d	20		 
	defb 020h		;350e	20		 
	defb 042h		;350f	42		B
	defb 059h		;3510	59		Y
	defb 020h		;3511	20		 
	defb 020h		;3512	20		 
	defb 044h		;3513	44		D
	defb 049h		;3514	49		I
	defb 047h		;3515	47		G
	defb 049h		;3516	49		I
	defb 054h		;3517	54		T
	defb 041h		;3518	41		A
	defb 04ch		;3519	4c		L
	defb 020h		;351a	20		 
	defb 052h		;351b	52		R
	defb 045h		;351c	45		E
	defb 053h		;351d	53		S
	defb 045h		;351e	45		E
	defb 041h		;351f	41		A
	defb 052h		;3520	52		R
	defb 043h		;3521	43		C
	defb 048h		;3522	48		H
	defb 00dh		;3523	0d		.
	defb 00ah		;3524	0a		.
	defb 042h		;3525	42		B
	defb 049h		;3526	49		I
	defb 04fh		;3527	4f		O
	defb 053h		;3528	53		S
	defb 020h		;3529	20		 
	defb 020h		;352a	20		 
	defb 056h		;352b	56		V
	defb 031h		;352c	31		1
	defb 02eh		;352d	2e		.
	defb 030h		;352e	30		0
	defb 037h		;352f	37		7
	defb 02eh		;3530	2e		.
	defb 031h		;3531	31		1
	defb 033h		;3532	33		3
	defb 020h		;3533	20		 
	defb 043h		;3534	43		C
	defb 04fh		;3535	4f		O
	defb 050h		;3536	50		P
	defb 059h		;3537	59		Y
	defb 052h		;3538	52		R
	defb 049h		;3539	49		I
	defb 047h		;353a	47		G
	defb 048h		;353b	48		H
	defb 054h		;353c	54		T
	defb 028h		;353d	28		(
	defb 043h		;353e	43		C
	defb 029h		;353f	29		)
	defb 020h		;3540	20		 
	defb 020h		;3541	20		 
	defb 042h		;3542	42		B
	defb 059h		;3543	59		Y
	defb 020h		;3544	20		 
	defb 020h		;3545	20		 
	defb 058h		;3546	58		X
	defb 056h		;3547	56		V
	defb 052h		;3548	52		R
	defb 020h		;3549	20		 
	defb 02dh		;354a	2d		-
	defb 020h		;354b	20		 
	defb 050h		;354c	50		P
	defb 052h		;354d	52		R
	defb 04fh		;354e	4f		O
	defb 044h		;354f	44		D
	defb 055h		;3550	55		U
	defb 043h		;3551	43		C
	defb 054h		;3552	54		T
	defb 00dh		;3553	0d		.
	defb 00ah		;3554	0a		.
	defb 000h		;3555	00		.
	defb 00dh		;3556	0d		.
	defb 00ah		;3557	0a		.
	defb 042h		;3558	42		B
	defb 049h		;3559	49		I
	defb 04fh		;355a	4f		O
	defb 053h		;355b	53		S
	defb 020h		;355c	20		 
	defb 045h		;355d	45		E
	defb 052h		;355e	52		R
	defb 052h		;355f	52		R
	defb 04fh		;3560	4f		O
	defb 052h		;3561	52		R
	defb 020h		;3562	20		 
	defb 000h		;3563	00		.
	defb 020h		;3564	20		 
	defb 041h		;3565	41		A
	defb 054h		;3566	54		T
	defb 020h		;3567	20		 
	defb 000h		;3568	00		.
	defb 00dh		;3569	0d		.
	defb 00ah		;356a	0a		.
	defb 020h		;356b	20		 
	defb 053h		;356c	53		S
	defb 045h		;356d	45		E
	defb 04ch		;356e	4c		L
	defb 045h		;356f	45		E
	defb 043h		;3570	43		C
	defb 054h		;3571	54		T
	defb 020h		;3572	20		 
	defb 028h		;3573	28		(
	defb 028h		;3574	28		(
	defb 052h		;3575	52		R
	defb 029h		;3576	29		)
	defb 045h		;3577	45		E
	defb 054h		;3578	54		T
	defb 052h		;3579	52		R
	defb 059h		;357a	59		Y
	defb 02ch		;357b	2c		,
	defb 020h		;357c	20		 
	defb 028h		;357d	28		(
	defb 049h		;357e	49		I
	defb 029h		;357f	29		)
	defb 047h		;3580	47		G
	defb 04eh		;3581	4e		N
	defb 04fh		;3582	4f		O
	defb 052h		;3583	52		R
	defb 045h		;3584	45		E
	defb 02ch		;3585	2c		,
	defb 020h		;3586	20		 
	defb 028h		;3587	28		(
	defb 041h		;3588	41		A
	defb 029h		;3589	29		)
	defb 042h		;358a	42		B
	defb 04fh		;358b	4f		O
	defb 052h		;358c	52		R
	defb 054h		;358d	54		T
	defb 02ch		;358e	2c		,
	defb 020h		;358f	20		 
	defb 028h		;3590	28		(
	defb 046h		;3591	46		F
	defb 029h		;3592	29		)
	defb 041h		;3593	41		A
	defb 049h		;3594	49		I
	defb 04ch		;3595	4c		L
	defb 029h		;3596	29		)
	defb 020h		;3597	20		 
	defb 03ah		;3598	3a		:
	defb 000h		;3599	00		.
BiosBanners_end:
	ld bc,0e502h		;359a	01 02 e5	. . .
	ld bc,WbootStub		;359d	01 00 00	. . .
	nop			;35a0	00		.
	add a,b			;35a1	80		.
	nop			;35a2	00		.
	nop			;35a3	00		.
	ld bc,l0200h		;35a4	01 00 02	. . .
	rst 38h			;35a7	ff		.
	rst 38h			;35a8	ff		.
	rst 38h			;35a9	ff		.
	rst 38h			;35aa	ff		.
	rst 38h			;35ab	ff		.
	rst 38h			;35ac	ff		.
	rst 38h			;35ad	ff		.
	nop			;35ae	00		.
	nop			;35af	00		.
	nop			;35b0	00		.
	nop			;35b1	00		.
	nop			;35b2	00		.
	nop			;35b3	00		.
	nop			;35b4	00		.
	nop			;35b5	00		.
	ld c,d			;35b6	4a		J
	rst 28h			;35b7	ef		.
	adc a,b			;35b8	88		.
	xor 000h		;35b9	ee 00		. .
	nop			;35bb	00		.
	nop			;35bc	00		.
	nop			;35bd	00		.
	nop			;35be	00		.
	nop			;35bf	00		.
	nop			;35c0	00		.
	nop			;35c1	00		.
	nop			;35c2	00		.
	nop			;35c3	00		.
	nop			;35c4	00		.
	nop			;35c5	00		.
	ld c,d			;35c6	4a		J
	rst 28h			;35c7	ef		.
	sub a			;35c8	97		.
	xor 000h		;35c9	ee 00		. .
	nop			;35cb	00		.
	nop			;35cc	00		.
	nop			;35cd	00		.
	nop			;35ce	00		.
	nop			;35cf	00		.
	nop			;35d0	00		.
	nop			;35d1	00		.
	nop			;35d2	00		.
	nop			;35d3	00		.
	nop			;35d4	00		.
	nop			;35d5	00		.
	ld c,d			;35d6	4a		J
	rst 28h			;35d7	ef		.
	and (hl)		;35d8	a6		.
	xor 000h		;35d9	ee 00		. .
	nop			;35db	00		.
	nop			;35dc	00		.
	nop			;35dd	00		.
	nop			;35de	00		.
	nop			;35df	00		.
	nop			;35e0	00		.
	nop			;35e1	00		.
	nop			;35e2	00		.
	nop			;35e3	00		.
	nop			;35e4	00		.
	nop			;35e5	00		.
	ld c,d			;35e6	4a		J
	rst 28h			;35e7	ef		.
	or l			;35e8	b5		.
	xor 000h		;35e9	ee 00		. .
	nop			;35eb	00		.
	nop			;35ec	00		.
	nop			;35ed	00		.
	nop			;35ee	00		.
	nop			;35ef	00		.
	nop			;35f0	00		.
	nop			;35f1	00		.
	nop			;35f2	00		.
	nop			;35f3	00		.
	nop			;35f4	00		.
	nop			;35f5	00		.
	ld c,d			;35f6	4a		J
	rst 28h			;35f7	ef		.
	call nz,000eeh		;35f8	c4 ee 00	. . .
	nop			;35fb	00		.
	nop			;35fc	00		.
	nop			;35fd	00		.
	nop			;35fe	00		.
	nop			;35ff	00		.
	nop			;3600	00		.
	nop			;3601	00		.
	nop			;3602	00		.
	nop			;3603	00		.
	nop			;3604	00		.
	nop			;3605	00		.
	ld c,d			;3606	4a		J
	rst 28h			;3607	ef		.
	out (0eeh),a		;3608	d3 ee		. .
	nop			;360a	00		.
	nop			;360b	00		.
	nop			;360c	00		.
	nop			;360d	00		.
	nop			;360e	00		.
	nop			;360f	00		.
	nop			;3610	00		.
	nop			;3611	00		.
	nop			;3612	00		.
	nop			;3613	00		.
	nop			;3614	00		.
	nop			;3615	00		.
	ld c,d			;3616	4a		J
	rst 28h			;3617	ef		.
	jp po,000eeh		;3618	e2 ee 00	. . .
	nop			;361b	00		.
	nop			;361c	00		.
	nop			;361d	00		.
	nop			;361e	00		.
	nop			;361f	00		.
	nop			;3620	00		.
	nop			;3621	00		.
	nop			;3622	00		.
	nop			;3623	00		.
	nop			;3624	00		.
	nop			;3625	00		.
	ld c,d			;3626	4a		J
	rst 28h			;3627	ef		.
	pop af			;3628	f1		.
	xor 000h		;3629	ee 00		. .
	nop			;362b	00		.
	nop			;362c	00		.
	nop			;362d	00		.
	nop			;362e	00		.
	nop			;362f	00		.
	nop			;3630	00		.
	nop			;3631	00		.
	nop			;3632	00		.
	nop			;3633	00		.
	nop			;3634	00		.
	nop			;3635	00		.
	ld c,d			;3636	4a		J
	rst 28h			;3637	ef		.
	nop			;3638	00		.
	rst 28h			;3639	ef		.
	nop			;363a	00		.
	nop			;363b	00		.
	nop			;363c	00		.
	nop			;363d	00		.
	nop			;363e	00		.
	nop			;363f	00		.
	nop			;3640	00		.
	nop			;3641	00		.
	nop			;3642	00		.
	nop			;3643	00		.
	nop			;3644	00		.
	nop			;3645	00		.
	ld c,d			;3646	4a		J
	rst 28h			;3647	ef		.
	rrca			;3648	0f		.
	rst 28h			;3649	ef		.
	nop			;364a	00		.
	nop			;364b	00		.
	nop			;364c	00		.
	nop			;364d	00		.
	nop			;364e	00		.
	nop			;364f	00		.
	inc b			;3650	04		.
	rrca			;3651	0f		.
	nop			;3652	00		.
	nop			;3653	00		.
	nop			;3654	00		.
	nop			;3655	00		.
	nop			;3656	00		.
	nop			;3657	00		.
	nop			;3658	00		.
	nop			;3659	00		.
	nop			;365a	00		.
	nop			;365b	00		.
	nop			;365c	00		.
	nop			;365d	00		.
	nop			;365e	00		.
	inc b			;365f	04		.
	rrca			;3660	0f		.
	nop			;3661	00		.
	nop			;3662	00		.
	nop			;3663	00		.
	nop			;3664	00		.
	nop			;3665	00		.
	nop			;3666	00		.
	nop			;3667	00		.
	nop			;3668	00		.
	nop			;3669	00		.
	nop			;366a	00		.
	nop			;366b	00		.
	nop			;366c	00		.
	nop			;366d	00		.
	inc b			;366e	04		.
	rrca			;366f	0f		.
	nop			;3670	00		.
	nop			;3671	00		.
	nop			;3672	00		.
	nop			;3673	00		.
	nop			;3674	00		.
	nop			;3675	00		.
	nop			;3676	00		.
	nop			;3677	00		.
	nop			;3678	00		.
	nop			;3679	00		.
	nop			;367a	00		.
	nop			;367b	00		.
	nop			;367c	00		.
	inc b			;367d	04		.
	rrca			;367e	0f		.
	nop			;367f	00		.
	nop			;3680	00		.
	nop			;3681	00		.
	nop			;3682	00		.
	nop			;3683	00		.
	nop			;3684	00		.
	nop			;3685	00		.
	nop			;3686	00		.
	nop			;3687	00		.
	nop			;3688	00		.
	nop			;3689	00		.
	nop			;368a	00		.
	nop			;368b	00		.
	inc b			;368c	04		.
	rrca			;368d	0f		.
	nop			;368e	00		.
	nop			;368f	00		.
	nop			;3690	00		.
	nop			;3691	00		.
	nop			;3692	00		.
	nop			;3693	00		.
	nop			;3694	00		.
	nop			;3695	00		.
	nop			;3696	00		.
	nop			;3697	00		.
	nop			;3698	00		.
	nop			;3699	00		.
	nop			;369a	00		.
	inc b			;369b	04		.
	rrca			;369c	0f		.
	nop			;369d	00		.
	nop			;369e	00		.
	nop			;369f	00		.
	nop			;36a0	00		.
	nop			;36a1	00		.
	nop			;36a2	00		.
	nop			;36a3	00		.
	nop			;36a4	00		.
	nop			;36a5	00		.
	nop			;36a6	00		.
	nop			;36a7	00		.
	nop			;36a8	00		.
	nop			;36a9	00		.
	inc b			;36aa	04		.
	rrca			;36ab	0f		.
	nop			;36ac	00		.
	nop			;36ad	00		.
	nop			;36ae	00		.
	nop			;36af	00		.
	nop			;36b0	00		.
	nop			;36b1	00		.
	nop			;36b2	00		.
	nop			;36b3	00		.
	nop			;36b4	00		.
	nop			;36b5	00		.
	nop			;36b6	00		.
	nop			;36b7	00		.
	nop			;36b8	00		.
	inc b			;36b9	04		.
	rrca			;36ba	0f		.
	nop			;36bb	00		.
	nop			;36bc	00		.
	nop			;36bd	00		.
	nop			;36be	00		.
	nop			;36bf	00		.
	nop			;36c0	00		.
	nop			;36c1	00		.
	nop			;36c2	00		.
	nop			;36c3	00		.
	nop			;36c4	00		.
	nop			;36c5	00		.
	nop			;36c6	00		.
	nop			;36c7	00		.
	inc b			;36c8	04		.
	rrca			;36c9	0f		.
	nop			;36ca	00		.
	nop			;36cb	00		.
	nop			;36cc	00		.
	nop			;36cd	00		.
	nop			;36ce	00		.
	nop			;36cf	00		.
	nop			;36d0	00		.
	nop			;36d1	00		.
	nop			;36d2	00		.
	nop			;36d3	00		.
	nop			;36d4	00		.
	nop			;36d5	00		.
	nop			;36d6	00		.
	inc b			;36d7	04		.
	rrca			;36d8	0f		.
	nop			;36d9	00		.
	nop			;36da	00		.
	nop			;36db	00		.
	nop			;36dc	00		.
	nop			;36dd	00		.
	nop			;36de	00		.
	nop			;36df	00		.
	nop			;36e0	00		.
	nop			;36e1	00		.
	nop			;36e2	00		.
	nop			;36e3	00		.
l36e4h:
	nop			;36e4	00		.
	nop			;36e5	00		.
	nop			;36e6	00		.
	nop			;36e7	00		.
	nop			;36e8	00		.
	nop			;36e9	00		.
	ld c,a			;36ea	4f		O
	jr l36edh		;36eb	18 00		. .
l36edh:
	rlca			;36ed	07		.
	nop			;36ee	00		.
	nop			;36ef	00		.
	nop			;36f0	00		.
	nop			;36f1	00		.
	nop			;36f2	00		.
	rlca			;36f3	07		.
	nop			;36f4	00		.
	inc bc			;36f5	03		.
	ld bc,l070ah		;36f6	01 0a 07	. . .
	ex af,af'		;36f9	08		.
	nop			;36fa	00		.
	nop			;36fb	00		.
	ld bc,l3f10h		;36fc	01 10 3f	. . ?
	cp 000h			;36ff	fe 00		. .
	and (hl)		;3701	a6		.
	nop			;3702	00		.
	nop			;3703	00		.
	nop			;3704	00		.
	nop			;3705	00		.
	nop			;3706	00		.
	nop			;3707	00		.
	nop			;3708	00		.
	nop			;3709	00		.
	nop			;370a	00		.
	nop			;370b	00		.
	nop			;370c	00		.
	nop			;370d	00		.
	rst 38h			;370e	ff		.
	rrca			;370f	0f		.
	dec b			;3710	05		.
	nop			;3711	00		.
	ex af,af'		;3712	08		.
	ld h,b			;3713	60		`
	inc b			;3714	04		.
	ccf			;3715	3f		?
	ret			;3716	c9		.
	nop			;3717	00		.
	nop			;3718	00		.
l3719h:
	rst 38h			;3719	ff		.
	cp 0fdh			;371a	fe fd		. .
	call m,0eeefh		;371c	fc ef ee	. . .
	defb 0edh ;next byte illegal after ed	;371f	ed		.
	call pe,0deffh		;3720	ec ff de	. . .
	cp l			;3723	bd		.
	sbc a,h			;3724	9c		.
	ld l,a			;3725	6f		o
	ld c,(hl)		;3726	4e		N
	dec l			;3727	2d		-
	inc c			;3728	0c		.
sub_3729h:
	ex af,af'		;3729	08		.
	ld a,003h		;372a	3e 03		> .
	rst 18h			;372c	df		.
	ex af,af'		;372d	08		.
	ld (05f92h),a		;372e	32 92 5f	2 . _
	ld a,0f9h		;3731	3e f9		> .
	ld i,a			;3733	ed 47		. G
	ld ix,05fbfh		;3735	dd 21 bf 5f	. ! . _
	ld hl,l36e4h		;3739	21 e4 36	! . 6
	ld de,05f40h		;373c	11 40 5f	. @ _
	ld bc,l0035h		;373f	01 35 00	. 5 .
	ldir			;3742	ed b0		. .
	ld a,001h		;3744	3e 01		> .
	rst 18h			;3746	df		.
	ld a,0c3h		;3747	3e c3		> .
	ld (0df43h),a		;3749	32 43 df	2 C .
	ld hl,MonitorTable0715_start	;374c	21 15 07	! . .
	ld (0df44h),hl		;374f	22 44 df	" D .
	call sub_0930h		;3752	cd 30 09	. 0 .
	call sub_0aa4h		;3755	cd a4 0a	. . .
	ld a,003h		;3758	3e 03		> .
	rst 18h			;375a	df		.
	ret			;375b	c9		.
sub_375ch:
	ld a,003h		;375c	3e 03		> .
	rst 18h			;375e	df		.
	ld de,0f800h		;375f	11 00 f8	. . .
	ld hl,BiosJumpTable	;3762	21 ca 02	! . .
	ld bc,0025eh		;3765	01 5e 02	. ^ .
	ldir			;3768	ed b0		. .
	ld hl,0fa5eh		;376a	21 5e fa	! ^ .
	ld de,0fa5fh		;376d	11 5f fa	. _ .
	ld bc,004fbh		;3770	01 fb 04	. . .
	ld (hl),000h		;3773	36 00		6 .
	ldir			;3775	ed b0		. .
	ret			;3777	c9		.
sub_3778h:
	ex (sp),hl		;3778	e3		.
	ld de,05f95h		;3779	11 95 5f	. . _
	ld bc,l0001h+2		;377c	01 03 00	. . .
	ldir			;377f	ed b0		. .
	ex (sp),hl		;3781	e3		.
	ld de,0ffffh		;3782	11 ff ff	. . .
	ld a,03fh		;3785	3e 3f		> ?
	call sub_3791h		;3787	cd 91 37	. . 7
	ld a,01fh		;378a	3e 1f		> .
	call sub_3791h		;378c	cd 91 37	. . 7
	ld a,007h		;378f	3e 07		> .
sub_3791h:
	rst 18h			;3791	df		.
	call 05f95h		;3792	cd 95 5f	. . _
	inc hl			;3795	23		#
	ret			;3796	c9		.
sub_3797h:
	call sub_3778h		;3797	cd 78 37	. x 7
	ld a,(de)		;379a	1a		.
	ld (hl),a		;379b	77		w
	ret			;379c	c9		.
	ret			;379d	c9		.
sub_379eh:
	call sub_3778h		;379e	cd 78 37	. x 7
	ld a,(hl)		;37a1	7e		~
	ld (de),a		;37a2	12		.
	ret			;37a3	c9		.
	ret			;37a4	c9		.
sub_37a5h:
	ld hl,05f98h		;37a5	21 98 5f	! . _
	call sub_3797h		;37a8	cd 97 37	. . 7
	ld hl,l37c0h		;37ab	21 c0 37	! . 7
	call sub_379eh		;37ae	cd 9e 37	. . 7
	ld hl,05f92h		;37b1	21 92 5f	! . _
	call sub_3797h		;37b4	cd 97 37	. . 7
	ld hl,05f98h		;37b7	21 98 5f	! . _
	call sub_379eh		;37ba	cd 9e 37	. . 7
	call sub_3801h		;37bd	cd 01 38	. . 8
l37c0h:
	ld c,c			;37c0	49		I
	sub d			;37c1	92		.
	inc h			;37c2	24		$
	set 0,(ix-07fh)		;37c3	dd cb 81 c6	. . . .
	dec c			;37c7	0d		.
	ld de,l3816h		;37c8	11 16 38	. . 8
	ret z			;37cb	c8		.
	res 0,(ix-07fh)		;37cc	dd cb 81 86	. . . .
	dec c			;37d0	0d		.
	ld de,l381dh		;37d1	11 1d 38	. . 8
	ret z			;37d4	c8		.
	ld de,RamSizeTable_end	;37d5	11 24 38	. $ 8
	dec c			;37d8	0d		.
	ret z			;37d9	c8		.
	call MonitorPrint	;37da	cd 06 12	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BiosFaultMsg - "fault. (" disk-fault message tail.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'BiosFaultMsg' (start 0x37dd end 0x37e7)
BiosFaultMsg_start:
	defb 066h		;37dd	66		f
	defb 061h		;37de	61		a
	defb 075h		;37df	75		u
	defb 06ch		;37e0	6c		l
	defb 074h		;37e1	74		t
	defb 02eh		;37e2	2e		.
	defb 020h		;37e3	20		 
	defb 028h		;37e4	28		(
	defb 000h		;37e5	00		.
	defb 03ah		;37e6	3a		:
BiosFaultMsg_end:
	sub d			;37e7	92		.
	ld e,a			;37e8	5f		_
	call sub_120eh		;37e9	cd 0e 12	. . .
	ld a,(05f93h)		;37ec	3a 93 5f	: . _
	call sub_120eh		;37ef	cd 0e 12	. . .
	ld a,(05f94h)		;37f2	3a 94 5f	: . _
	call sub_1217h		;37f5	cd 17 12	. . .
	call MonitorPrint	;37f8	cd 06 12	. . .
	add hl,hl		;37fb	29		)
	dec c			;37fc	0d		.
	ld a,(bc)		;37fd	0a		.
	nop			;37fe	00		.
	di			;37ff	f3		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RamSizeMenu - memory configuration scan and report.
; Scans RamSizeTable ($3815: attr+string records "128"/"512"/"1024"),
; prints "Memory " (MsgMemory), the size via MonitorPrintNumber,
; "size is " (MsgSizeIs), the value and the "k." KiB suffix (MsgKib),
; then stores the selection into the $FA4D/$FA55 cells.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RamSizeMenu:
	halt			;3800	76		v
sub_3801h:
	pop hl			;3801	e1		.
	ld de,05f92h		;3802	11 92 5f	. . _
	ld bc,00300h		;3805	01 00 03	. . .
l3808h:
	ld a,(de)		;3808	1a		.
	cp (hl)			;3809	be		.
	inc hl			;380a	23		#
	inc de			;380b	13		.
	jr nz,l3811h		;380c	20 03		  .
	inc c			;380e	0c		.
	jr l3813h		;380f	18 02		. .
l3811h:
	ld c,000h		;3811	0e 00		. .
l3813h:
	djnz l3808h		;3813	10 f3		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RamSizeTable - attr+string records "128" / "512" / "1024".
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'RamSizeTable' (start 0x3815 end 0x3824)
RamSizeTable_start:
	defb 0e9h		;3815	e9		.
l3816h:
	defb 003h		;3816	03		.
	defb 080h		;3817	80		.
	defb 001h		;3818	01		.
	defb 031h		;3819	31		1
	defb 032h		;381a	32		2
	defb 038h		;381b	38		8
	defb 000h		;381c	00		.
l381dh:
	defb 01bh		;381d	1b		.
	defb 080h		;381e	80		.
	defb 00dh		;381f	0d		.
	defb 035h		;3820	35		5
	defb 031h		;3821	31		1
	defb 032h		;3822	32		2
	defb 000h		;3823	00		.
RamSizeTable_end:
	dec sp			;3824	3b		;
	add a,b			;3825	80		.
	dec e			;3826	1d		.
	ld sp,l3230h		;3827	31 30 32	1 0 2
	inc (hl)		;382a	34		4
	nop			;382b	00		.
sub_382ch:
	call MonitorPrint	;382c	cd 06 12	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgMemory - "Memory " prefix of the RAM-size report.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgMemory' (start 0x382f end 0x3837)
MsgMemory_start:
	defb 04dh		;382f	4d		M
	defb 065h		;3830	65		e
	defb 06dh		;3831	6d		m
	defb 06fh		;3832	6f		o
	defb 072h		;3833	72		r
	defb 079h		;3834	79		y
	defb 020h		;3835	20		 
	defb 000h		;3836	00		.
MsgMemory_end:
	call sub_37a5h		;3837	cd a5 37	. . 7
	ld a,003h		;383a	3e 03		> .
	rst 18h			;383c	df		.
	ex de,hl		;383d	eb		.
	ld a,(hl)		;383e	7e		~
	inc hl			;383f	23		#
	ld (0fa4dh),a		;3840	32 4d fa	2 M .
	ld (0fa59h),a		;3843	32 59 fa	2 Y .
	ld e,(hl)		;3846	5e		^
	inc hl			;3847	23		#
	ld d,(hl)		;3848	56		V
	inc hl			;3849	23		#
	ld (0fa55h),de		;384a	ed 53 55 fa	. S U .
	call MonitorPrint	;384e	cd 06 12	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgSizeIs - "size is " of the RAM-size report.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgSizeIs' (start 0x3851 end 0x385a)
MsgSizeIs_start:
	defb 073h		;3851	73		s
	defb 069h		;3852	69		i
	defb 07ah		;3853	7a		z
	defb 065h		;3854	65		e
	defb 020h		;3855	20		 
	defb 069h		;3856	69		i
	defb 073h		;3857	73		s
	defb 020h		;3858	20		 
	defb 000h		;3859	00		.
MsgSizeIs_end:
	call MonitorPrintNumber	;385a	cd ff 11	. . .
	call MonitorPrint	;385d	cd 06 12	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgKib - "k.\r\n" KiB suffix of the RAM-size report.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgKib' (start 0x3860 end 0x3865)
MsgKib_start:
	defb 06bh		;3860	6b		k
	defb 02eh		;3861	2e		.
	defb 00dh		;3862	0d		.
	defb 00ah		;3863	0a		.
	defb 000h		;3864	00		.
MsgKib_end:
	ret			;3865	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WbootReloadImage - THE warm-boot reload.
; LD BC,$1B1E / LD DE,$D400 / LD HL,$1BC6 / LDIR / RET:
; copies the CCP+BDOS image $1BC6-$36E3 to RAM $D400-$EF1E.  This
; LDIR is the proof of the t-$B83A mapping (the $D400-$EFFF window
; in dual use, HEADER above).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WbootReloadImage:
	ld bc,l1b1eh		;3866	01 1e 1b	. . .
sub_3869h:
	ld de,0d400h		;3869	11 00 d4	. . .
	ld hl,CcpBase		;386c	21 c6 1b	! . .
	ldir			;386f	ed b0		. .
	ret			;3871	c9		.
sub_3872h:
	ld a,(05f5dh)		;3872	3a 5d 5f	: ] _
	ld bc,0fd77h		;3875	01 77 fd	. w .
	out (c),a		;3878	ed 79		. y
	ret			;387a	c9		.
	di			;387b	f3		.
	scf			;387c	37		7
	ret			;387d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Wboot - warm boot entry (traced at runtime $F87E).
; LD BC,$0077 / LD A,$02 / OUT (C),A: the ATM manager port $77 with
; the CPM control line; IM 1, DI, LD SP,$388E, JP WbootContinue
; ($38FA).  The cell just above ($3872) latches port $FD77 from RAM
; variable $5F5D on the way in.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Wboot:
	ld bc,l0077h		;387e	01 77 00	. w .
	ld a,002h		;3881	3e 02		> .
	out (c),a		;3883	ed 79		. y
	im 1			;3885	ed 56		. V
	di			;3887	f3		.
	ld sp,l388eh		;3888	31 8e 38	1 . 8
	jp WbootContinue	;388b	c3 fa 38	. . 8
l388eh:
	sub b			;388e	90		.
	jr c,l3892h		;388f	38 01		8 .
	ld (hl),a		;3891	77		w
l3892h:
	nop			;3892	00		.
	ld a,006h		;3893	3e 06		> .
	out (c),a		;3895	ed 79		. y
	ld sp,DriverTable389D_start	;3897	31 9d 38	1 . 8
	jp WbootContinue	;389a	c3 fa 38	. . 8
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DriverTable389D - driver lookup table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'DriverTable389D' (start 0x389d end 0x38a9)
DriverTable389D_start:
	defb 09fh		;389d	9f		.
	defb 038h		;389e	38		8
	defb 031h		;389f	31		1
	defb 000h		;38a0	00		.
	defb 080h		;38a1	80		.
	defb 03eh		;38a2	3e		>
	defb 010h		;38a3	10		.
	defb 0edh		;38a4	ed		.
	defb 079h		;38a5	79		y
	defb 03eh		;38a6	3e		>
	defb 006h		;38a7	06		.
	defb 001h		;38a8	01		.
DriverTable389D_end:
	ld (hl),a		;38a9	77		w
	ld bc,079edh		;38aa	01 ed 79	. . y
	ld hl,l391eh		;38ad	21 1e 39	! . 9
	ld de,l0610h		;38b0	11 10 06	. . .
	ld c,077h		;38b3	0e 77		. w
	ld a,008h		;38b5	3e 08		> .
	out (0ffh),a		;38b7	d3 ff		. .
l38b9h:
	ld a,(hl)		;38b9	7e		~
	inc hl			;38ba	23		#
	out (0ffh),a		;38bb	d3 ff		. .
	ld b,041h		;38bd	06 41		. A
	out (c),d		;38bf	ed 51		. Q
	or 008h			;38c1	f6 08		. .
	out (0ffh),a		;38c3	d3 ff		. .
	ld b,001h		;38c5	06 01		. .
	out (c),d		;38c7	ed 51		. Q
	dec e			;38c9	1d		.
	jr nz,l38b9h		;38ca	20 ed		  .
	ld bc,04177h		;38cc	01 77 41	. w A
	ld a,006h		;38cf	3e 06		> .
	out (c),a		;38d1	ed 79		. y
	ld a,0ffh		;38d3	3e ff		> .
	out (0ffh),a		;38d5	d3 ff		. .
	ld hl,0c000h		;38d7	21 00 c0	! . .
	ld a,(hl)		;38da	7e		~
	cp 0c3h			;38db	fe c3		. .
	jr nz,l38f7h		;38dd	20 18		  .
	xor a			;38df	af		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DriverTable38E0 - driver lookup table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'DriverTable38E0' (start 0x38e0 end 0x38fa)
DriverTable38E0_start:
	defb 086h		;38e0	86		.
	defb 0ceh		;38e1	ce		.
	defb 000h		;38e2	00		.
	defb 023h		;38e3	23		#
	defb 0cbh		;38e4	cb		.
	defb 07ch		;38e5	7c		|
	defb 020h		;38e6	20		 
	defb 0f8h		;38e7	f8		.
	defb 03dh		;38e8	3d		=
	defb 020h		;38e9	20		 
	defb 00ch		;38ea	0c		.
	defb 03eh		;38eb	3e		>
	defb 0aah		;38ec	aa		.
	defb 02bh		;38ed	2b		+
	defb 0beh		;38ee	be		.
	defb 020h		;38ef	20		 
	defb 006h		;38f0	06		.
	defb 02fh		;38f1	2f		/
	defb 02bh		;38f2	2b		+
	defb 0beh		;38f3	be		.
	defb 0cah		;38f4	ca		.
	defb 000h		;38f5	00		.
	defb 0c0h		;38f6	c0		.
l38f7h:
	defb 0c3h		;38f7	c3		.
	defb 078h		;38f8	78		x
	defb 000h		;38f9	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WbootContinue - disk re-read and page-zero rebuild, then the hand
; off to WbootReloadImage and the CCP at $D400.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WbootContinue:
	ld hl,DriverTable3916_start	;38fa	21 16 39	! . 9
	ld de,l0040h		;38fd	11 40 00	. @ .
l3900h:
	ld bc,00ff7h		;3900	01 f7 0f	. . .
l3903h:
	outi			;3903	ed a3		. .
	ld a,b			;3905	78		x
	add a,e			;3906	83		.
	ld b,a			;3907	47		G
	jr nc,l3903h		;3908	30 f9		0 .
	ld bc,07ffdh		;390a	01 fd 7f	. . .
	ld a,d			;390d	7a		z
	xor 010h		;390e	ee 10		. .
	ld d,a			;3910	57		W
	out (c),a		;3911	ed 79		. y
	jr nz,l3900h		;3913	20 eb		  .
	ret			;3915	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DriverTable3916 - driver lookup table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'DriverTable3916' (start 0x3916 end 0x392e)
DriverTable3916_start:
	defb 07fh		;3916	7f		.
	defb 07bh		;3917	7b		{
	defb 07dh		;3918	7d		}
	defb 07ch		;3919	7c		|
	defb 018h		;391a	18		.
	defb 07ah		;391b	7a		z
	defb 07dh		;391c	7d		}
	defb 060h		;391d	60		`
l391eh:
	defb 0f1h		;391e	f1		.
	defb 0e1h		;391f	e1		.
	defb 0d1h		;3920	d1		.
	defb 0c1h		;3921	c1		.
	defb 0c1h		;3922	c1		.
	defb 0b1h		;3923	b1		.
	defb 0a1h		;3924	a1		.
	defb 091h		;3925	91		.
	defb 041h		;3926	41		A
	defb 021h		;3927	21		!
	defb 031h		;3928	31		1
	defb 011h		;3929	11		.
	defb 001h		;392a	01		.
	defb 001h		;392b	01		.
	defb 0f1h		;392c	f1		.
	defb 0e1h		;392d	e1		.
DriverTable3916_end:
	ret			;392e	c9		.
l392fh:
	jp l3947h		;392f	c3 47 39	. G 9
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DriverTable3932 - driver lookup table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'DriverTable3932' (start 0x3932 end 0x3935)
DriverTable3932_start:
	defb 0c3h		;3932	c3		.
	defb 078h		;3933	78		x
	defb 039h		;3934	39		9
DriverTable3932_end:
	jp DriverTable39DE_end	;3935	c3 ed 39	. . 9
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DriverTable3938 - driver lookup table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'DriverTable3938' (start 0x3938 end 0x393e)
DriverTable3938_start:
	defb 0c3h		;3938	c3		.
	defb 0cbh		;3939	cb		.
	defb 039h		;393a	39		9
	defb 0c3h		;393b	c3		.
	defb 0deh		;393c	de		.
	defb 039h		;393d	39		9
DriverTable3938_end:
	ex af,af'		;393e	08		.
	ld a,055h		;393f	3e 55		> U
	in a,(0feh)		;3941	db fe		. .
	ex af,af'		;3943	08		.
	in a,(0feh)		;3944	db fe		. .
	ret			;3946	c9		.
l3947h:
	ld a,008h		;3947	3e 08		> .
	call DriverTable3938_end	;3949	cd 3e 39	. > 9
	ld a,08ah		;394c	3e 8a		> .
	in a,(0feh)		;394e	db fe		. .
	ld a,001h		;3950	3e 01		> .
	call DriverTable3938_end	;3952	cd 3e 39	. > 9
	cp 010h			;3955	fe 10		. .
	jr nc,l395bh		;3957	30 02		0 .
	xor a			;3959	af		.
	ret			;395a	c9		.
l395bh:
	call MonitorPrint	;395b	cd 06 12	. . .
	dec de			;395e	1b		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgM1Ve31 - "M1VE31 not installed" add-on board message.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgM1Ve31' (start 0x395f end 0x3976)
MsgM1Ve31_start:
	defb 04dh		;395f	4d		M
	defb 031h		;3960	31		1
	defb 056h		;3961	56		V
	defb 045h		;3962	45		E
	defb 033h		;3963	33		3
	defb 031h		;3964	31		1
	defb 020h		;3965	20		 
	defb 06eh		;3966	6e		n
	defb 06fh		;3967	6f		o
	defb 074h		;3968	74		t
	defb 020h		;3969	20		 
	defb 069h		;396a	69		i
	defb 06eh		;396b	6e		n
	defb 073h		;396c	73		s
	defb 074h		;396d	74		t
	defb 061h		;396e	61		a
	defb 06ch		;396f	6c		l
	defb 06ch		;3970	6c		l
	defb 065h		;3971	65		e
	defb 064h		;3972	64		d
	defb 00dh		;3973	0d		.
	defb 00ah		;3974	0a		.
	defb 000h		;3975	00		.
MsgM1Ve31_end:
	di			;3976	f3		.
	halt			;3977	76		v
l3978h:
	bit 5,(ix-07eh)		;3978	dd cb 82 6e	. . . n
	jr z,l3989h		;397c	28 0b		( .
	ld a,(ix-05ch)		;397e	dd 7e a4	. ~ .
	cp (ix-058h)		;3981	dd be a8	. . .
	ld (05f67h),a		;3984	32 67 5f	2 g _
	jr z,l39a7h		;3987	28 1e		( .
l3989h:
	ld bc,000feh		;3989	01 fe 00	. . .
	in a,(c)		;398c	ed 78		. x
	ld b,040h		;398e	06 40		. @
	in l,(c)		;3990	ed 68		. h
	ld b,080h		;3992	06 80		. .
	in h,(c)		;3994	ed 60		. `
	push de			;3996	d5		.
	ld de,(05f84h)		;3997	ed 5b 84 5f	. [ . _
	ld (05f84h),hl		;399b	22 84 5f	" . _
	or a			;399e	b7		.
	sbc hl,de		;399f	ed 52		. R
	pop de			;39a1	d1		.
	jr nz,l39c0h		;39a2	20 1c		  .
	or a			;39a4	b7		.
	jr nz,l39bbh		;39a5	20 14		  .
l39a7h:
	inc e			;39a7	1c		.
	dec e			;39a8	1d		.
	jr nz,l3978h		;39a9	20 cd		  .
	ld a,0ffh		;39ab	3e ff		> .
l39adh:
	ld l,000h		;39ad	2e 00		. .
l39afh:
	ld de,(05f84h)		;39af	ed 5b 84 5f	. [ . _
l39b3h:
	ex af,af'		;39b3	08		.
	ld a,d			;39b4	7a		z
	and 00fh		;39b5	e6 0f		. .
	or e			;39b7	b3		.
	ld e,a			;39b8	5f		_
	ex af,af'		;39b9	08		.
	ret			;39ba	c9		.
l39bbh:
	ld l,a			;39bb	6f		o
	ld a,000h		;39bc	3e 00		> .
	jr l39afh		;39be	18 ef		. .
l39c0h:
	or a			;39c0	b7		.
	jr nz,l39bbh		;39c1	20 f8		  .
	bit 1,e			;39c3	cb 4b		. K
	jr z,l3978h		;39c5	28 b1		( .
	ld a,001h		;39c7	3e 01		> .
	jr l39adh		;39c9	18 e2		. .
	ld a,009h		;39cb	3e 09		> .
	call DriverTable3938_end	;39cd	cd 3e 39	. > 9
	ld e,a			;39d0	5f		_
	ld a,049h		;39d1	3e 49		> I
	call DriverTable3938_end	;39d3	cd 3e 39	. > 9
	ld d,a			;39d6	57		W
	ld (05f84h),de		;39d7	ed 53 84 5f	. S . _
	xor a			;39db	af		.
	jr l39b3h		;39dc	18 d5		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DriverTable39DE - driver lookup table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'DriverTable39DE' (start 0x39de end 0x39ed)
DriverTable39DE_start:
	defb 0b7h		;39de	b7		.
	defb 03eh		;39df	3e		>
	defb 055h		;39e0	55		U
	defb 0dbh		;39e1	db		.
	defb 0feh		;39e2	fe		.
	defb 03eh		;39e3	3e		>
	defb 00bh		;39e4	0b		.
	defb 028h		;39e5	28		(
	defb 001h		;39e6	01		.
	defb 03dh		;39e7	3d		=
	defb 0dbh		;39e8	db		.
	defb 0feh		;39e9	fe		.
	defb 0c3h		;39ea	c3		.
	defb 0cbh		;39eb	cb		.
	defb 039h		;39ec	39		9
DriverTable39DE_end:
	ld hl,l39f2h		;39ed	21 f2 39	! . 9
	xor a			;39f0	af		.
	ret			;39f1	c9		.
l39f2h:
	ld d,h			;39f2	54		T
	nop			;39f3	00		.
	ld d,h			;39f4	54		T
	dec b			;39f5	05		.
	nop			;39f6	00		.
	nop			;39f7	00		.
	defb 018h		;39f8	18		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KbdMap39F9 - scan-code to charset class map (":" classes with
; high-bit codes 8E/91/9C/9F/AA).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KbdMap39F9' (start 0x39f9 end 0x3a24)
KbdMap39F9_start:
	defb 03ah		;39f9	3a		:
	defb 04dh		;39fa	4d		M
	defb 03ah		;39fb	3a		:
	defb 05eh		;39fc	5e		^
	defb 03ah		;39fd	3a		:
	defb 073h		;39fe	73		s
	defb 03ah		;39ff	3a		:
	defb 000h		;3a00	00		.
	defb 000h		;3a01	00		.
	defb 036h		;3a02	36		6
	defb 03ah		;3a03	3a		:
	defb 036h		;3a04	36		6
	defb 03ah		;3a05	3a		:
	defb 036h		;3a06	36		6
	defb 03ah		;3a07	3a		:
	defb 036h		;3a08	36		6
	defb 03ah		;3a09	3a		:
l3a0ah:
	defb 08eh		;3a0a	8e		.
	defb 03ah		;3a0b	3a		:
	defb 091h		;3a0c	91		.
	defb 03ah		;3a0d	3a		:
	defb 09ch		;3a0e	9c		.
	defb 03ah		;3a0f	3a		:
	defb 09fh		;3a10	9f		.
	defb 03ah		;3a11	3a		:
	defb 0aah		;3a12	aa		.
	defb 03ah		;3a13	3a		:
	defb 044h		;3a14	44		D
	defb 03ah		;3a15	3a		:
	defb 036h		;3a16	36		6
	defb 03ah		;3a17	3a		:
	defb 02eh		;3a18	2e		.
	defb 0ffh		;3a19	ff		.
	defb 0ddh		;3a1a	dd		.
	defb 0cbh		;3a1b	cb		.
	defb 0c5h		;3a1c	c5		.
	defb 06eh		;3a1d	6e		n
	defb 0c0h		;3a1e	c0		.
	defb 0feh		;3a1f	fe		.
	defb 02eh		;3a20	2e		.
	defb 020h		;3a21	20		 
	defb 003h		;3a22	03		.
	defb 03eh		;3a23	3e		>
KbdMap39F9_end:
	ld a,c			;3a24	79		y
	ret			;3a25	c9		.
	cp 030h			;3a26	fe 30		. 0
	ret c			;3a28	d8		.
	cp 03ah			;3a29	fe 3a		. :
	ret nc			;3a2b	d0		.
	ld hl,l3a0ah		;3a2c	21 0a 3a	! . :
	call sub_1106h		;3a2f	cd 06 11	. . .
	ld a,(hl)		;3a32	7e		~
	ld l,0ffh		;3a33	2e ff		. .
	ret			;3a35	c9		.
	xor a			;3a36	af		.
	ld l,000h		;3a37	2e 00		. .
	ret			;3a39	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CharsetTables - 144 bytes of charset/codepage tables.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'CharsetTables' (start 0x3a3a end 0x3aca)
CharsetTables_start:
	defb 078h		;3a3a	78		x
	defb 077h		;3a3b	77		w
	defb 071h		;3a3c	71		q
	defb 075h		;3a3d	75		u
	defb 072h		;3a3e	72		r
	defb 000h		;3a3f	00		.
	defb 073h		;3a40	73		s
	defb 076h		;3a41	76		v
	defb 070h		;3a42	70		p
	defb 074h		;3a43	74		t
	defb 0feh		;3a44	fe		.
	defb 040h		;3a45	40		@
	defb 038h		;3a46	38		8
	defb 0eeh		;3a47	ee		.
	defb 0e6h		;3a48	e6		.
	defb 01fh		;3a49	1f		.
	defb 02eh		;3a4a	2e		.
	defb 000h		;3a4b	00		.
	defb 0c9h		;3a4c	c9		.
	defb 088h		;3a4d	88		.
	defb 060h		;3a4e	60		`
	defb 03bh		;3a4f	3b		;
	defb 027h		;3a50	27		'
	defb 02ch		;3a51	2c		,
	defb 02eh		;3a52	2e		.
	defb 05bh		;3a53	5b		[
	defb 05dh		;3a54	5d		]
	defb 05ch		;3a55	5c		\
	defb 028h		;3a56	28		(
	defb 076h		;3a57	76		v
	defb 07ch		;3a58	7c		|
	defb 062h		;3a59	62		b
	defb 060h		;3a5a	60		`
	defb 068h		;3a5b	68		h
	defb 07fh		;3a5c	7f		.
	defb 000h		;3a5d	00		.
	defb 08ah		;3a5e	8a		.
	defb 060h		;3a5f	60		`
	defb 03bh		;3a60	3b		;
	defb 027h		;3a61	27		'
	defb 02ch		;3a62	2c		,
	defb 02eh		;3a63	2e		.
	defb 05bh		;3a64	5b		[
	defb 05dh		;3a65	5d		]
	defb 03dh		;3a66	3d		=
	defb 05ch		;3a67	5c		\
	defb 02fh		;3a68	2f		/
	defb 029h		;3a69	29		)
	defb 056h		;3a6a	56		V
	defb 05ch		;3a6b	5c		\
	defb 042h		;3a6c	42		B
	defb 040h		;3a6d	40		@
	defb 048h		;3a6e	48		H
	defb 05fh		;3a6f	5f		_
	defb 02bh		;3a70	2b		+
	defb 000h		;3a71	00		.
	defb 03fh		;3a72	3f		?
	defb 000h		;3a73	00		.
	defb 066h		;3a74	66		f
	defb 069h		;3a75	69		i
	defb 073h		;3a76	73		s
	defb 077h		;3a77	77		w
	defb 075h		;3a78	75		u
	defb 061h		;3a79	61		a
	defb 070h		;3a7a	70		p
	defb 072h		;3a7b	72		r
	defb 07bh		;3a7c	7b		{
	defb 06fh		;3a7d	6f		o
	defb 06ch		;3a7e	6c		l
	defb 064h		;3a7f	64		d
	defb 078h		;3a80	78		x
	defb 074h		;3a81	74		t
	defb 07dh		;3a82	7d		}
	defb 07ah		;3a83	7a		z
	defb 06ah		;3a84	6a		j
	defb 06bh		;3a85	6b		k
	defb 079h		;3a86	79		y
	defb 065h		;3a87	65		e
	defb 067h		;3a88	67		g
	defb 06dh		;3a89	6d		m
	defb 063h		;3a8a	63		c
	defb 07eh		;3a8b	7e		~
	defb 06eh		;3a8c	6e		n
	defb 071h		;3a8d	71		q
	defb 081h		;3a8e	81		.
	defb 00dh		;3a8f	0d		.
	defb 00ah		;3a90	0a		.
	defb 000h		;3a91	00		.
	defb 029h		;3a92	29		)
	defb 021h		;3a93	21		!
	defb 040h		;3a94	40		@
	defb 023h		;3a95	23		#
	defb 024h		;3a96	24		$
	defb 025h		;3a97	25		%
	defb 05eh		;3a98	5e		^
	defb 026h		;3a99	26		&
	defb 02ah		;3a9a	2a		*
	defb 028h		;3a9b	28		(
	defb 001h		;3a9c	01		.
	defb 036h		;3a9d	36		6
	defb 01eh		;3a9e	1e		.
	defb 000h		;3a9f	00		.
	defb 025h		;3aa0	25		%
	defb 021h		;3aa1	21		!
	defb 022h		;3aa2	22		"
	defb 02fh		;3aa3	2f		/
	defb 024h		;3aa4	24		$
	defb 03ah		;3aa5	3a		:
	defb 02ch		;3aa6	2c		,
	defb 02eh		;3aa7	2e		.
	defb 03bh		;3aa8	3b		;
	defb 03fh		;3aa9	3f		?
	defb 00bh		;3aaa	0b		.
	defb 060h		;3aab	60		`
	defb 03bh		;3aac	3b		;
	defb 027h		;3aad	27		'
	defb 02ch		;3aae	2c		,
	defb 02eh		;3aaf	2e		.
	defb 05bh		;3ab0	5b		[
	defb 05dh		;3ab1	5d		]
	defb 02dh		;3ab2	2d		-
	defb 03dh		;3ab3	3d		=
	defb 05ch		;3ab4	5c		\
	defb 02fh		;3ab5	2f		/
	defb 07eh		;3ab6	7e		~
	defb 03ah		;3ab7	3a		:
	defb 022h		;3ab8	22		"
	defb 03ch		;3ab9	3c		<
	defb 03eh		;3aba	3e		>
	defb 07bh		;3abb	7b		{
	defb 07dh		;3abc	7d		}
	defb 05fh		;3abd	5f		_
	defb 02bh		;3abe	2b		+
	defb 07ch		;3abf	7c		|
	defb 03fh		;3ac0	3f		?
sub_3ac1h:
	defb 0c3h		;3ac1	c3		.
	defb 0cah		;3ac2	ca		.
	defb 03ah		;3ac3	3a		:
	defb 0c3h		;3ac4	c3		.
	defb 06ah		;3ac5	6a		j
	defb 03ch		;3ac6	3c		<
	defb 0c3h		;3ac7	c3		.
	defb 00ah		;3ac8	0a		.
	defb 03dh		;3ac9	3d		=
CharsetTables_end:
	xor a			;3aca	af		.
	ld (05f86h),a		;3acb	32 86 5f	2 . _
	ld (05f87h),a		;3ace	32 87 5f	2 . _
	ld (05f88h),a		;3ad1	32 88 5f	2 . _
	ld (05f89h),a		;3ad4	32 89 5f	2 . _
	jp 05f81h		;3ad7	c3 81 5f	. . _
sub_3adah:
	ld d,a			;3ada	57		W
	bit 2,e			;3adb	cb 53		. S
	scf			;3add	37		7
	ret nz			;3ade	c0		.
l3adfh:
	ld d,a			;3adf	57		W
	bit 7,a			;3ae0	cb 7f		. .
	jr z,l3aefh		;3ae2	28 0b		( .
	res 7,d			;3ae4	cb ba		. .
	xor a			;3ae6	af		.
	call sub_3b8fh		;3ae7	cd 8f 3b	. . ;
l3aeah:
	inc l			;3aea	2c		,
	jr z,l3adfh		;3aeb	28 f2		( .
	jr l3b33h		;3aed	18 44		. D
l3aefh:
	ld hl,l3c14h		;3aef	21 14 3c	! . <
	ld b,0ffh		;3af2	06 ff		. .
l3af4h:
	inc hl			;3af4	23		#
	inc b			;3af5	04		.
	cp (hl)			;3af6	be		.
	inc hl			;3af7	23		#
	jr c,l3af4h		;3af8	38 fa		8 .
	cp (hl)			;3afa	be		.
	jr nc,l3af4h		;3afb	30 f7		0 .
	ld c,000h		;3afd	0e 00		. .
	bit 1,e			;3aff	cb 4b		. K
	ld a,003h		;3b01	3e 03		> .
	jr nz,l3b17h		;3b03	20 12		  .
	bit 7,e			;3b05	cb 7b		. {
	jr z,l3b0bh		;3b07	28 02		( .
	ld c,004h		;3b09	0e 04		. .
l3b0bh:
	bit 0,e			;3b0b	cb 43		. C
	ld a,001h		;3b0d	3e 01		> .
	jr nz,l3b17h		;3b0f	20 06		  .
	inc a			;3b11	3c		<
	bit 3,e			;3b12	cb 5b		. [
	jr nz,l3b17h		;3b14	20 01		  .
	xor a			;3b16	af		.
l3b17h:
	or c			;3b17	b1		.
	add a,a			;3b18	87		.
	add a,a			;3b19	87		.
	add a,a			;3b1a	87		.
	add a,a			;3b1b	87		.
	or b			;3b1c	b0		.
	cp 032h			;3b1d	fe 32		. 2
	jr nz,l3b3fh		;3b1f	20 1e		  .
	ld a,01fh		;3b21	3e 1f		> .
	and d			;3b23	a2		.
	ret			;3b24	c9		.
l3b25h:
	ld a,d			;3b25	7a		z
	bit 4,e			;3b26	cb 63		. c
	ret z			;3b28	c8		.
l3b29h:
	add a,020h		;3b29	c6 20		.  
	ret			;3b2b	c9		.
l3b2ch:
	ld a,d			;3b2c	7a		z
	bit 4,e			;3b2d	cb 63		. c
	ret nz			;3b2f	c0		.
	jr l3b29h		;3b30	18 f7		. .
l3b32h:
	ld a,d			;3b32	7a		z
l3b33h:
	or a			;3b33	b7		.
	ret nz			;3b34	c0		.
	scf			;3b35	37		7
	ret			;3b36	c9		.
sub_3b37h:
	call sub_3b8eh		;3b37	cd 8e 3b	. . ;
	inc l			;3b3a	2c		,
	jr nz,l3b33h		;3b3b	20 f6		  .
	jr l3adfh		;3b3d	18 a0		. .
l3b3fh:
	cp 002h			;3b3f	fe 02		. .
	jr z,l3b25h		;3b41	28 e2		( .
	cp 012h			;3b43	fe 12		. .
	jr z,l3b2ch		;3b45	28 e5		( .
	ld hl,KbdCtrlMap3C1F_start	;3b47	21 1f 3c	! . <
	ld bc,l0017h		;3b4a	01 17 00	. . .
	cpir			;3b4d	ed b1		. .
	jr nz,l3b32h		;3b4f	20 e1		  .
	ld a,c			;3b51	79		y
	cp 008h			;3b52	fe 08		. .
	jr c,l3b5bh		;3b54	38 05		8 .
l3b56h:
	call sub_3b8eh		;3b56	cd 8e 3b	. . ;
	jr l3aeah		;3b59	18 8f		. .
l3b5bh:
	push af			;3b5b	f5		.
	or 001h			;3b5c	f6 01		. .
	call sub_3b37h		;3b5e	cd 37 3b	. 7 ;
	pop hl			;3b61	e1		.
	res 0,h			;3b62	cb 84		. .
	jr c,l3b87h		;3b64	38 21		8 !
	bit 7,a			;3b66	cb 7f		. .
	jr nz,l3b7eh		;3b68	20 14		  .
	cp 041h			;3b6a	fe 41		. A
	bit 7,e			;3b6c	cb 7b		. {
	jr z,l3b72h		;3b6e	28 02		( .
	cp 040h			;3b70	fe 40		. @
l3b72h:
	jr c,l3b87h		;3b72	38 13		8 .
	cp 05bh			;3b74	fe 5b		. [
	bit 7,e			;3b76	cb 7b		. {
	jr z,l3b7ch		;3b78	28 02		( .
	cp 060h			;3b7a	fe 60		. `
l3b7ch:
	jr nc,l3b87h		;3b7c	30 09		0 .
l3b7eh:
	ld c,a			;3b7e	4f		O
	ld a,e			;3b7f	7b		{
	and 011h		;3b80	e6 11		. .
	ld a,c			;3b82	79		y
	ret po			;3b83	e0		.
l3b84h:
	ld a,h			;3b84	7c		|
	jr l3b56h		;3b85	18 cf		. .
l3b87h:
	or a			;3b87	b7		.
	bit 0,e			;3b88	cb 43		. C
	jr nz,l3b33h		;3b8a	20 a7		  .
	jr l3b84h		;3b8c	18 f6		. .
sub_3b8eh:
	inc a			;3b8e	3c		<
sub_3b8fh:
	push de			;3b8f	d5		.
	push af			;3b90	f5		.
	call DriverTable3932_end	;3b91	cd 35 39	. 5 9
	pop af			;3b94	f1		.
	push hl			;3b95	e5		.
	ld c,a			;3b96	4f		O
	add a,a			;3b97	87		.
	add a,006h		;3b98	c6 06		. .
	call sub_1106h		;3b9a	cd 06 11	. . .
	ld b,(hl)		;3b9d	46		F
	inc hl			;3b9e	23		#
	ld h,(hl)		;3b9f	66		f
	ld l,b			;3ba0	68		h
	ex (sp),hl		;3ba1	e3		.
	ld a,c			;3ba2	79		y
	srl a			;3ba3	cb 3f		. ?
	srl a			;3ba5	cb 3f		. ?
	call sub_1106h		;3ba7	cd 06 11	. . .
	ld a,c			;3baa	79		y
	and 003h		;3bab	e6 03		. .
	ld b,a			;3bad	47		G
	ld a,(hl)		;3bae	7e		~
	jr z,l3bb5h		;3baf	28 04		( .
l3bb1h:
	rra			;3bb1	1f		.
	rra			;3bb2	1f		.
	djnz l3bb1h		;3bb3	10 fc		. .
l3bb5h:
	and 003h		;3bb5	e6 03		. .
	jr z,KbdTables3BF6_start	;3bb7	28 3d		( =
	dec a			;3bb9	3d		=
	ld l,000h		;3bba	2e 00		. .
	jr z,l3bbfh		;3bbc	28 01		( .
	dec l			;3bbe	2d		-
l3bbfh:
	ex (sp),hl		;3bbf	e3		.
	ld a,(hl)		;3bc0	7e		~
	inc hl			;3bc1	23		#
	or a			;3bc2	b7		.
	jr nz,l3bddh		;3bc3	20 18		  .
	ld a,d			;3bc5	7a		z
	cp 041h			;3bc6	fe 41		. A
	jr c,l3bd2h		;3bc8	38 08		8 .
	cp 061h			;3bca	fe 61		. a
	jr c,l3bd0h		;3bcc	38 02		8 .
	sub 020h		;3bce	d6 20		.  
l3bd0h:
	sub 011h		;3bd0	d6 11		. .
l3bd2h:
	sub 030h		;3bd2	d6 30		. 0
l3bd4h:
	call sub_1106h		;3bd4	cd 06 11	. . .
	ld a,(hl)		;3bd7	7e		~
l3bd8h:
	pop hl			;3bd8	e1		.
	pop de			;3bd9	d1		.
	jp l3b33h		;3bda	c3 33 3b	. 3 ;
l3bddh:
	ld c,a			;3bdd	4f		O
	bit 7,c			;3bde	cb 79		. y
	jr z,l3be3h		;3be0	28 01		( .
	scf			;3be2	37		7
l3be3h:
	res 7,c			;3be3	cb b9		. .
	ld e,c			;3be5	59		Y
	ld b,000h		;3be6	06 00		. .
	ld a,d			;3be8	7a		z
	cpir			;3be9	ed b1		. .
	dec hl			;3beb	2b		+
	jr nz,l3bf1h		;3bec	20 03		  .
	ld a,e			;3bee	7b		{
	jr l3bd4h		;3bef	18 e3		. .
l3bf1h:
	jr c,l3bd8h		;3bf1	38 e5		8 .
	xor a			;3bf3	af		.
	jr l3bd8h		;3bf4	18 e2		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KbdTables3BF6 - keyboard decode tables.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KbdTables3BF6' (start 0x3bf6 end 0x3c1a)
KbdTables3BF6_start:
	defb 0e1h		;3bf6	e1		.
	defb 07ch		;3bf7	7c		|
	defb 0b5h		;3bf8	b5		.
	defb 028h		;3bf9	28		(
	defb 008h		;3bfa	08		.
	defb 07ah		;3bfb	7a		z
	defb 0cdh		;3bfc	cd		.
	defb 011h		;3bfd	11		.
	defb 03ch		;3bfe	3c		<
	defb 0d1h		;3bff	d1		.
	defb 0c3h		;3c00	c3		.
	defb 033h		;3c01	33		3
	defb 03bh		;3c02	3b		;
	defb 079h		;3c03	79		y
	defb 03dh		;3c04	3d		=
	defb 0cdh		;3c05	cd		.
	defb 08fh		;3c06	8f		.
	defb 03bh		;3c07	3b		;
	defb 0d1h		;3c08	d1		.
	defb 02ch		;3c09	2c		,
	defb 02dh		;3c0a	2d		-
	defb 0c0h		;3c0b	c0		.
	defb 0eeh		;3c0c	ee		.
	defb 020h		;3c0d	20		 
	defb 0c3h		;3c0e	c3		.
	defb 033h		;3c0f	33		3
	defb 03bh		;3c10	3b		;
	defb 0e9h		;3c11	e9		.
	defb 0afh		;3c12	af		.
	defb 0d1h		;3c13	d1		.
l3c14h:
	defb 0c9h		;3c14	c9		.
	defb 000h		;3c15	00		.
	defb 021h		;3c16	21		!
	defb 030h		;3c17	30		0
	defb 03ah		;3c18	3a		:
	defb 041h		;3c19	41		A
KbdTables3BF6_end:
	ld e,e			;3c1a	5b		[
	ld h,c			;3c1b	61		a
	ld a,e			;3c1c	7b		{
	nop			;3c1d	00		.
	rst 38h			;3c1e	ff		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KbdCtrlMap3C1F - control-key mapping pairs.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KbdCtrlMap3C1F' (start 0x3c1f end 0x3c36)
KbdCtrlMap3C1F_start:
	defb 063h		;3c1f	63		c
	defb 023h		;3c20	23		#
	defb 062h		;3c21	62		b
	defb 022h		;3c22	22		"
	defb 064h		;3c23	64		d
	defb 024h		;3c24	24		$
	defb 061h		;3c25	61		a
	defb 021h		;3c26	21		!
	defb 033h		;3c27	33		3
	defb 034h		;3c28	34		4
	defb 014h		;3c29	14		.
	defb 051h		;3c2a	51		Q
	defb 031h		;3c2b	31		1
	defb 011h		;3c2c	11		.
	defb 030h		;3c2d	30		0
	defb 053h		;3c2e	53		S
	defb 043h		;3c2f	43		C
	defb 013h		;3c30	13		.
	defb 003h		;3c31	03		.
	defb 052h		;3c32	52		R
	defb 042h		;3c33	42		B
	defb 054h		;3c34	54		T
	defb 044h		;3c35	44		D
KbdCtrlMap3C1F_end:
	bit 2,(ix-07fh)		;3c36	dd cb 81 56	. . . V
	jr z,l3c5ch		;3c3a	28 20		(  
	call sub_3d0ah		;3c3c	cd 0a 3d	. . =
	or a			;3c3f	b7		.
	jr nz,l3c5ch		;3c40	20 1a		  .
	ld a,001h		;3c42	3e 01		> .
	rst 18h			;3c44	df		.
	push af			;3c45	f5		.
	call l0586h		;3c46	cd 86 05	. . .
	pop af			;3c49	f1		.
	rst 18h			;3c4a	df		.
	ei			;3c4b	fb		.
	call l3c5ch		;3c4c	cd 5c 3c	. \ <
	push af			;3c4f	f5		.
	ld a,001h		;3c50	3e 01		> .
	rst 18h			;3c52	df		.
	push af			;3c53	f5		.
	call sub_05a0h		;3c54	cd a0 05	. . .
	pop af			;3c57	f1		.
	rst 18h			;3c58	df		.
	ei			;3c59	fb		.
	pop af			;3c5a	f1		.
	ret			;3c5b	c9		.
l3c5ch:
	ei			;3c5c	fb		.
	halt			;3c5d	76		v
	call sub_3c6ah		;3c5e	cd 6a 3c	. j <
	ld (ix-039h),000h	;3c61	dd 36 c7 00	. 6 . .
	ld (ix-037h),000h	;3c65	dd 36 c9 00	. 6 . .
	ret			;3c69	c9		.
sub_3c6ah:
	ld (ix-069h),001h	;3c6a	dd 36 97 01	. 6 . .
sub_3c6eh:
	ld a,(05f86h)		;3c6e	3a 86 5f	: . _
	ld (ix-039h),000h	;3c71	dd 36 c7 00	. 6 . .
	or a			;3c75	b7		.
	ret nz			;3c76	c0		.
l3c77h:
	ld e,(ix-069h)		;3c77	dd 5e 97	. ^ .
	call DriverTable3932_start	;3c7a	cd 32 39	. 2 9
	or a			;3c7d	b7		.
	ld a,000h		;3c7e	3e 00		> .
	scf			;3c80	37		7
	ret nz			;3c81	c0		.
	ld a,e			;3c82	7b		{
	xor 010h		;3c83	ee 10		. .
	ld e,a			;3c85	5f		_
	ld a,l			;3c86	7d		}
	ld (05f89h),a		;3c87	32 89 5f	2 . _
	call sub_3adah		;3c8a	cd da 3a	. . :
	ld (ix-038h),e		;3c8d	dd 73 c8	. s .
	ld (ix-037h),d		;3c90	dd 72 c9	. r .
	ret nc			;3c93	d0		.
	ld a,e			;3c94	7b		{
	and 006h		;3c95	e6 06		. .
	cp 006h			;3c97	fe 06		. .
	jr z,l3ccch		;3c99	28 31		( 1
	cp 004h			;3c9b	fe 04		. .
	jr z,KbdTables3CB3_end	;3c9d	28 25		( %
	ld a,d			;3c9f	7a		z
	cp 07ah			;3ca0	fe 7a		. z
	jr z,l3cf9h		;3ca2	28 55		( U
	cp 06fh			;3ca4	fe 6f		. o
	jr z,l3cf0h		;3ca6	28 48		( H
	cp 06eh			;3ca8	fe 6e		. n
	jr nz,KbdTables3CB3_start	;3caa	20 07		  .
	ld e,001h		;3cac	1e 01		. .
	call DriverTable3932_start	;3cae	cd 32 39	. 2 9
	jr l3c77h		;3cb1	18 c4		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KbdTables3CB3 - keyboard decode tables.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KbdTables3CB3' (start 0x3cb3 end 0x3cc4)
KbdTables3CB3_start:
	defb 0d6h		;3cb3	d6		.
	defb 070h		;3cb4	70		p
	defb 038h		;3cb5	38		8
	defb 00dh		;3cb6	0d		.
	defb 0feh		;3cb7	fe		.
	defb 00bh		;3cb8	0b		.
	defb 030h		;3cb9	30		0
	defb 009h		;3cba	09		.
	defb 021h		;3cbb	21		!
	defb 000h		;3cbc	00		.
	defb 03dh		;3cbd	3d		=
	defb 0cdh		;3cbe	cd		.
	defb 006h		;3cbf	06		.
	defb 011h		;3cc0	11		.
	defb 07eh		;3cc1	7e		~
	defb 0b7h		;3cc2	b7		.
	defb 0c0h		;3cc3	c0		.
KbdTables3CB3_end:
	bit 1,(ix-069h)		;3cc4	dd cb 97 4e	. . . N
	jr z,l3c77h		;3cc8	28 ad		( .
	xor a			;3cca	af		.
	ret			;3ccb	c9		.
l3ccch:
	ld a,d			;3ccc	7a		z
	cp 0abh			;3ccd	fe ab		. .
	jr z,l3ce1h		;3ccf	28 10		( .
	cp 0adh			;3cd1	fe ad		. .
	jr z,l3ce7h		;3cd3	28 12		( .
	cp 0aeh			;3cd5	fe ae		. .
	jp z,WbootStub		;3cd7	ca 00 00	. . .
	cp 079h			;3cda	fe 79		. y
	jr nz,l3c77h		;3cdc	20 99		  .
	jp WbootStub		;3cde	c3 00 00	. . .
l3ce1h:
	set 3,(ix-062h)		;3ce1	dd cb 9e de	. . . .
	jr l3cebh		;3ce5	18 04		. .
l3ce7h:
	res 3,(ix-062h)		;3ce7	dd cb 9e 9e	. . . .
l3cebh:
	call sub_3872h		;3ceb	cd 72 38	. r 8
	jr l3c77h		;3cee	18 87		. .
l3cf0h:
	bit 3,(ix-07eh)		;3cf0	dd cb 82 5e	. . . ^
	jr nz,KbdTables3CB3_end	;3cf4	20 ce		  .
	jp 0f864h		;3cf6	c3 64 f8	. d .
l3cf9h:
	ld a,00eh		;3cf9	3e 0e		> .
	bit 7,e			;3cfb	cb 7b		. {
	ret nz			;3cfd	c0		.
	inc a			;3cfe	3c		<
	ret			;3cff	c9		.
	dec b			;3d00	05		.
	jr l3d16h		;3d01	18 13		. .
	inc b			;3d03	04		.
	ld (de),a		;3d04	12		.
	inc bc			;3d05	03		.
	nop			;3d06	00		.
	nop			;3d07	00		.
	rrca			;3d08	0f		.
	rlca			;3d09	07		.
sub_3d0ah:
	ld (ix-069h),000h	;3d0a	dd 36 97 00	. 6 . .
	call sub_3c6eh		;3d0e	cd 6e 3c	. n <
	jr c,l3d19h		;3d11	38 06		8 .
	ld (05f86h),a		;3d13	32 86 5f	2 . _
l3d16h:
	ld a,0ffh		;3d16	3e ff		> .
	ret			;3d18	c9		.
l3d19h:
	xor a			;3d19	af		.
	ret			;3d1a	c9		.
sub_3d1bh:
	ld a,(05f88h)		;3d1b	3a 88 5f	: . _
	or a			;3d1e	b7		.
	ld h,000h		;3d1f	26 00		& .
	scf			;3d21	37		7
	ret z			;3d22	c8		.
	cp 078h			;3d23	fe 78		. x
	jr z,ZxKeyboardRows_start	;3d25	28 3f		( ?
	cp 061h			;3d27	fe 61		. a
	jr c,l3d45h		;3d29	38 1a		8 .
	cp 06fh			;3d2b	fe 6f		. o
	jr nc,l3d45h		;3d2d	30 16		0 .
l3d2fh:
	sub 061h		;3d2f	d6 61		. a
	ld hl,l3d3bh		;3d31	21 3b 3d	! ; =
	call sub_1106h		;3d34	cd 06 11	. . .
	ld h,(hl)		;3d37	66		f
	ld d,0c0h		;3d38	16 c0		. .
	ret			;3d3a	c9		.
l3d3bh:
	inc bc			;3d3b	03		.
	dec bc			;3d3c	0b		.
	inc de			;3d3d	13		.
	dec de			;3d3e	1b		.
	inc hl			;3d3f	23		#
	inc h			;3d40	24		$
	inc e			;3d41	1c		.
	inc d			;3d42	14		.
	inc c			;3d43	0c		.
	inc b			;3d44	04		.
l3d45h:
	ld hl,l3d7eh		;3d45	21 7e 3d	! ~ =
	ld bc,l0041h		;3d48	01 41 00	. A .
	cpir			;3d4b	ed b1		. .
	call nz,sub_3d6dh	;3d4d	c4 6d 3d	. m =
	ld a,041h		;3d50	3e 41		> A
	sub c			;3d52	91		.
	ld h,a			;3d53	67		g
l3d54h:
	ld d,000h		;3d54	16 00		. .
	or a			;3d56	b7		.
	bit 0,e			;3d57	cb 43		. C
	jr z,l3d5eh		;3d59	28 03		( .
	set 7,d			;3d5b	cb fa		. .
	ret			;3d5d	c9		.
l3d5eh:
	bit 1,e			;3d5e	cb 4b		. K
	ret z			;3d60	c8		.
	set 7,d			;3d61	cb fa		. .
	set 6,d			;3d63	cb f2		. .
	ret			;3d65	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ZxKeyboardRows - ZX keyboard half-row matrix.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'ZxKeyboardRows' (start 0x3d66 end 0x3dbf)
ZxKeyboardRows_start:
	defb 0ddh		;3d66	dd		.
	defb 036h		;3d67	36		6
	defb 0c7h		;3d68	c7		.
	defb 05eh		;3d69	5e		^
	defb 026h		;3d6a	26		&
	defb 033h		;3d6b	33		3
	defb 0c9h		;3d6c	c9		.
sub_3d6dh:
	defb 03ah		;3d6d	3a		:
	defb 089h		;3d6e	89		.
	defb 05fh		;3d6f	5f		_
	defb 021h		;3d70	21		!
	defb 07eh		;3d71	7e		~
	defb 03dh		;3d72	3d		=
	defb 001h		;3d73	01		.
	defb 041h		;3d74	41		A
	defb 000h		;3d75	00		.
	defb 0edh		;3d76	ed		.
	defb 0b1h		;3d77	b1		.
	defb 0c8h		;3d78	c8		.
	defb 0f1h		;3d79	f1		.
	defb 026h		;3d7a	26		&
	defb 03fh		;3d7b	3f		?
	defb 0b7h		;3d7c	b7		.
	defb 0c9h		;3d7d	c9		.
l3d7eh:
	defb 041h		;3d7e	41		A
	defb 051h		;3d7f	51		Q
	defb 031h		;3d80	31		1
	defb 030h		;3d81	30		0
	defb 050h		;3d82	50		P
	defb 00dh		;3d83	0d		.
	defb 020h		;3d84	20		 
	defb 05ah		;3d85	5a		Z
	defb 053h		;3d86	53		S
	defb 057h		;3d87	57		W
	defb 032h		;3d88	32		2
	defb 039h		;3d89	39		9
	defb 04fh		;3d8a	4f		O
	defb 04ch		;3d8b	4c		L
	defb 000h		;3d8c	00		.
	defb 058h		;3d8d	58		X
	defb 044h		;3d8e	44		D
	defb 045h		;3d8f	45		E
	defb 033h		;3d90	33		3
	defb 038h		;3d91	38		8
	defb 049h		;3d92	49		I
	defb 04bh		;3d93	4b		K
	defb 04dh		;3d94	4d		M
	defb 043h		;3d95	43		C
	defb 046h		;3d96	46		F
	defb 052h		;3d97	52		R
	defb 034h		;3d98	34		4
	defb 037h		;3d99	37		7
	defb 055h		;3d9a	55		U
	defb 04ah		;3d9b	4a		J
	defb 04eh		;3d9c	4e		N
	defb 056h		;3d9d	56		V
	defb 047h		;3d9e	47		G
	defb 054h		;3d9f	54		T
	defb 035h		;3da0	35		5
	defb 036h		;3da1	36		6
	defb 059h		;3da2	59		Y
	defb 048h		;3da3	48		H
	defb 042h		;3da4	42		B
	defb 071h		;3da5	71		q
	defb 072h		;3da6	72		r
	defb 070h		;3da7	70		p
	defb 07ah		;3da8	7a		z
	defb 073h		;3da9	73		s
	defb 009h		;3daa	09		.
	defb 000h		;3dab	00		.
	defb 000h		;3dac	00		.
	defb 008h		;3dad	08		.
	defb 01bh		;3dae	1b		.
	defb 0aah		;3daf	aa		.
	defb 000h		;3db0	00		.
	defb 0afh		;3db1	af		.
	defb 02ch		;3db2	2c		,
	defb 0adh		;3db3	ad		.
	defb 03fh		;3db4	3f		?
	defb 02eh		;3db5	2e		.
	defb 0abh		;3db6	ab		.
	defb 05bh		;3db7	5b		[
	defb 060h		;3db8	60		`
	defb 03dh		;3db9	3d		=
	defb 03bh		;3dba	3b		;
	defb 05dh		;3dbb	5d		]
	defb 03ah		;3dbc	3a		:
	defb 022h		;3dbd	22		"
	defb 05fh		;3dbe	5f		_
ZxKeyboardRows_end:
	ld (ix-069h),000h	;3dbf	dd 36 97 00	. 6 . .
	ld a,(05f88h)		;3dc3	3a 88 5f	: . _
	or a			;3dc6	b7		.
	call z,sub_3de8h	;3dc7	cc e8 3d	. . =
	ld a,001h		;3dca	3e 01		> .
	ld c,000h		;3dcc	0e 00		. .
	jr c,l3d54h		;3dce	38 84		8 .
	call sub_3d1bh		;3dd0	cd 1b 3d	. . =
	ld (ix-037h),000h	;3dd3	dd 36 c9 00	. 6 . .
	ld a,001h		;3dd7	3e 01		> .
	ld c,000h		;3dd9	0e 00		. .
	jr c,l3de1h		;3ddb	38 04		8 .
	xor a			;3ddd	af		.
	ld c,(ix-039h)		;3dde	dd 4e c7	. N .
l3de1h:
	ld l,001h		;3de1	2e 01		. .
	ld (ix-039h),000h	;3de3	dd 36 c7 00	. 6 . .
	ret			;3de7	c9		.
sub_3de8h:
	call l3c77h		;3de8	cd 77 3c	. w <
	ld (ix-039h),000h	;3deb	dd 36 c7 00	. 6 . .
	ret c			;3def	d8		.
	ld (05f86h),a		;3df0	32 86 5f	2 . _
	ret			;3df3	c9		.
l3df4h:
	ld (ix-069h),000h	;3df4	dd 36 97 00	. 6 . .
	ld a,(05f88h)		;3df8	3a 88 5f	: . _
	or a			;3dfb	b7		.
	call z,sub_3de8h	;3dfc	cc e8 3d	. . =
	ld a,001h		;3dff	3e 01		> .
	ld l,a			;3e01	6f		o
	ld e,(ix-038h)		;3e02	dd 5e c8	. ^ .
	jr c,DriverDispatch	;3e05	38 01		8 .
	xor a			;3e07	af		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DriverDispatch - 7 x JP local, the driver sub-dispatch:
; JP $3D54 / $392E / $3C36 / $3D0A / $3DBF / $39F4 / $392F (all 1:1).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DriverDispatch:
	jp l3d54h		;3e08	c3 54 3d	. T =
l3e0bh:
	jp DriverTable3916_end	;3e0b	c3 2e 39	. . 9
	jp KbdCtrlMap3C1F_end	;3e0e	c3 36 3c	. 6 <
	jp sub_3d0ah		;3e11	c3 0a 3d	. . =
	jp ZxKeyboardRows_end	;3e14	c3 bf 3d	. . =
	jp l3df4h		;3e17	c3 f4 3d	. . =
	jp l392fh		;3e1a	c3 2f 39	. / 9
	nop			;3e1d	00		.
	nop			;3e1e	00		.
	nop			;3e1f	00		.
	nop			;3e20	00		.
	nop			;3e21	00		.
	nop			;3e22	00		.
	nop			;3e23	00		.
	nop			;3e24	00		.
	nop			;3e25	00		.
	nop			;3e26	00		.
	nop			;3e27	00		.
	nop			;3e28	00		.
	nop			;3e29	00		.
	nop			;3e2a	00		.
	nop			;3e2b	00		.
	nop			;3e2c	00		.
	nop			;3e2d	00		.
	nop			;3e2e	00		.
	nop			;3e2f	00		.
	nop			;3e30	00		.
	nop			;3e31	00		.
	nop			;3e32	00		.
	nop			;3e33	00		.
	nop			;3e34	00		.
	nop			;3e35	00		.
	nop			;3e36	00		.
	nop			;3e37	00		.
	nop			;3e38	00		.
	nop			;3e39	00		.
	nop			;3e3a	00		.
	nop			;3e3b	00		.
	nop			;3e3c	00		.
	nop			;3e3d	00		.
	nop			;3e3e	00		.
	nop			;3e3f	00		.
	nop			;3e40	00		.
	nop			;3e41	00		.
	nop			;3e42	00		.
	nop			;3e43	00		.
	nop			;3e44	00		.
	nop			;3e45	00		.
	nop			;3e46	00		.
	nop			;3e47	00		.
	nop			;3e48	00		.
	nop			;3e49	00		.
	nop			;3e4a	00		.
	nop			;3e4b	00		.
	nop			;3e4c	00		.
	nop			;3e4d	00		.
	nop			;3e4e	00		.
	nop			;3e4f	00		.
	nop			;3e50	00		.
	nop			;3e51	00		.
	nop			;3e52	00		.
	nop			;3e53	00		.
	nop			;3e54	00		.
	nop			;3e55	00		.
	nop			;3e56	00		.
	nop			;3e57	00		.
	nop			;3e58	00		.
	nop			;3e59	00		.
	nop			;3e5a	00		.
	nop			;3e5b	00		.
	nop			;3e5c	00		.
	nop			;3e5d	00		.
	nop			;3e5e	00		.
	nop			;3e5f	00		.
	nop			;3e60	00		.
	nop			;3e61	00		.
	nop			;3e62	00		.
	nop			;3e63	00		.
	nop			;3e64	00		.
	nop			;3e65	00		.
	nop			;3e66	00		.
	nop			;3e67	00		.
	nop			;3e68	00		.
	nop			;3e69	00		.
	nop			;3e6a	00		.
	nop			;3e6b	00		.
	nop			;3e6c	00		.
	nop			;3e6d	00		.
	nop			;3e6e	00		.
	nop			;3e6f	00		.
	nop			;3e70	00		.
	nop			;3e71	00		.
	nop			;3e72	00		.
	nop			;3e73	00		.
	nop			;3e74	00		.
	nop			;3e75	00		.
	nop			;3e76	00		.
	nop			;3e77	00		.
	nop			;3e78	00		.
	nop			;3e79	00		.
	nop			;3e7a	00		.
	nop			;3e7b	00		.
	nop			;3e7c	00		.
	nop			;3e7d	00		.
	nop			;3e7e	00		.
	nop			;3e7f	00		.
	nop			;3e80	00		.
	nop			;3e81	00		.
	nop			;3e82	00		.
	nop			;3e83	00		.
	nop			;3e84	00		.
	nop			;3e85	00		.
	nop			;3e86	00		.
	nop			;3e87	00		.
	nop			;3e88	00		.
	nop			;3e89	00		.
	nop			;3e8a	00		.
	nop			;3e8b	00		.
	nop			;3e8c	00		.
	nop			;3e8d	00		.
	nop			;3e8e	00		.
	nop			;3e8f	00		.
	nop			;3e90	00		.
	nop			;3e91	00		.
	nop			;3e92	00		.
	nop			;3e93	00		.
	nop			;3e94	00		.
	nop			;3e95	00		.
	nop			;3e96	00		.
	nop			;3e97	00		.
	nop			;3e98	00		.
	nop			;3e99	00		.
	nop			;3e9a	00		.
	nop			;3e9b	00		.
	nop			;3e9c	00		.
	nop			;3e9d	00		.
	nop			;3e9e	00		.
	nop			;3e9f	00		.
	nop			;3ea0	00		.
	nop			;3ea1	00		.
	nop			;3ea2	00		.
	nop			;3ea3	00		.
	nop			;3ea4	00		.
	nop			;3ea5	00		.
	nop			;3ea6	00		.
	nop			;3ea7	00		.
	nop			;3ea8	00		.
	nop			;3ea9	00		.
	nop			;3eaa	00		.
	nop			;3eab	00		.
	nop			;3eac	00		.
	nop			;3ead	00		.
	nop			;3eae	00		.
	nop			;3eaf	00		.
	nop			;3eb0	00		.
	nop			;3eb1	00		.
	nop			;3eb2	00		.
	nop			;3eb3	00		.
	nop			;3eb4	00		.
	nop			;3eb5	00		.
	nop			;3eb6	00		.
	nop			;3eb7	00		.
	nop			;3eb8	00		.
	nop			;3eb9	00		.
	nop			;3eba	00		.
	nop			;3ebb	00		.
	nop			;3ebc	00		.
	nop			;3ebd	00		.
	nop			;3ebe	00		.
	nop			;3ebf	00		.
	nop			;3ec0	00		.
	nop			;3ec1	00		.
	nop			;3ec2	00		.
	nop			;3ec3	00		.
	nop			;3ec4	00		.
	nop			;3ec5	00		.
	nop			;3ec6	00		.
	nop			;3ec7	00		.
	nop			;3ec8	00		.
	nop			;3ec9	00		.
	nop			;3eca	00		.
	nop			;3ecb	00		.
	nop			;3ecc	00		.
	nop			;3ecd	00		.
	nop			;3ece	00		.
	nop			;3ecf	00		.
	nop			;3ed0	00		.
	nop			;3ed1	00		.
	nop			;3ed2	00		.
	nop			;3ed3	00		.
	nop			;3ed4	00		.
	nop			;3ed5	00		.
	nop			;3ed6	00		.
	nop			;3ed7	00		.
	nop			;3ed8	00		.
	nop			;3ed9	00		.
	nop			;3eda	00		.
	nop			;3edb	00		.
	nop			;3edc	00		.
	nop			;3edd	00		.
	nop			;3ede	00		.
	nop			;3edf	00		.
	nop			;3ee0	00		.
	nop			;3ee1	00		.
	nop			;3ee2	00		.
	nop			;3ee3	00		.
	nop			;3ee4	00		.
	nop			;3ee5	00		.
	nop			;3ee6	00		.
	nop			;3ee7	00		.
	nop			;3ee8	00		.
	nop			;3ee9	00		.
	nop			;3eea	00		.
	nop			;3eeb	00		.
	nop			;3eec	00		.
	nop			;3eed	00		.
	nop			;3eee	00		.
	nop			;3eef	00		.
	nop			;3ef0	00		.
	nop			;3ef1	00		.
	nop			;3ef2	00		.
	nop			;3ef3	00		.
	nop			;3ef4	00		.
	nop			;3ef5	00		.
	nop			;3ef6	00		.
	nop			;3ef7	00		.
	nop			;3ef8	00		.
	nop			;3ef9	00		.
	nop			;3efa	00		.
	nop			;3efb	00		.
	nop			;3efc	00		.
	nop			;3efd	00		.
	nop			;3efe	00		.
	ld a,e			;3eff	7b		{
	jr c,RomTailPad_end	;3f00	38 64		8 d
	dec b			;3f02	05		.
	ld (bc),a		;3f03	02		.
	nop			;3f04	00		.
	ld h,(hl)		;3f05	66		f
	ld h,l			;3f06	65		e
	ld h,h			;3f07	64		d
	dec b			;3f08	05		.
	ld (bc),a		;3f09	02		.
	add a,b			;3f0a	80		.
	ld h,(hl)		;3f0b	66		f
	ld h,l			;3f0c	65		e
l3f0dh:
	ld b,005h		;3f0d	06 05		. .
	ld (bc),a		;3f0f	02		.
l3f10h:
	nop			;3f10	00		.
	ld b,007h		;3f11	06 07		. .
	ld b,005h		;3f13	06 05		. .
	ld (bc),a		;3f15	02		.
	add a,b			;3f16	80		.
	ld b,007h		;3f17	06 07		. .
l3f19h:
	inc e			;3f19	1c		.
	dec b			;3f1a	05		.
	ld (bc),a		;3f1b	02		.
	nop			;3f1c	00		.
	dec e			;3f1d	1d		.
	ld e,01ch		;3f1e	1e 1c		. .
	dec b			;3f20	05		.
	ld (bc),a		;3f21	02		.
	add a,b			;3f22	80		.
	dec e			;3f23	1d		.
	ld e,02bh		;3f24	1e 2b		. +
	ccf			;3f26	3f		?
	ld (hl),03fh		;3f27	36 3f		6 ?
	ld b,b			;3f29	40		@
	ccf			;3f2a	3f		?
	out (c),e		;3f2b	ed 59		. Y
	nop			;3f2d	00		.
	ld a,030h		;3f2e	3e 30		> 0
	ld bc,07ffdh		;3f30	01 fd 7f	. . .
	out (c),a		;3f33	ed 79		. y
	rst 0			;3f35	c7		.
	out (c),e		;3f36	ed 59		. Y
	nop			;3f38	00		.
	xor a			;3f39	af		.
	ld bc,07ffdh		;3f3a	01 fd 7f	. . .
	out (c),a		;3f3d	ed 79		. y
	rst 0			;3f3f	c7		.
	out (c),e		;3f40	ed 59		. Y
	nop			;3f42	00		.
	ld bc,07ffdh		;3f43	01 fd 7f	. . .
	ld a,030h		;3f46	3e 30		> 0
	out (c),a		;3f48	ed 79		. y
	ld bc,WbootStub		;3f4a	01 00 00	. . .
	push bc			;3f4d	c5		.
	jp l3d2fh		;3f4e	c3 2f 3d	. / =
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RomTailPad - trailing zero padding to the 16 KiB boundary.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'RomTailPad' (start 0x3f51 end 0x3f66)
RomTailPad_start:
	defb 000h		;3f51	00		.
	defb 000h		;3f52	00		.
	defb 000h		;3f53	00		.
	defb 000h		;3f54	00		.
	defb 000h		;3f55	00		.
	defb 000h		;3f56	00		.
	defb 000h		;3f57	00		.
	defb 000h		;3f58	00		.
	defb 000h		;3f59	00		.
	defb 000h		;3f5a	00		.
	defb 000h		;3f5b	00		.
	defb 000h		;3f5c	00		.
	defb 000h		;3f5d	00		.
	defb 000h		;3f5e	00		.
	defb 000h		;3f5f	00		.
	defb 000h		;3f60	00		.
	defb 000h		;3f61	00		.
	defb 000h		;3f62	00		.
	defb 000h		;3f63	00		.
	defb 000h		;3f64	00		.
	defb 000h		;3f65	00		.
RomTailPad_end:
	nop			;3f66	00		.
	nop			;3f67	00		.
	nop			;3f68	00		.
	nop			;3f69	00		.
	nop			;3f6a	00		.
	nop			;3f6b	00		.
	nop			;3f6c	00		.
	nop			;3f6d	00		.
	nop			;3f6e	00		.
	nop			;3f6f	00		.
	nop			;3f70	00		.
	nop			;3f71	00		.
	nop			;3f72	00		.
	nop			;3f73	00		.
	nop			;3f74	00		.
	nop			;3f75	00		.
	nop			;3f76	00		.
	nop			;3f77	00		.
	nop			;3f78	00		.
	nop			;3f79	00		.
	nop			;3f7a	00		.
	nop			;3f7b	00		.
	nop			;3f7c	00		.
	nop			;3f7d	00		.
	nop			;3f7e	00		.
	nop			;3f7f	00		.
	nop			;3f80	00		.
	nop			;3f81	00		.
	nop			;3f82	00		.
	nop			;3f83	00		.
	nop			;3f84	00		.
	nop			;3f85	00		.
	nop			;3f86	00		.
	nop			;3f87	00		.
	nop			;3f88	00		.
	nop			;3f89	00		.
	nop			;3f8a	00		.
	nop			;3f8b	00		.
	nop			;3f8c	00		.
	nop			;3f8d	00		.
	nop			;3f8e	00		.
	nop			;3f8f	00		.
	nop			;3f90	00		.
	nop			;3f91	00		.
	nop			;3f92	00		.
	nop			;3f93	00		.
	nop			;3f94	00		.
	nop			;3f95	00		.
	nop			;3f96	00		.
	nop			;3f97	00		.
	nop			;3f98	00		.
	nop			;3f99	00		.
	nop			;3f9a	00		.
	nop			;3f9b	00		.
	nop			;3f9c	00		.
	nop			;3f9d	00		.
	nop			;3f9e	00		.
	nop			;3f9f	00		.
	nop			;3fa0	00		.
	nop			;3fa1	00		.
	nop			;3fa2	00		.
	nop			;3fa3	00		.
	nop			;3fa4	00		.
	nop			;3fa5	00		.
	nop			;3fa6	00		.
	nop			;3fa7	00		.
	nop			;3fa8	00		.
	nop			;3fa9	00		.
	nop			;3faa	00		.
	nop			;3fab	00		.
	nop			;3fac	00		.
	nop			;3fad	00		.
	nop			;3fae	00		.
	nop			;3faf	00		.
	nop			;3fb0	00		.
	nop			;3fb1	00		.
	nop			;3fb2	00		.
	nop			;3fb3	00		.
	nop			;3fb4	00		.
	nop			;3fb5	00		.
	nop			;3fb6	00		.
	nop			;3fb7	00		.
	nop			;3fb8	00		.
	nop			;3fb9	00		.
	nop			;3fba	00		.
	nop			;3fbb	00		.
	nop			;3fbc	00		.
	nop			;3fbd	00		.
	nop			;3fbe	00		.
	nop			;3fbf	00		.
	nop			;3fc0	00		.
	nop			;3fc1	00		.
	nop			;3fc2	00		.
	nop			;3fc3	00		.
	nop			;3fc4	00		.
	nop			;3fc5	00		.
	nop			;3fc6	00		.
	nop			;3fc7	00		.
	nop			;3fc8	00		.
	nop			;3fc9	00		.
	nop			;3fca	00		.
	nop			;3fcb	00		.
	nop			;3fcc	00		.
	nop			;3fcd	00		.
	nop			;3fce	00		.
	nop			;3fcf	00		.
	nop			;3fd0	00		.
	nop			;3fd1	00		.
	nop			;3fd2	00		.
	nop			;3fd3	00		.
	nop			;3fd4	00		.
	nop			;3fd5	00		.
	nop			;3fd6	00		.
	nop			;3fd7	00		.
	nop			;3fd8	00		.
	nop			;3fd9	00		.
	nop			;3fda	00		.
	nop			;3fdb	00		.
	nop			;3fdc	00		.
	nop			;3fdd	00		.
	nop			;3fde	00		.
	nop			;3fdf	00		.
	nop			;3fe0	00		.
	nop			;3fe1	00		.
	nop			;3fe2	00		.
	nop			;3fe3	00		.
	nop			;3fe4	00		.
	nop			;3fe5	00		.
	nop			;3fe6	00		.
	nop			;3fe7	00		.
	nop			;3fe8	00		.
	nop			;3fe9	00		.
	nop			;3fea	00		.
	nop			;3feb	00		.
	nop			;3fec	00		.
	nop			;3fed	00		.
	nop			;3fee	00		.
	nop			;3fef	00		.
	nop			;3ff0	00		.
	nop			;3ff1	00		.
	nop			;3ff2	00		.
	nop			;3ff3	00		.
	nop			;3ff4	00		.
	nop			;3ff5	00		.
	nop			;3ff6	00		.
	nop			;3ff7	00		.
	nop			;3ff8	00		.
	nop			;3ff9	00		.
	nop			;3ffa	00		.
	nop			;3ffb	00		.
	nop			;3ffc	00		.
	nop			;3ffd	00		.
	or d			;3ffe	b2		.
l3fffh:
	nop			;3fff	00		.
