; ============================================================================
;  Scorpion ZS-256 Turbo - SERVICE MONITOR  (page 2 of scorpion.rom)
; ============================================================================
;
;  Source   : data/rom/scorpion.rom, bytes $8000-$BFFF (page 2), 16 KiB
;  Family   : Scorpion service monitor, v2.x ("base")
;  Sisters  : scorp295.rom      p2 = v2.95  (boot ROM checksum, code moved),
;             scorp_prof401.rom p2 = v4.01 (ProfRAM layout, RAM hooks).
;  Method   : z80dasm 1.2.0, code/data block map from a dedicated
;             Z80 length-decoder + reachability analysis; labels and
;             comments added by hand after studying the three listings.
;  Style    : educational, after Logan & O'Hara, "The Complete Spectrum
;             ROM Disassembly" (see docs/rom for the inspiration).
;
; ----------------------------------------------------------------------------
;  THE ROM BUNDLE
; ----------------------------------------------------------------------------
;  scorpion.rom is a 64 KiB bundle of four 16 KiB pages.  Hash comparison
;  against data/rom/*.rom shows the real contents (page numbers here are
;  offsets inside the bundle file, NOT the emulator's slot numbering):
;
;    page 0 ($0000-)  patched standard 128K ROM0 (~290 bytes differ from
;                     128.rom - TR-DOS autorun patches)
;    page 1 ($4000-)  patched 48K BASIC ROM (~115 bytes differ from
;                     sos.rom - same autorun patch family)
;    page 2 ($8000-)  THIS service monitor (machine-code debugger)
;    page 3 ($C000-)  TR-DOS 5.03 variant (strings identical to dos.rom,
;                     ~4.7K of bytes differ - Scorpion adaptations)
;
;  NOTE: core/src/emulator/memory/rom.cpp maps MM_SCORP slots as
;  sys=p0/dos=p1/128=p2/sos=p3 - inverted with respect to this bundle.
;
; ----------------------------------------------------------------------------
;  PAGING AS THE MONITOR USES IT
; ----------------------------------------------------------------------------
;  #7FFD  bit 4 = 0 selects RAM bank 0 at $C000; bank bits select the
;         RAM bank.  The monitor keeps $10 here while it runs, so its
;         workspace in bank 0 ($C000-$FFFF) stays visible.
;  #1FFD  Scorpion extension port.  The monitor writes $12 to map itself
;         (service page visible in the low 16K), $02 around TR-DOS
;         sessions, and $10/$12 pairs to restore.  Exact hardware
;         priority of the bits is not fully modelled here - wording
;         below sticks to the values actually written.
;
; ----------------------------------------------------------------------------
;  MONITOR RST API  (stub at $0000-$0038, identical in all versions)
; ----------------------------------------------------------------------------
;    RST 00h  write one byte through the banked window (carry = 1)
;    RST 08h  report error, code in B
;    RST 10h  print character in A
;    RST 18h  RAM-extension call: inline 16-bit target -> (RamExtTarget),
;             then JP through the RAM dispatcher at (RamExtDispatcher)
;    RST 20h  print inline bit-7-terminated message (EX (SP),HL trick)
;    RST 28h  read one byte through the banked window (carry = 0)
;    RST 30h  set IX work buffer from HL (default $E05F via $13A3)
;    RST 38h  INT: short timed loop, then EI: RET
;    NMI      magic-button entry -> context save -> monitor
;
; ----------------------------------------------------------------------------
;  RAM WORKSPACE (bank 0, visible at $C000 while the monitor runs)
; ----------------------------------------------------------------------------
;    $C001/$C002  power-on magic bytes ($55/$AA) - warm/cold boot test
;    $C064        boot pass counter (non-zero = warm boot)
;    $C069-$DB09  monitor own screen buffer (64 column, 6x8 font)
;    $DD6B/$DD6D  user PC / user SP mirrors (context block)
;    $DD73-$DD81  register file: HL,DE,BC,AF,IX,IY,SP (see SaveContext)
;    $DD83/$DD85/$DD86   user IFF2 / I / IM state
;    $DD99+       register backup used by single-step
;    $DDD0        step PC,  $DDD4/$DDD6 breakpoint count/table
;    $DDD8/$DDDA  error message table pointers
;    $DFD7/$DFD8  AY mirror / last key code
;    $DFDD        paging mirror (E = last #7FFD, D = last #1FFD value)
;    $DFDB        paging shadow for TR-DOS trips
;    $DFDF        IY base for the monitor's flags (IY+$00..$16)
;    $E000-$E00C  keyboard state for auto-repeat
;    $E035/$E051/$E06D/$E089  window descriptors (prompt, disasm, popup,
;                              registers) - IX-relative blocks
;    $E05F        default IX work buffer (RST 30h target)
;    $E0AA        step context copy, $E2B7 step trampoline buffer
;    $E2B5        monitor SP,  $E2C1 cursor cell save (16 bytes)
;    $E358/$E373  RAM-extension dispatcher / pending target
;    $E394        watchpoint table: 8 entries x 11 bytes
;    $E7F7        extension menu table (calls RAM code at $EB06)
;    $E80B        command table (2-byte entries, see RunCommand)
;    $E82D        tables decoded at boot from ROM by DecodeXorTable
;    $E928        hardware configuration table (filled by inventory)
;
;  All page-2 addresses in the comments below are offsets in this 16 KiB
;  block (z80dasm origin $0000).  Labels are PascalCase per project rules.
;
;  Trailing data at the end of the page: the boot configuration table
;  (BootConfigTable, $F5E0 - a RAM-side copy is built by DecodeTables)
;  and the monitor's own 6x8 font (Font6x8, $FCA0, 8 bytes per glyph)
;  run up to $3FFF.
;

	org 00000h
ZeroPad3F91_end:	equ 0x4000
RamMagic1:	equ 0xc001
RamMagic2:	equ 0xc002
BootCounter:	equ 0xc064
MonitorScreen:	equ 0xc069
UserPc:	equ 0xdd6b
UserSp:	equ 0xdd6d
RegisterFile:	equ 0xdd73
UserIff:	equ 0xdd83
UserI:	equ 0xdd85
UserIm:	equ 0xdd86
RegisterBackup:	equ 0xdd99
StepPc:	equ 0xddd0
BreakCount:	equ 0xddd4
BreakTable:	equ 0xddd6
ErrorTextTable:	equ 0xddd8
ErrorTextTable2:	equ 0xddda
AyShadowReg:	equ 0xdfd7
LastKeyCode:	equ 0xdfd8
PagingBackup:	equ 0xdfdb
PagingState:	equ 0xdfdd
IyWorkBase:	equ 0xdfdf
StepFlags:	equ 0xdff1
KeyState:	equ 0xe000
PromptWindowDef:	equ 0xe035
DisasmWindowDef:	equ 0xe051
WorkBuffer:	equ 0xe05f
PopupWindowDef:	equ 0xe06d
RegWindowDef:	equ 0xe089
StepContext:	equ 0xe0aa
MonitorStack:	equ 0xe2b5
StepStub:	equ 0xe2b7
CursorCellSave:	equ 0xe2c1
WorkBufferPtr:	equ 0xe2d4
RamExtDispatcher:	equ 0xe358
RamExtTarget:	equ 0xe373
WatchTable:	equ 0xe394
ExtMenuTable:	equ 0xe7f7
CommandTable:	equ 0xe80b
DecodedTables:	equ 0xe82d
HwConfigTable:	equ 0xe928
BootConfigTable:	equ 0xf5e0
Font6x8:	equ 0xfca0
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 00h - WRITE one byte through the banked window.
; SCF presets carry=1; the shared engine at PeekPokeAnyBank ($04D9)
; treats carry as "write".  Twin: RST 28h below enters with carry
; clear = READ.  H bits 6/7 select the window (see $04D9).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

WriteAnyBankByte:
	scf			;0000	37		7
l0001h:
	bit 7,h			;0001	cb 7c		. |
l0003h:
	jp PeekPokeAnyBank	;0003	c3 d9 04	. . .
	jr SoftEntry		;0006	18 61		. a
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 08h - report the error whose code is in B (ReportError $0F7B).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst08Vector:
	jp ReportError		;0008	c3 7b 0f	. { .
l000bh:
	out (c),a		;000b	ed 79		. y
l000dh:
	jp WarmEntry		;000d	c3 9c 00	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 10h - print the character in A (PrintChar $26BE).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst10Vector:
	jp PrintChar		;0010	c3 be 26	. . &
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Filler0013: unused padding between the RST
; vector stubs of the vector page ($0008-$0056).
; The bytes read nop nop jp $00B6 but no path
; enters them.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Filler0013' (start 0x0013 end 0x0018)
Filler0013_start:
	defb 000h		;0013	00		.
	defb 000h		;0014	00		.
	defb 0c3h		;0015	c3		.
	defb 0b6h		;0016	b6		.
	defb 000h		;0017	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 18h - RAM-extension call (RamExtCall $02DC): the 16-bit target
; sits inline after the RST at the call site; dispatch goes through
; the RAM vector at (RamExtDispatcher).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst18Vector:
	jp RamExtCall		;0018	c3 dc 02	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Filler001B: three nop bytes of vector-page
; padding between the RST 18 and RST 20 stubs.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Filler001B' (start 0x001b end 0x001e)
Filler001B_start:
	defb 000h		;001b	00		.
	defb 000h		;001c	00		.
	defb 000h		;001d	00		.
Filler001B_end:
	jr ColdStart		;001e	18 1b		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 20h - print inline message (Rst20Handler $1990): characters
; follow the call site, the last one carries bit 7 set.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst20Vector:
	jp Rst20Handler		;0020	c3 90 19	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Filler0023: vector-page padding (nop nop nop
; jr $0066); never executed.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Filler0023' (start 0x0023 end 0x0028)
Filler0023_start:
	defb 000h		;0023	00		.
l0024h:
	defb 000h		;0024	00		.
	defb 000h		;0025	00		.
	defb 018h		;0026	18		.
	defb 041h		;0027	41		A
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 28h - READ one byte through the banked window (OR A clears
; carry).  Same engine as RST 00h, opposite direction.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ReadAnyBankByte:
	or a			;0028	b7		.
	bit 7,h			;0029	cb 7c		. |
	jr l0003h		;002b	18 d6		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Filler002D: dead jp $00B6 left between the
; RST 20 and RST 30 stubs.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Filler002D' (start 0x002d end 0x0030)
Filler002D_start:
	defb 0c3h		;002d	c3		.
	defb 0b6h		;002e	b6		.
	defb 000h		;002f	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 30h - set the IX work buffer from HL (SetWorkspace $13A6);
; callers wanting the default buffer enter at $13A3 instead.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Filler002D_end:
Rst30Vector:
	jp SetWorkspace		;0030	c3 a6 13	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Filler0033: padding after the RST 30 stub
; (nop nop jp $00B6), unreachable.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Filler0033' (start 0x0033 end 0x0038)
Filler0033_start:
	defb 000h		;0033	00		.
	defb 000h		;0034	00		.
sub_0035h:
	defb 0c3h		;0035	c3		.
	defb 0b6h		;0036	b6		.
	defb 000h		;0037	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; INT vector - timed spin then EI:RET (IntHandler $0092).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst38Vector:
	jp IntHandler		;0038	c3 92 00	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Cold start - entered with the service page mapped (C=$FD assumed
; from the CPU reset state).  Zeroes #7FFD, sets scratch SP=$5BFF,
; then validates the RAM hardware table at HwConfigTable: each
; successive byte must equal the descending counter in B.  A
; mismatch (cold RAM, garbage tables) drops to $007B - inventory,
; window setup, interrupt-mode probe - after which BootContinue
; and the $006C tail re-validate with B=$1F until the boot state
; settles (warm/cold arbitration also lives at MonitorEntry).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ColdStart:
	ld b,07fh		;003b	06 7f		. .
	xor a			;003d	af		.
	out (c),a		;003e	ed 79		. y
	ld sp,05bffh		;0040	31 ff 5b	1 . [
	ld b,000h		;0043	06 00		. .
	jr l0049h		;0045	18 02		. .
l0047h:
	out (c),a		;0047	ed 79		. y
l0049h:
	ld hl,HwConfigTable	;0049	21 28 e9	! ( .
l004ch:
	ld a,b			;004c	78		x
	cp (hl)			;004d	be		.
	inc hl			;004e	23		#
	jr nz,l007bh		;004f	20 2a		  *
	djnz l004ch		;0051	10 f9		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Filler0053: stray instruction bytes (a
; ld hl,(dFF3h)) closing the vector page;
; nothing references them.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Filler0053' (start 0x0053 end 0x0056)
Filler0053_start:
	defb 02ah		;0053	2a		*
	defb 0f3h		;0054	f3		.
	defb 0dfh		;0055	df		.
Filler0053_end:
	push hl			;0056	e5		.
	call BootContinue	;0057	cd c1 0f	. . .
	pop hl			;005a	e1		.
	ld bc,07ffdh		;005b	01 fd 7f	. . .
	xor a			;005e	af		.
	ld (05b88h),a		;005f	32 88 5b	2 . [
	out (c),a		;0062	ed 79		. y
	jr ProbeTail		;0064	18 06		. .
NmiVector:
	jp l000dh		;0066	c3 0d 00	. . .
SoftEntry:
	jp MonitorEntry		;0069	c3 b6 00	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Probe tail / re-validation: B=$1F, the IM0 acknowledge pattern
; ($C7 = RST 00h opcode, or $ED) is pushed and #1FFD cleared before
; the table walk is restarted at $0047 (inference from code shape).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ProbeTail:
	ld b,01fh		;006c	06 1f		. .
	rlc l			;006e	cb 05		. .
	ld hl,000c7h		;0070	21 c7 00	! . .
	jr nc,l0078h		;0073	30 03		0 .
	ld hl,000edh		;0075	21 ed 00	! . .
l0078h:
	push hl			;0078	e5		.
	jr l0047h		;0079	18 cc		. .
l007bh:
	call InventoryHardware	;007b	cd 64 05	. d .
	pop bc			;007e	c1		.
	call sub_35c9h		;007f	cd c9 35	. . 5
	xor a			;0082	af		.
	ld l,a			;0083	6f		o
	ld i,a			;0084	ed 47		. G
	im 0			;0086	ed 46		. F
	ei			;0088	fb		.
	halt			;0089	76		v
	im 1			;008a	ed 56		. V
	halt			;008c	76		v
	im 2			;008d	ed 5e		. ^
	halt			;008f	76		v
	jr Filler0053_end	;0090	18 c4		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; INT handler: A=0 then a DJNZ spin (timing), D bumped, EI, RET.
; Serves the $0038 vector and releases every HALT of the IM probe.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
IntHandler:
	ld a,000h		;0092	3e 00		> .
l0094h:
	djnz l0094h		;0094	10 fe		. .
	cp b			;0096	b8		.
	jr nz,l009ah		;0097	20 01		  .
	inc d			;0099	14		.
l009ah:
	ei			;009a	fb		.
	ret			;009b	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Warm entry (NMI / magic button).  Builds the interrupted context
; frame: return address bookkeeping pushed, subtype byte $08, BC
; and HL stacked, magic bytes $55/$AA stamped at RamMagic1/2.
; Then DI, #1FFD=$12 + #7FFD=$10 (monitor mapping), user SP parked
; at UserSp, monitor SP=$E2B5 installed; the boot counter at
; BootCounter decides between the full save below ($00D4) and a
; quick re-entry.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WarmEntry:
	push af			;009c	f5		.
	ld a,r			;009d	ed 5f		. _
	push af			;009f	f5		.
	ld a,008h		;00a0	3e 08		> .
	push af			;00a2	f5		.
	inc sp			;00a3	33		3
	push bc			;00a4	c5		.
	push hl			;00a5	e5		.
	ld hl,(RamMagic1)	;00a6	2a 01 c0	* . .
	ex (sp),hl		;00a9	e3		.
	ld a,055h		;00aa	3e 55		> U
	ld (RamMagic1),a	;00ac	32 01 c0	2 . .
	cpl			;00af	2f		/
	ld (RamMagic2),a	;00b0	32 02 c0	2 . .
	ld bc,01ffdh		;00b3	01 fd 1f	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Monitor main entry: DI; #1FFD=$12 pages the service monitor;
; #7FFD=$10 puts RAM bank 0 at $C000 (monitor workspace).  User
; SP -> UserSp, monitor stack $E2B5 installed.  BootCounter==0
; -> full SaveContext + register/IM analysis chain; otherwise the
; dispatch is entered directly.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MonitorEntry:
	di			;00b6	f3		.
	ld a,012h		;00b7	3e 12		> .
	out (c),a		;00b9	ed 79		. y
	ld b,07fh		;00bb	06 7f		. .
	ld a,010h		;00bd	3e 10		> .
sub_00bfh:
	out (c),a		;00bf	ed 79		. y
	ld (UserSp),sp		;00c1	ed 73 6d dd	. s m .
	ld (0ddf7h),sp		;00c5	ed 73 f7 dd	. s . .
	ld sp,MonitorStack	;00c9	31 b5 e2	1 . .
	ld bc,(BootCounter)	;00cc	ed 4b 64 c0	. K d .
	ld a,c			;00d0	79		y
	or b			;00d1	b0		.
	jr nz,l0101h		;00d2	20 2d		  -
	call SaveContext	;00d4	cd 26 03	. & .
	call AyReadRegister	;00d7	cd f0 02	. . .
	ld a,(UserIm)		;00da	3a 86 dd	: . .
	cp 080h			;00dd	fe 80		. .
	jp z,l081eh		;00df	ca 1e 08	. . .
	call CheckWatchpoints	;00e2	cd 7f 29	. . )
	ld a,(UserIm)		;00e5	3a 86 dd	: . .
	and 003h		;00e8	e6 03		. .
	jr nz,l013fh		;00ea	20 53		  S
	call sub_02bfh		;00ec	cd bf 02	. . .
	jr l0108h		;00ef	18 17		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Filler00F1: unreferenced junk immediately
; before the XOR key table at $00FC.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Filler00F1' (start 0x00f1 end 0x00fc)
Filler00F1_start:
	defb 00eh		;00f1	0e		.
	defb 0b6h		;00f2	b6		.
	defb 0e0h		;00f3	e0		.
	defb 024h		;00f4	24		$
	defb 066h		;00f5	66		f
l00f6h:
	defb 01fh		;00f6	1f		.
	defb 06fh		;00f7	6f		o
	defb 015h		;00f8	15		.
	defb 0deh		;00f9	de		.
	defb 0d6h		;00fa	d6		.
l00fbh:
	defb 001h		;00fb	01		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; XorKeys: private key bytes for DecodeXorTable ($2F99); several
; ROM tables are stored XOR-masked and are unmasked into RAM at boot.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Filler00F1_end:
XorDecodeKeys:

; BLOCK 'XorKeys' (start 0x00fc end 0x0100)
XorKeys_start:
	defb 018h		;00fc	18		.
l00fdh:
	defb 009h		;00fd	09		.
l00feh:
	defb 05eh		;00fe	5e		^
	defb 079h		;00ff	79		y
XorKeys_end:
	ex (sp),hl		;0100	e3		.
l0101h:
	push bc			;0101	c5		.
l0102h:
	dec bc			;0102	0b		.
	ld a,b			;0103	78		x
	or c			;0104	b1		.
l0105h:
	jr nz,l0102h		;0105	20 fb		  .
	ret			;0107	c9		.
l0108h:
	bit 0,(iy+012h)		;0108	fd cb 12 46	. . . F
	jp z,l11beh		;010c	ca be 11	. . .
	ld a,(0c063h)		;010f	3a 63 c0	: c .
	or a			;0112	b7		.
	jp nz,l11beh		;0113	c2 be 11	. . .
	jp l0a40h		;0116	c3 40 0a	. @ .
l0119h:
	call sub_02bch		;0119	cd bc 02	. . .
l011ch:
	ld sp,MonitorStack	;011c	31 b5 e2	1 . .
	call sub_04cch		;011f	cd cc 04	. . .
l0122h:
	call sub_2985h		;0122	cd 85 29	. . )
l0125h:
	res 3,(iy+012h)		;0125	fd cb 12 9e	. . . .
	ld a,(UserIm)		;0129	3a 86 dd	: . .
	and 00ch		;012c	e6 0c		. .
	call nz,0e321h		;012e	c4 21 e3	. ! .
	call AyWriteData	;0131	cd 1a 03	. . .
	call sub_041eh		;0134	cd 1e 04	. . .
	xor a			;0137	af		.
	ld sp,(UserSp)		;0138	ed 7b 6d dd	. { m .
	jp l000bh		;013c	c3 0b 00	. . .
l013fh:
	ld hl,(UserPc)		;013f	2a 6b dd	* k .
	dec hl			;0142	2b		+
	ld (UserPc),hl		;0143	22 6b dd	" k .
	ld a,(StepFlags)	;0146	3a f1 df	: . .
	bit 5,a			;0149	cb 6f		. o
	jr z,l0166h		;014b	28 19		( .
	ex de,hl		;014d	eb		.
	ld hl,(StepContext)	;014e	2a aa e0	* . .
	push hl			;0151	e5		.
	scf			;0152	37		7
	sbc hl,de		;0153	ed 52		. R
	ex de,hl		;0155	eb		.
	pop de			;0156	d1		.
	jr nc,l0166h		;0157	30 0d		0 .
	ex de,hl		;0159	eb		.
	push de			;015a	d5		.
	ld de,Rst08Vector	;015b	11 08 00	. . .
	add hl,de		;015e	19		.
	pop de			;015f	d1		.
	sbc hl,de		;0160	ed 52		. R
	ex de,hl		;0162	eb		.
	jp nc,l04a2h		;0163	d2 a2 04	. . .
l0166h:
	bit 4,a			;0166	cb 67		. g
	res 4,(iy+012h)		;0168	fd cb 12 a6	. . . .
	jr nz,l01cah		;016c	20 5c		  \
	call FindWatchpoint	;016e	cd e8 29	. . )
	ld e,000h		;0171	1e 00		. .
	jr c,l01cdh		;0173	38 58		8 X
l0175h:
	ld l,(ix+007h)		;0175	dd 6e 07	. n .
	ld h,(ix+008h)		;0178	dd 66 08	. f .
	ld a,l			;017b	7d		}
	or h			;017c	b4		.
	jr z,l018ah		;017d	28 0b		( .
	dec hl			;017f	2b		+
	ld (ix+007h),l		;0180	dd 75 07	. u .
	ld (ix+008h),h		;0183	dd 74 08	. t .
	ld a,h			;0186	7c		|
	or l			;0187	b5		.
	jr nz,l01b8h		;0188	20 2e		  .
l018ah:
	ld l,(ix+005h)		;018a	dd 6e 05	. n .
	ld (ix+007h),l		;018d	dd 75 07	. u .
	ld h,(ix+006h)		;0190	dd 66 06	. f .
	ld (ix+008h),h		;0193	dd 74 08	. t .
	push ix			;0196	dd e5		. .
	pop hl			;0198	e1		.
	bit 1,(hl)		;0199	cb 4e		. N
	jr nz,l019fh		;019b	20 02		  .
	res 7,(hl)		;019d	cb be		. .
l019fh:
	ld a,009h		;019f	3e 09		> .
	sub b			;01a1	90		.
	call LookupCommand	;01a2	cd 6d 29	. m )
	push af			;01a5	f5		.
	call nz,0eb06h		;01a6	c4 06 eb	. . .
	pop af			;01a9	f1		.
l01aah:
	ld l,a			;01aa	6f		o
	ld a,083h		;01ab	3e 83		> .
	ld h,000h		;01ad	26 00		& .
	ld (0dda7h),hl		;01af	22 a7 dd	" . .
	call sub_02c2h		;01b2	cd c2 02	. . .
	jp ExitToError		;01b5	c3 54 0a	. T .
l01b8h:
	bit 7,e			;01b8	cb 7b		. {
	jp nz,l13d7h		;01ba	c2 d7 13	. . .
	ld sp,0e25eh		;01bd	31 5e e2	1 ^ .
	res 7,(iy+016h)		;01c0	fd cb 16 be	. . . .
	call BuildStepStub	;01c4	cd b3 1a	. . .
l01c7h:
	jp l0122h		;01c7	c3 22 01	. " .
l01cah:
	xor a			;01ca	af		.
	jr l01aah		;01cb	18 dd		. .
l01cdh:
	push de			;01cd	d5		.
	inc hl			;01ce	23		#
	rst 28h			;01cf	ef		.
	cp 0ffh			;01d0	fe ff		. .
	jr z,l01d8h		;01d2	28 04		( .
	sla a			;01d4	cb 27		. '
	jr c,l01f9h		;01d6	38 21		8 !
l01d8h:
	ex de,hl		;01d8	eb		.
	call sub_354ah		;01d9	cd 4a 35	. J 5
	bit 4,(iy-002h)		;01dc	fd cb fe 66	. . . f
	jr z,l01cah		;01e0	28 e8		( .
	ld hl,(05c5dh)		;01e2	2a 5d 5c	* ] \
	ld (05c5fh),hl		;01e5	22 5f 5c	" _ \
	ld hl,Filler0053_start	;01e8	21 53 00	! S .
	ld (UserPc),hl		;01eb	22 6b dd	" k .
l01eeh:
	pop de			;01ee	d1		.
	bit 7,e			;01ef	cb 7b		. {
	jr z,l01c7h		;01f1	28 d4		( .
	ld hl,(UserPc)		;01f3	2a 6b dd	* k .
	jp l1b9ah		;01f6	c3 9a 1b	. . .
l01f9h:
	inc hl			;01f9	23		#
	ld (UserPc),hl		;01fa	22 6b dd	" k .
	cp 013h			;01fd	fe 13		. .
	jr nc,l01d8h		;01ff	30 d7		0 .
	ld hl,l01eeh		;0201	21 ee 01	! . .
	push hl			;0204	e5		.
	ld hl,KeyJumpTable	;0205	21 ba 07	! . .
	ld e,a			;0208	5f		_
	ld d,000h		;0209	16 00		. .
	add hl,de		;020b	19		.
	ld a,(hl)		;020c	7e		~
	inc hl			;020d	23		#
	ld h,(hl)		;020e	66		f
	ld l,a			;020f	6f		o
	jp (hl)			;0210	e9		.
	call ReadPort1FFD	;0211	cd 15 04	. . .
	call ReadPort7FFD	;0214	cd 11 04	. . .
l0217h:
	ld a,0afh		;0217	3e af		> .
	ld (0e379h),a		;0219	32 79 e3	2 y .
	call ShortDelay		;021c	cd 53 05	. S .
	jr z,l0226h		;021f	28 05		( .
	ld a,0c0h		;0221	3e c0		> .
	ld (0dff8h),a		;0223	32 f8 df	2 . .
l0226h:
	ld (0e38fh),hl		;0226	22 8f e3	" . .
	jp Filler0053_end	;0229	c3 56 00	. V .
	ld a,007h		;022c	3e 07		> .
	ld (0dd80h),a		;022e	32 80 dd	2 . .
	rst 18h			;0231	df		.
	xor l			;0232	ad		.
	jr c,l0256h		;0233	38 21		8 !
	dec b			;0235	05		.
	ret po			;0236	e0		.
l0237h:
	in a,(01fh)		;0237	db 1f		. .
	and h			;0239	a4		.
	jr z,l0237h		;023a	28 fb		( .
	ret m			;023c	f8		.
	ret pe			;023d	e8		.
	res 5,h			;023e	cb ac		. .
	inc (hl)		;0240	34		4
	inc hl			;0241	23		#
	ld (hl),l		;0242	75		u
	ret			;0243	c9		.
	call sub_0250h		;0244	cd 50 02	. P .
	set 6,(hl)		;0247	cb f6		. .
	ret			;0249	c9		.
	call sub_0250h		;024a	cd 50 02	. P .
	res 6,(hl)		;024d	cb b6		. .
	ret			;024f	c9		.
sub_0250h:
	xor a			;0250	af		.
	ld hl,0dff8h		;0251	21 f8 df	! . .
	bit 7,(hl)		;0254	cb 7e		. ~
l0256h:
	jr nz,l025ah		;0256	20 02		  .
	inc a			;0258	3c		<
	pop bc			;0259	c1		.
l025ah:
	ld (0dd7fh),a		;025a	32 7f dd	2 . .
	ret			;025d	c9		.
	ld hl,(RegisterFile)	;025e	2a 73 dd	* s .
	ld de,(0dd75h)		;0261	ed 5b 75 dd	. [ u .
	ld bc,(0dd77h)		;0265	ed 4b 77 dd	. K w .
	ld a,(0dd80h)		;0269	3a 80 dd	: . .
	call sub_02d6h		;026c	cd d6 02	. . .
	ld (0dd77h),bc		;026f	ed 43 77 dd	. C w .
	ld (0dd75h),de		;0273	ed 53 75 dd	. S u .
	ld (RegisterFile),hl	;0277	22 73 dd	" s .
	push af			;027a	f5		.
	pop hl			;027b	e1		.
	ld (0dd7fh),hl		;027c	22 7f dd	" . .
	ret			;027f	c9		.
	ld de,l029eh		;0280	11 9e 02	. . .
	ld hl,(0dd75h)		;0283	2a 75 dd	* u .
	add hl,de		;0286	19		.
	ld e,(hl)		;0287	5e		^
	inc hl			;0288	23		#
	ld d,(hl)		;0289	56		V
	ld hl,(0dd77h)		;028a	2a 77 dd	* w .
	add hl,de		;028d	19		.
	ld a,(0dd7fh)		;028e	3a 7f dd	: . .
	rrca			;0291	0f		.
	jr c,l0299h		;0292	38 05		8 .
	ld a,(hl)		;0294	7e		~
	ld (0dd80h),a		;0295	32 80 dd	2 . .
	ret			;0298	c9		.
l0299h:
	ld a,(0dd80h)		;0299	3a 80 dd	: . .
	ld (hl),a		;029c	77		w
	ret			;029d	c9		.
l029eh:
	rst 18h			;029e	df		.
	rst 18h			;029f	df		.
	nop			;02a0	00		.
	ret po			;02a1	e0		.
	ret nz			;02a2	c0		.
	rst 38h			;02a3	ff		.
	dec l			;02a4	2d		-
	ret pe			;02a5	e8		.
	call sub_02afh		;02a6	cd af 02	. . .
	ld a,(0dd80h)		;02a9	3a 80 dd	: . .
	jp PrintChar		;02ac	c3 be 26	. . &
sub_02afh:
	ld hl,(0dd6fh)		;02af	2a 6f dd	* o .
	rst 30h			;02b2	f7		.
	ret			;02b3	c9		.
	ld (0c063h),a		;02b4	32 63 c0	2 c .
	res 7,(iy+014h)		;02b7	fd cb 14 be	. . . .
	ret			;02bb	c9		.
sub_02bch:
	push af			;02bc	f5		.
	jr l02c6h		;02bd	18 07		. .
sub_02bfh:
	call ReadPort1FFD	;02bf	cd 15 04	. . .
sub_02c2h:
	push af			;02c2	f5		.
	call sub_049bh		;02c3	cd 9b 04	. . .
l02c6h:
	ld a,(l00fbh)		;02c6	3a fb 00	: . .
	ld hl,LastKeyCode	;02c9	21 d8 df	! . .
	cp (hl)			;02cc	be		.
	call nz,KeyClick	;02cd	c4 51 27	. Q '
	pop af			;02d0	f1		.
	pop hl			;02d1	e1		.
	ld sp,0e25eh		;02d2	31 5e e2	1 ^ .
	jp (hl)			;02d5	e9		.
sub_02d6h:
	push hl			;02d6	e5		.
	ld hl,l0825h+1		;02d7	21 26 08	! & .
	jr l02ech		;02da	18 10		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RamExtCall - RST 18h backend.  EX (SP),HL lifts the inline
; 16-bit address that follows the RST, parks it at RamExtTarget,
; restores the caller HL and dispatches through the RAM vector at
; (RamExtDispatcher).  RAM extensions may re-point either cell.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RamExtCall:
	ex (sp),hl		;02dc	e3		.
	push de			;02dd	d5		.
	ld e,(hl)		;02de	5e		^
	inc hl			;02df	23		#
	ld d,(hl)		;02e0	56		V
	inc hl			;02e1	23		#
	ld (RamExtTarget),de	;02e2	ed 53 73 e3	. S s .
	pop de			;02e6	d1		.
	ex (sp),hl		;02e7	e3		.
	push hl			;02e8	e5		.
	ld hl,(RamExtTarget)	;02e9	2a 73 e3	* s .
l02ech:
	ex (sp),hl		;02ec	e3		.
	jp RamExtDispatcher	;02ed	c3 58 e3	. X .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AY-3-8910 helpers: register select on #FFFD, data on #BFFD.
; AyReadRegister keeps the last value mirrored at AyShadowReg;
; AyWriteData ($031A) restores from that mirror.  Register 7
; doubles as input port (mouse buttons, hardware probes).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AyReadRegister:
	bit 0,(iy+014h)		;02f0	fd cb 14 46	. . . F
	ret nz			;02f4	c0		.
	ld b,007h		;02f5	06 07		. .
	call AyReadData		;02f7	cd 0e 03	. . .
	ld (AyShadowReg),a	;02fa	32 d7 df	2 . .
	ld a,0ffh		;02fd	3e ff		> .
l02ffh:
	push bc			;02ff	c5		.
	push af			;0300	f5		.
	ld a,b			;0301	78		x
	ld bc,0fffdh		;0302	01 fd ff	. . .
	out (c),a		;0305	ed 79		. y
	pop af			;0307	f1		.
	ld b,0bfh		;0308	06 bf		. .
	out (c),a		;030a	ed 79		. y
	pop bc			;030c	c1		.
	ret			;030d	c9		.
AyReadData:
	push bc			;030e	c5		.
	ld a,b			;030f	78		x
	ld bc,0fffdh		;0310	01 fd ff	. . .
	out (c),a		;0313	ed 79		. y
	nop			;0315	00		.
	in a,(c)		;0316	ed 78		. x
l0318h:
	pop bc			;0318	c1		.
	ret			;0319	c9		.
AyWriteData:
	bit 0,(iy+014h)		;031a	fd cb 14 46	. . . F
	ret nz			;031e	c0		.
	ld b,007h		;031f	06 07		. .
	ld a,(AyShadowReg)	;0321	3a d7 df	: . .
	jr l02ffh		;0324	18 d9		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SaveContext - snapshot the interrupted CPU.  AF, AF' (ex af
; twice), then BC/DE/HL of both sets, IY, IX are pushed onto the
; context stack below $DD83; IY <- IyWorkBase (monitor flags).
; The user PC is recovered from the stack, user SP derived (+11
; stacked words), registers copied to RegisterFile and the backup
; at RegisterBackup, I and IM captured (IM decoded from the
; stacked I byte), paging mirror updated, monitor mapping
; ($12/$10) re-asserted.  v2.95/prof additionally beep here.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SaveContext:
	ld bc,01ffdh		;0326	01 fd 1f	. . .
	ld (0dda7h),sp		;0329	ed 73 a7 dd	. s . .
	ld sp,UserIff		;032d	31 83 dd	1 . .
	ex af,af'		;0330	08		.
	push af			;0331	f5		.
	ex af,af'		;0332	08		.
	push af			;0333	f5		.
	exx			;0334	d9		.
	push bc			;0335	c5		.
	push de			;0336	d5		.
	push hl			;0337	e5		.
	exx			;0338	d9		.
	push bc			;0339	c5		.
	push de			;033a	d5		.
	push hl			;033b	e5		.
	push iy			;033c	fd e5		. .
	push ix			;033e	dd e5		. .
	ld iy,IyWorkBase	;0340	fd 21 df df	. ! . .
	ld hl,(UserSp)		;0344	2a 6d dd	* m .
	ld de,00200h		;0347	11 00 02	. . .
l034ah:
	ld bc,07ffdh		;034a	01 fd 7f	. . .
	out (c),e		;034d	ed 59		. Y
	ld b,01fh		;034f	06 1f		. .
	out (c),d		;0351	ed 51		. Q
	ld a,(RamMagic1)	;0353	3a 01 c0	: . .
	cp 055h			;0356	fe 55		. U
	jr z,l036ah		;0358	28 10		( .
l035ah:
	inc e			;035a	1c		.
	bit 3,e			;035b	cb 5b		. [
	jr z,l034ah		;035d	28 eb		( .
	ld e,000h		;035f	1e 00		. .
	bit 4,d			;0361	cb 62		. b
	jp nz,l04d4h		;0363	c2 d4 04	. . .
	ld d,012h		;0366	16 12		. .
	jr l034ah		;0368	18 e0		. .
l036ah:
	ld a,(RamMagic2)	;036a	3a 02 c0	: . .
	cp 0aah			;036d	fe aa		. .
	jr nz,l035ah		;036f	20 e9		  .
	ld sp,hl		;0371	f9		.
	pop hl			;0372	e1		.
	ld (RamMagic1),hl	;0373	22 01 c0	" . .
	ld b,01fh		;0376	06 1f		. .
	ld a,012h		;0378	3e 12		> .
	out (c),a		;037a	ed 79		. y
	ld b,07fh		;037c	06 7f		. .
	ld a,010h		;037e	3e 10		> .
	out (c),a		;0380	ed 79		. y
	res 1,d			;0382	cb 8a		. .
	ld (PagingState),de	;0384	ed 53 dd df	. S . .
	ld a,015h		;0388	3e 15		> .
	out (c),a		;038a	ed 79		. y
	ld hl,(RamMagic1)	;038c	2a 01 c0	* . .
	ld (RamMagic1),a	;038f	32 01 c0	2 . .
	ld a,010h		;0392	3e 10		> .
	out (c),a		;0394	ed 79		. y
	ld (RamMagic1),a	;0396	32 01 c0	2 . .
	ld a,015h		;0399	3e 15		> .
	out (c),a		;039b	ed 79		. y
	ld a,(RamMagic1)	;039d	3a 01 c0	: . .
	ld (RamMagic1),hl	;03a0	22 01 c0	" . .
	cp 015h			;03a3	fe 15		. .
	jr z,l03abh		;03a5	28 04		( .
	ld a,e			;03a7	7b		{
	or 030h			;03a8	f6 30		. 0
	ld e,a			;03aa	5f		_
l03abh:
	ld a,010h		;03ab	3e 10		> .
	out (c),a		;03ad	ed 79		. y
	ld a,(05b5ch)		;03af	3a 5c 5b	: \ [
	and 008h		;03b2	e6 08		. .
	bit 5,e			;03b4	cb 6b		. k
	jr nz,l03bah		;03b6	20 02		  .
	or e			;03b8	b3		.
	ld e,a			;03b9	5f		_
l03bah:
	ld a,(PagingState)	;03ba	3a dd df	: . .
	or e			;03bd	b3		.
	ld (PagingState),a	;03be	32 dd df	2 . .
	ld sp,(0dda7h)		;03c1	ed 7b a7 dd	. { . .
	ld hl,(UserSp)		;03c5	2a 6d dd	* m .
	push hl			;03c8	e5		.
	ld bc,l000bh		;03c9	01 0b 00	. . .
	add hl,bc		;03cc	09		.
	ld (UserSp),hl		;03cd	22 6d dd	" m .
	pop hl			;03d0	e1		.
	ld de,RegisterBackup	;03d1	11 99 dd	. . .
	call CopyAcrossBanks	;03d4	cd 46 06	. F .
	ld hl,(0dda2h)		;03d7	2a a2 dd	* . .
	ld (UserPc),hl		;03da	22 6b dd	" k .
	ld hl,(0dda0h)		;03dd	2a a0 dd	* . .
	ld (0dd7fh),hl		;03e0	22 7f dd	" . .
	ld hl,(0dd9eh)		;03e3	2a 9e dd	* . .
	ld a,h			;03e6	7c		|
	sub 004h		;03e7	d6 04		. .
	rlca			;03e9	07		.
	sla h			;03ea	cb 24		. $
	rra			;03ec	1f		.
	ld h,a			;03ed	67		g
	ld (UserIff),hl		;03ee	22 83 dd	" . .
	ld hl,(0dd9bh)		;03f1	2a 9b dd	* . .
	ld (0dd77h),hl		;03f4	22 77 dd	" w .
	ld a,i			;03f7	ed 57		. W
	ld (UserI),a		;03f9	32 85 dd	2 . .
	xor a			;03fc	af		.
	ld i,a			;03fd	ed 47		. G
l03ffh:
	ld a,(0dd9dh)		;03ff	3a 9d dd	: . .
	ld (UserIm),a		;0402	32 86 dd	2 . .
	and 006h		;0405	e6 06		. .
	ld a,(PagingState)	;0407	3a dd df	: . .
	jr z,l040eh		;040a	28 02		( .
	or 010h			;040c	f6 10		. .
l040eh:
	ld (PagingState),a	;040e	32 dd df	2 . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Read the paging ports back: Scorpion #7FFD / #1FFD return their
; latched value (B=$7F / B=$1F with C=$FD).  Used by NMI/magic
; detection and by the step engine.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ReadPort7FFD:
	ld b,07fh		;0411	06 7f		. .
	jr l0417h		;0413	18 02		. .
ReadPort1FFD:
	ld b,01fh		;0415	06 1f		. .
l0417h:
	ld c,0fdh		;0417	0e fd		. .
	in a,(c)		;0419	ed 78		. x
	in a,(c)		;041b	ed 78		. x
	ret			;041d	c9		.
sub_041eh:
	ld hl,(0dd75h)		;041e	2a 75 dd	* u .
	ld (RegisterBackup),hl	;0421	22 99 dd	" . .
	ld hl,(0dd77h)		;0424	2a 77 dd	* w .
	ld (0dd9bh),hl		;0427	22 9b dd	" . .
	ld a,(UserI)		;042a	3a 85 dd	: . .
	ld i,a			;042d	ed 47		. G
	ld (0dd9dh),a		;042f	32 9d dd	2 . .
	ld hl,(UserIff)		;0432	2a 83 dd	* . .
	ld a,h			;0435	7c		|
	sub 006h		;0436	d6 06		. .
	rlca			;0438	07		.
	sla h			;0439	cb 24		. $
	rra			;043b	1f		.
	ld h,a			;043c	67		g
	ld (0dd9eh),hl		;043d	22 9e dd	" . .
	ld hl,(0dd7fh)		;0440	2a 7f dd	* . .
	ld (0dda0h),hl		;0443	22 a0 dd	" . .
	ld hl,(UserPc)		;0446	2a 6b dd	* k .
	ld (0dda2h),hl		;0449	22 a2 dd	" . .
	ld hl,(UserSp)		;044c	2a 6d dd	* m .
	ld bc,0fff5h		;044f	01 f5 ff	. . .
	add hl,bc		;0452	09		.
	ld (UserSp),hl		;0453	22 6d dd	" m .
	ld de,RegisterBackup	;0456	11 99 dd	. . .
	ex de,hl		;0459	eb		.
	ld bc,l000bh		;045a	01 0b 00	. . .
	call sub_063ch		;045d	cd 3c 06	. < .
	ld (0dda7h),sp		;0460	ed 73 a7 dd	. s . .
	ld sp,0dd6fh		;0464	31 6f dd	1 o .
	pop ix			;0467	dd e1		. .
	pop iy			;0469	fd e1		. .
	pop hl			;046b	e1		.
	pop de			;046c	d1		.
	pop bc			;046d	c1		.
	exx			;046e	d9		.
	pop hl			;046f	e1		.
	pop de			;0470	d1		.
	pop bc			;0471	c1		.
	exx			;0472	d9		.
	pop af			;0473	f1		.
	ex af,af'		;0474	08		.
	pop af			;0475	f1		.
	ex af,af'		;0476	08		.
	ld sp,(0dda7h)		;0477	ed 7b a7 dd	. { . .
	call sub_048ch		;047b	cd 8c 04	. . .
	ld de,(PagingState)	;047e	ed 5b dd df	. [ . .
	ld a,e			;0482	7b		{
	ld b,07fh		;0483	06 7f		. .
	and 010h		;0485	e6 10		. .
	out (c),a		;0487	ed 79		. y
	ld b,01fh		;0489	06 1f		. .
	ret			;048b	c9		.
sub_048ch:
	ld bc,01ffdh		;048c	01 fd 1f	. . .
	ld a,(0dff8h)		;048f	3a f8 df	: . .
	rlca			;0492	07		.
	rlca			;0493	07		.
	jr nc,l0498h		;0494	30 02		0 .
	ld b,07fh		;0496	06 7f		. .
l0498h:
	in a,(c)		;0498	ed 78		. x
	ret			;049a	c9		.
sub_049bh:
	bit 3,(iy+012h)		;049b	fd cb 12 5e	. . . ^
	ret nz			;049f	c0		.
	jr SwapScreenBanks	;04a0	18 04		. .
l04a2h:
	ld sp,(MonitorStack)	;04a2	ed 7b b5 e2	. { . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SwapScreenBanks - exchange the user screen at $4000 with the
; private monitor screen at MonitorScreen ($C069, $1B00 bytes) via
; LDIR under #7FFD bit-4 toggling; direction chosen by carry and
; the shadow-screen flag (IY+12 bit 3).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SwapScreenBanks:
	ld hl,StepFlags		;04a6	21 f1 df	! . .
	bit 1,(hl)		;04a9	cb 4e		. N
	ret nz			;04ab	c0		.
	set 3,(hl)		;04ac	cb de		. .
	or a			;04ae	b7		.
l04afh:
	ld bc,07ffdh		;04af	01 fd 7f	. . .
	ld a,010h		;04b2	3e 10		> .
	out (c),a		;04b4	ed 79		. y
	ld hl,ZeroPad3F91_end	;04b6	21 00 40	! . @
	ld de,MonitorScreen	;04b9	11 69 c0	. i .
	jr nc,l04bfh		;04bc	30 01		0 .
	ex de,hl		;04be	eb		.
l04bfh:
	ld bc,01b00h		;04bf	01 00 1b	. . .
	ldir			;04c2	ed b0		. .
	ld bc,07ffdh		;04c4	01 fd 7f	. . .
	ld a,010h		;04c7	3e 10		> .
	out (c),a		;04c9	ed 79		. y
	ret			;04cb	c9		.
sub_04cch:
	bit 1,(iy+012h)		;04cc	fd cb 12 4e	. . . N
	ret nz			;04d0	c0		.
	scf			;04d1	37		7
	jr l04afh		;04d2	18 db		. .
l04d4h:
	ld a,004h		;04d4	3e 04		> .
	out (0feh),a		;04d6	d3 fe		. .
	halt			;04d8	76		v
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PeekPokeAnyBank - universal byte access; the engine behind RST 00h
; (carry=1, write) and RST 28h (carry=0, read).  H selects the
; window: bit 6 set -> banked RAM window (paging taken from the
; PagingState mirror, restored to $12/$10 afterwards); bit 7 set ->
; extended window (bank bits from mirror E, #1FFD <- (D&$10)|$02);
; neither -> plain (HL) access.  Shadow-screen flag routes $5B00+
; addresses through the monitor screen buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PeekPokeAnyBank:
	jr nz,l0518h		;04d9	20 3d		  =
	ex af,af'		;04db	08		.
	push bc			;04dc	c5		.
	push de			;04dd	d5		.
	bit 6,h			;04de	cb 74		. t
	jr nz,l04f0h		;04e0	20 0e		  .
	ld de,(PagingState)	;04e2	ed 5b dd df	. [ . .
	ld bc,07ffdh		;04e6	01 fd 7f	. . .
	jp 0e2dbh		;04e9	c3 db e2	. . .
l04ech:
	pop de			;04ec	d1		.
	pop bc			;04ed	c1		.
	ex af,af'		;04ee	08		.
	ret			;04ef	c9		.
l04f0h:
	ld de,05b00h		;04f0	11 00 5b	. . [
	or a			;04f3	b7		.
	sbc hl,de		;04f4	ed 52		. R
	add hl,de		;04f6	19		.
	jr nc,l0546h		;04f7	30 4d		0 M
	bit 3,(iy+012h)		;04f9	fd cb 12 5e	. . . ^
	jr z,l0546h		;04fd	28 47		( G
	ld de,08069h		;04ff	11 69 80	. i .
	add hl,de		;0502	19		.
	ld bc,07ffdh		;0503	01 fd 7f	. . .
	ld a,010h		;0506	3e 10		> .
	out (c),a		;0508	ed 79		. y
	ex af,af'		;050a	08		.
	jr c,l050eh		;050b	38 01		8 .
	ld a,(hl)		;050d	7e		~
l050eh:
	ld (hl),a		;050e	77		w
	ex af,af'		;050f	08		.
	and 010h		;0510	e6 10		. .
	out (c),a		;0512	ed 79		. y
	sbc hl,de		;0514	ed 52		. R
	jr l04ech		;0516	18 d4		. .
l0518h:
	bit 6,h			;0518	cb 74		. t
	jr z,l0549h		;051a	28 2d		( -
	ex af,af'		;051c	08		.
	push bc			;051d	c5		.
	push de			;051e	d5		.
	ld de,(PagingState)	;051f	ed 5b dd df	. [ . .
	ld bc,07ffdh		;0523	01 fd 7f	. . .
	ld a,e			;0526	7b		{
	and 017h		;0527	e6 17		. .
	out (c),a		;0529	ed 79		. y
	ld a,d			;052b	7a		z
	and 010h		;052c	e6 10		. .
	or 002h			;052e	f6 02		. .
	ld b,01fh		;0530	06 1f		. .
	out (c),a		;0532	ed 79		. y
	ex af,af'		;0534	08		.
	jr c,l0538h		;0535	38 01		8 .
	ld a,(hl)		;0537	7e		~
l0538h:
	ld (hl),a		;0538	77		w
	ex af,af'		;0539	08		.
	ld a,012h		;053a	3e 12		> .
	out (c),a		;053c	ed 79		. y
	ld b,07fh		;053e	06 7f		. .
	ld a,010h		;0540	3e 10		> .
	out (c),a		;0542	ed 79		. y
	jr l04ech		;0544	18 a6		. .
l0546h:
	pop de			;0546	d1		.
	pop bc			;0547	c1		.
	ex af,af'		;0548	08		.
l0549h:
	jr c,l054ch		;0549	38 01		8 .
	ld a,(hl)		;054b	7e		~
l054ch:
	ld (hl),a		;054c	77		w
	ret			;054d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FatalHalt: border flash and HALT forever - end of a failed
; hardware inventory (bad RAM under the monitor).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FatalHalt:
	ld a,004h		;054e	3e 04		> .
	out (0feh),a		;0550	d3 fe		. .
	halt			;0552	76		v
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ShortDelay: calibrated A/E delay loops (tape and AY timing).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ShortDelay:
	xor a			;0553	af		.
	ld h,a			;0554	67		g
	ld e,00eh		;0555	1e 0e		. .
	inc a			;0557	3c		<
l0558h:
	or a			;0558	b7		.
	jr z,l0562h		;0559	28 07		( .
	dec h			;055b	25		%
	jr nz,l0558h		;055c	20 fa		  .
	dec e			;055e	1d		.
	jr nz,l0558h		;055f	20 f7		  .
	inc e			;0561	1c		.
l0562h:
	di			;0562	f3		.
	ret			;0563	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; InventoryHardware: probe the machine - read #1FFD, walk #7FFD
; bank values, verify RAM with a fill/compare, clear the monitor
; screen buffer - and leave the signature the cold-start walk at
; $0049 expects in HwConfigTable.  Any RAM failure -> FatalHalt.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
InventoryHardware:
	ld iy,IyWorkBase	;0564	fd 21 df df	. ! . .
	ld bc,01ffdh		;0568	01 fd 1f	. . .
	in a,(c)		;056b	ed 78		. x
	ld a,012h		;056d	3e 12		> .
	out (c),a		;056f	ed 79		. y
	ld a,018h		;0571	3e 18		> .
	jr l0582h		;0573	18 0d		. .
l0575h:
	ld hl,0c000h		;0575	21 00 c0	! . .
	ld (hl),000h		;0578	36 00		6 .
	ld de,RamMagic1		;057a	11 01 c0	. . .
	ld bc,l3fffh		;057d	01 ff 3f	. . ?
	ldir			;0580	ed b0		. .
l0582h:
	dec a			;0582	3d		=
	ld bc,07ffdh		;0583	01 fd 7f	. . .
	out (c),a		;0586	ed 79		. y
	cp 010h			;0588	fe 10		. .
	jr nz,l0575h		;058a	20 e9		  .
	xor a			;058c	af		.
	ld hl,MonitorScreen	;058d	21 69 c0	! i .
	push hl			;0590	e5		.
	ld (hl),a		;0591	77		w
	ld de,0c06ah		;0592	11 6a c0	. j .
	ld bc,l3f96h		;0595	01 96 3f	. . ?
	ldir			;0598	ed b0		. .
	pop hl			;059a	e1		.
l059bh:
	cp (hl)			;059b	be		.
	inc hl			;059c	23		#
	jr nz,FatalHalt		;059d	20 af		  .
	cp h			;059f	bc		.
	jr nz,l059bh		;05a0	20 f9		  .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DecodeTables: copy + XOR-unmask the coded ROM tables into RAM
; (DecodedTables at $E82D) using the keys at XorDecodeKeys;
; IX is then pointed at the boot configuration table copy.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DecodeTables:
	ld hl,ErrorShortStrings_end	;05a2	21 45 3e	! E >
	ld de,DecodedTables	;05a5	11 2d e8	. - .
	call DecodeXorTable	;05a8	cd 99 2f	. . /
	ld ix,(BootConfigTable)	;05ab	dd 2a e0 f5	. * . .
	ld hl,0dff7h		;05af	21 f7 df	! . .
	ld (hl),001h		;05b2	36 01		6 .
	ld hl,HwConfigTable	;05b4	21 28 e9	! ( .
l05b7h:
	ld (hl),b		;05b7	70		p
	inc hl			;05b8	23		#
	djnz l05b7h		;05b9	10 fc		. .
	ld hl,MenuBuilder_end	;05bb	21 90 36	! . 6
	ld de,0e2dbh		;05be	11 db e2	. . .
	call DecodeXorTable	;05c1	cd 99 2f	. . /
	jp (ix)			;05c4	dd e9		. .
	or (hl)			;05c6	b6		.
	call p,04921h		;05c7	f4 21 49	. ! I
	ccf			;05ca	3f		?
	ld de,0ffc0h		;05cb	11 c0 ff	. . .
	ld bc,Rst20Vector	;05ce	01 20 00	.   .
	ldir			;05d1	ed b0		. .
	rst 18h			;05d3	df		.
	ld bc,0cd32h		;05d4	01 32 cd	. 2 .
	ld a,l			;05d7	7d		}
	ld (hl),0fdh		;05d8	36 fd		6 .
	ld (hl),008h		;05da	36 08		6 .
	jr nc,l05ffh		;05dc	30 21		0 !
	ret			;05de	c9		.
	ret po			;05df	e0		.
	res 7,(hl)		;05e0	cb be		. .
l05e2h:
	inc hl			;05e2	23		#
	ld (0e2d9h),hl		;05e3	22 d9 e2	" . .
	set 6,(iy+009h)		;05e6	fd cb 09 f6	. . . .
	ld c,000h		;05ea	0e 00		. .
	call sub_0633h		;05ec	cd 33 06	. 3 .
	rst 20h			;05ef	e7		.
	adc a,02eh		;05f0	ce 2e		. .
	call p,07e61h		;05f2	f4 61 7e	. a ~
	defb 0edh ;next byte illegal after ed	;05f5	ed		.
	ld l,h			;05f6	6c		l
	inc hl			;05f7	23		#
	ld h,(hl)		;05f8	66		f
	ld l,a			;05f9	6f		o
	call PrintHexWord	;05fa	cd 9e 19	. . .
	ld c,001h		;05fd	0e 01		. .
l05ffh:
	call sub_0633h		;05ff	cd 33 06	. 3 .
	ld e,0fch		;0602	1e fc		. .
	ld d,c			;0604	51		Q
	dec d			;0605	15		.
	ld a,(de)		;0606	1a		.
	inc de			;0607	13		.
	push de			;0608	d5		.
	ld b,002h		;0609	06 02		. .
	call PrintDecimal	;060b	cd 18 19	. . .
	rst 20h			;060e	e7		.
	xor l			;060f	ad		.
	pop de			;0610	d1		.
	ld a,(de)		;0611	1a		.
	ld b,002h		;0612	06 02		. .
	call PrintDecimal	;0614	cd 18 19	. . .
	ld c,002h		;0617	0e 02		. .
	call sub_0633h		;0619	cd 33 06	. 3 .
	rst 20h			;061c	e7		.
	sub 0e7h		;061d	d6 e7		. .
	ld (0e7aeh),a		;061f	32 ae e7	2 . .
	add hl,sp		;0622	39		9
	or d			;0623	b2		.
	ld hl,(0e2d9h)		;0624	2a d9 e2	* . .
	dec hl			;0627	2b		+
	set 7,(hl)		;0628	cb fe		. .
	res 6,(iy+009h)		;062a	fd cb 09 b6	. . . .
	ld hl,ChecksumLoop	;062e	21 59 28	! Y (
	ex (sp),hl		;0631	e3		.
	jp (hl)			;0632	e9		.
sub_0633h:
	rst 20h			;0633	e7		.
	sbc a,e			;0634	9b		.
	ld a,c			;0635	79		y
	rst 10h			;0636	d7		.
	rst 20h			;0637	e7		.
	and h			;0638	a4		.
	ret			;0639	c9		.
l063ah:
	ex de,hl		;063a	eb		.
	ret			;063b	c9		.
sub_063ch:
	scf			;063c	37		7
	ex de,hl		;063d	eb		.
	exx			;063e	d9		.
	ld hl,l063ah		;063f	21 3a 06	! : .
	push hl			;0642	e5		.
	exx			;0643	d9		.
	jr l0647h		;0644	18 01		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CopyAcrossBanks - LDIR with either side in a foreign bank:
; bytes travel one at a time through the bank window (PeekPoke-
; AnyBank style access); $4000-area sides go via the monitor
; screen buffer when the shadow-screen flag is set.  Used by all
; windows that display foreign memory (dumps, disassembly, sums).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CopyAcrossBanks:
	or a			;0646	b7		.
l0647h:
	ex af,af'		;0647	08		.
	bit 7,h			;0648	cb 7c		. |
	jr nz,l0690h		;064a	20 44		  D
	bit 6,h			;064c	cb 74		. t
	jr nz,l0668h		;064e	20 18		  .
	push hl			;0650	e5		.
	add hl,bc		;0651	09		.
	dec hl			;0652	2b		+
	bit 6,h			;0653	cb 74		. t
	pop hl			;0655	e1		.
	jp z,l06e4h		;0656	ca e4 06	. . .
	push bc			;0659	c5		.
	ld bc,ZeroPad3F91_end	;065a	01 00 40	. . @
	ex de,hl		;065d	eb		.
	ex (sp),hl		;065e	e3		.
	call sub_06d3h		;065f	cd d3 06	. . .
	ex (sp),hl		;0662	e3		.
	ex de,hl		;0663	eb		.
	call l06e4h		;0664	cd e4 06	. . .
	pop bc			;0667	c1		.
l0668h:
	push hl			;0668	e5		.
	call sub_06cah		;0669	cd ca 06	. . .
	pop hl			;066c	e1		.
	jr nc,l0694h		;066d	30 25		0 %
	bit 3,(iy+012h)		;066f	fd cb 12 5e	. . . ^
	jr z,l0694h		;0673	28 1f		( .
	push hl			;0675	e5		.
	add hl,bc		;0676	09		.
	dec hl			;0677	2b		+
	call sub_06cah		;0678	cd ca 06	. . .
	pop hl			;067b	e1		.
	jp c,l06fah		;067c	da fa 06	. . .
	push bc			;067f	c5		.
	ld bc,05b00h		;0680	01 00 5b	. . [
	ex de,hl		;0683	eb		.
	ex (sp),hl		;0684	e3		.
	call sub_06d3h		;0685	cd d3 06	. . .
	ex (sp),hl		;0688	e3		.
	ex de,hl		;0689	eb		.
	call l06fah		;068a	cd fa 06	. . .
	pop bc			;068d	c1		.
	jr l0694h		;068e	18 04		. .
l0690h:
	bit 6,h			;0690	cb 74		. t
	jr nz,l06afh		;0692	20 1b		  .
l0694h:
	push hl			;0694	e5		.
	add hl,bc		;0695	09		.
	dec hl			;0696	2b		+
	ld a,h			;0697	7c		|
	and 0c0h		;0698	e6 c0		. .
	cp 0c0h			;069a	fe c0		. .
	pop hl			;069c	e1		.
	jp nz,l06efh		;069d	c2 ef 06	. . .
	push bc			;06a0	c5		.
	ld bc,0c000h		;06a1	01 00 c0	. . .
	ex de,hl		;06a4	eb		.
	ex (sp),hl		;06a5	e3		.
	call sub_06d3h		;06a6	cd d3 06	. . .
	ex (sp),hl		;06a9	e3		.
	ex de,hl		;06aa	eb		.
	call l06efh		;06ab	cd ef 06	. . .
	pop bc			;06ae	c1		.
l06afh:
	push hl			;06af	e5		.
	add hl,bc		;06b0	09		.
	dec hl			;06b1	2b		+
	bit 6,h			;06b2	cb 74		. t
	pop hl			;06b4	e1		.
	jp nz,l0714h		;06b5	c2 14 07	. . .
	push bc			;06b8	c5		.
	ld bc,WriteAnyBankByte	;06b9	01 00 00	. . .
	ex de,hl		;06bc	eb		.
	ex (sp),hl		;06bd	e3		.
	call sub_06d3h		;06be	cd d3 06	. . .
	ex (sp),hl		;06c1	e3		.
	ex de,hl		;06c2	eb		.
	call l0714h		;06c3	cd 14 07	. . .
	pop bc			;06c6	c1		.
	jp l06e4h		;06c7	c3 e4 06	. . .
sub_06cah:
	push de			;06ca	d5		.
	ld de,05b00h		;06cb	11 00 5b	. . [
	or a			;06ce	b7		.
	sbc hl,de		;06cf	ed 52		. R
	pop de			;06d1	d1		.
	ret			;06d2	c9		.
sub_06d3h:
	ex de,hl		;06d3	eb		.
	or a			;06d4	b7		.
	push hl			;06d5	e5		.
	ld l,c			;06d6	69		i
	ld h,b			;06d7	60		`
	pop bc			;06d8	c1		.
	sbc hl,bc		;06d9	ed 42		. B
	push bc			;06db	c5		.
	ld c,l			;06dc	4d		M
	ld b,h			;06dd	44		D
	pop hl			;06de	e1		.
	ex de,hl		;06df	eb		.
	or a			;06e0	b7		.
	sbc hl,bc		;06e1	ed 42		. B
	ret			;06e3	c9		.
l06e4h:
	exx			;06e4	d9		.
	ld de,(PagingState)	;06e5	ed 5b dd df	. [ . .
	ld bc,07ffdh		;06e9	01 fd 7f	. . .
	jp 0e2fch		;06ec	c3 fc e2	. . .
l06efh:
	ex af,af'		;06ef	08		.
	jr nc,l06f3h		;06f0	30 01		0 .
	ex de,hl		;06f2	eb		.
l06f3h:
	ldir			;06f3	ed b0		. .
	jr nc,l06f8h		;06f5	30 01		0 .
	ex de,hl		;06f7	eb		.
l06f8h:
	ex af,af'		;06f8	08		.
	ret			;06f9	c9		.
l06fah:
	push bc			;06fa	c5		.
	ld bc,08069h		;06fb	01 69 80	. i .
	add hl,bc		;06fe	09		.
	pop bc			;06ff	c1		.
	push hl			;0700	e5		.
	ld hl,l070dh		;0701	21 0d 07	! . .
	ex (sp),hl		;0704	e3		.
	push iy			;0705	fd e5		. .
	ld iy,01010h		;0707	fd 21 10 10	. ! . .
	jr l071ah		;070b	18 0d		. .
l070dh:
	push bc			;070d	c5		.
	ld bc,07f97h		;070e	01 97 7f	. . .
	add hl,bc		;0711	09		.
	pop bc			;0712	c1		.
	ret			;0713	c9		.
l0714h:
	push iy			;0714	fd e5		. .
	ld iy,(PagingState)	;0716	fd 2a dd df	. * . .
l071ah:
	push hl			;071a	e5		.
	ld hl,(08000h)		;071b	2a 00 80	* . .
	ex (sp),hl		;071e	e3		.
	push ix			;071f	dd e5		. .
	defb 0ddh,060h ;ld ixh,b	;0721	dd 60		. `
	defb 0ddh,069h ;ld ixl,c	;0723	dd 69		. i
	exx			;0725	d9		.
	ld bc,07ffdh		;0726	01 fd 7f	. . .
	ld a,010h		;0729	3e 10		> .
	out (c),a		;072b	ed 79		. y
	defb 0ddh,044h ;ld b,ixh	;072d	dd 44		. D
	defb 0ddh,04dh ;ld c,ixl	;072f	dd 4d		. M
	ld hl,08002h		;0731	21 02 80	! . .
	ld de,0db69h		;0734	11 69 db	. i .
	ldir			;0737	ed b0		. .
	ld hl,l1210h		;0739	21 10 12	! . .
	defb 0fdh,07dh ;ld a,iyl	;073c	fd 7d		. }
	and 017h		;073e	e6 17		. .
	ld e,a			;0740	5f		_
	defb 0fdh,07ch ;ld a,iyh	;0741	fd 7c		. |
	and 010h		;0743	e6 10		. .
	or 002h			;0745	f6 02		. .
	ld d,a			;0747	57		W
	ex af,af'		;0748	08		.
	jr nc,l074ch		;0749	30 01		0 .
	ex de,hl		;074b	eb		.
l074ch:
	ld bc,07ffdh		;074c	01 fd 7f	. . .
	out (c),e		;074f	ed 59		. Y
	ld b,01fh		;0751	06 1f		. .
	out (c),d		;0753	ed 51		. Q
	exx			;0755	d9		.
	jr nc,l0759h		;0756	30 01		0 .
	ex de,hl		;0758	eb		.
l0759h:
	ld (08000h),de		;0759	ed 53 00 80	. S . .
	ld de,08002h		;075d	11 02 80	. . .
	ldir			;0760	ed b0		. .
	ld de,(08000h)		;0762	ed 5b 00 80	. [ . .
	defb 0ddh,044h ;ld b,ixh	;0766	dd 44		. D
	defb 0ddh,04dh ;ld c,ixl	;0768	dd 4d		. M
	exx			;076a	d9		.
	out (c),h		;076b	ed 61		. a
	ld b,07fh		;076d	06 7f		. .
	out (c),l		;076f	ed 69		. i
	exx			;0771	d9		.
	ld (08000h),hl		;0772	22 00 80	" . .
	ld hl,08002h		;0775	21 02 80	! . .
	ldir			;0778	ed b0		. .
	ld hl,(08000h)		;077a	2a 00 80	* . .
	jr nc,l0780h		;077d	30 01		0 .
	ex de,hl		;077f	eb		.
l0780h:
	ex af,af'		;0780	08		.
	exx			;0781	d9		.
	ld a,010h		;0782	3e 10		> .
	out (c),a		;0784	ed 79		. y
	ld b,01fh		;0786	06 1f		. .
	ld a,012h		;0788	3e 12		> .
	out (c),a		;078a	ed 79		. y
	ld hl,0db69h		;078c	21 69 db	! i .
	ld de,08002h		;078f	11 02 80	. . .
	defb 0ddh,044h ;ld b,ixh	;0792	dd 44		. D
	defb 0ddh,04dh ;ld c,ixl	;0794	dd 4d		. M
	ldir			;0796	ed b0		. .
	ld bc,07ffdh		;0798	01 fd 7f	. . .
	ld a,010h		;079b	3e 10		> .
	out (c),a		;079d	ed 79		. y
	exx			;079f	d9		.
	pop ix			;07a0	dd e1		. .
	ex (sp),hl		;07a2	e3		.
	ld (08000h),hl		;07a3	22 00 80	" . .
	pop hl			;07a6	e1		.
	pop iy			;07a7	fd e1		. .
	ret			;07a9	c9		.
sub_07aah:
	rst 18h			;07aa	df		.
	jr z,KeyJumpTable_end	;07ab	28 36		( 6
	ld hl,l07b4h		;07ad	21 b4 07	! . .
	push hl			;07b0	e5		.
	jp 0e322h		;07b1	c3 22 e3	. " .
l07b4h:
	push af			;07b4	f5		.
	rst 18h			;07b5	df		.
	jr z,$+56		;07b6	28 36		( 6
	pop af			;07b8	f1		.
	ret			;07b9	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyJumpTable - 16-bit handler addresses indexed by raw key code;
; the NMI-side scan (cf. $0205) computes HL=table+2*key and jumps
; (HL).  Unhandled keys share a common ignore entry.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
KeyJumpTable:

; BLOCK 'KeyJumpTable' (start 0x07ba end 0x07e3)
KeyJumpTable_start:
	defb 00dh		;07ba	0d		.
	defb 008h		;07bb	08		.
	defb 05eh		;07bc	5e		^
	defb 002h		;07bd	02		.
	defb 080h		;07be	80		.
	defb 002h		;07bf	02		.
	defb 058h		;07c0	58		X
	defb 026h		;07c1	26		&
	defb 0a6h		;07c2	a6		.
	defb 002h		;07c3	02		.
	defb 0b4h		;07c4	b4		.
	defb 002h		;07c5	02		.
	defb 0ceh		;07c6	ce		.
	defb 007h		;07c7	07		.
	defb 044h		;07c8	44		D
	defb 002h		;07c9	02		.
	defb 04ah		;07ca	4a		J
	defb 002h		;07cb	02		.
	defb 02ch		;07cc	2c		,
	defb 002h		;07cd	02		.
	defb 0afh		;07ce	af		.
	defb 037h		;07cf	37		7
	defb 018h		;07d0	18		.
	defb 001h		;07d1	01		.
l07d2h:
	defb 0afh		;07d2	af		.
	defb 021h		;07d3	21		!
	defb 0f3h		;07d4	f3		.
	defb 007h		;07d5	07		.
	defb 022h		;07d6	22		"
	defb 012h		;07d7	12		.
	defb 0deh		;07d8	de		.
	defb 02ah		;07d9	2a		*
	defb 0d4h		;07da	d4		.
	defb 0e2h		;07db	e2		.
	defb 0e5h		;07dc	e5		.
	defb 021h		;07dd	21		!
	defb 019h		;07de	19		.
	defb 0e0h		;07df	e0		.
	defb 0f7h		;07e0	f7		.
	defb 0f5h		;07e1	f5		.
	defb 0edh		;07e2	ed		.
KeyJumpTable_end:
	ld (hl),e		;07e3	73		s
	and a			;07e4	a7		.
	defb 0ddh,0fdh,0cbh ;illegal sequence	;07e5	dd fd cb	. . .
	add hl,bc		;07e8	09		.
	and 0d4h		;07e9	e6 d4		. .
	ld d,a			;07eb	57		W
	dec (hl)		;07ec	35		5
	call sub_094eh		;07ed	cd 4e 09	. N .
	pop af			;07f0	f1		.
	jr l07fah		;07f1	18 07		. .
	ld sp,(0dda7h)		;07f3	ed 7b a7 dd	. { . .
	pop af			;07f7	f1		.
	ld a,001h		;07f8	3e 01		> .
l07fah:
	ld hl,0dfe8h		;07fa	21 e8 df	! . .
	res 4,(hl)		;07fd	cb a6		. .
	res 5,(hl)		;07ff	cb ae		. .
	pop ix			;0801	dd e1		. .
	ld (WorkBufferPtr),ix	;0803	dd 22 d4 e2	. " . .
	ret nc			;0807	d0		.
	or a			;0808	b7		.
	jr z,l0834h		;0809	28 29		( )
	jr l081ah		;080b	18 0d		. .
	ld hl,l0816h		;080d	21 16 08	! . .
	ld (0dda7h),sp		;0810	ed 73 a7 dd	. s . .
	jr l0825h		;0814	18 0f		. .
l0816h:
	ld sp,(0dda7h)		;0816	ed 7b a7 dd	. { . .
l081ah:
	ld a,001h		;081a	3e 01		> .
	jr l0836h		;081c	18 18		. .
l081eh:
	ld hl,l0125h		;081e	21 25 01	! % .
	push hl			;0821	e5		.
	ld hl,l083eh		;0822	21 3e 08	! > .
l0825h:
	ld (0de12h),hl		;0825	22 12 de	" . .
	ld hl,0e019h		;0828	21 19 e0	! . .
	rst 30h			;082b	f7		.
	set 4,(iy+009h)		;082c	fd cb 09 e6	. . . .
	ld a,(0dd80h)		;0830	3a 80 dd	: . .
	rst 10h			;0833	d7		.
l0834h:
	ld a,040h		;0834	3e 40		> @
l0836h:
	ld (0dd7fh),a		;0836	32 7f dd	2 . .
sub_0839h:
	res 4,(iy+009h)		;0839	fd cb 09 a6	. . . .
	ret			;083d	c9		.
l083eh:
	ld sp,0e25eh		;083e	31 5e e2	1 ^ .
	call sub_0839h		;0841	cd 39 08	. 9 .
	ld hl,l05e2h		;0844	21 e2 05	! . .
	ld (UserPc),hl		;0847	22 6b dd	" k .
	jp l0125h		;084a	c3 25 01	. % .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Tape output feeder: serialises bits to the tape port (OUT $FE
; border bit) with loop-timed pulses; drives SaveToTape.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FeedTapeOutput:
	bit 5,(iy+009h)		;084d	fd cb 09 6e	. . . n
	jr nz,MouseDriver	;0851	20 53		  S
	cp 00dh			;0853	fe 0d		. .
	jr nz,l0884h		;0855	20 2d		  -
	ld (ix+001h),000h	;0857	dd 36 01 00	. 6 . .
	call sub_0902h		;085b	cd 02 09	. . .
	bit 2,(ix+007h)		;085e	dd cb 07 56	. . . V
	jr z,l086dh		;0862	28 09		( .
	ld a,(0e007h)		;0864	3a 07 e0	: . .
	or a			;0867	b7		.
	jr z,l086dh		;0868	28 03		( .
	call MouseDriver	;086a	cd a6 08	. . .
l086dh:
	ld a,(ix+004h)		;086d	dd 7e 04	. ~ .
	or a			;0870	b7		.
	ret z			;0871	c8		.
	inc (ix+000h)		;0872	dd 34 00	. 4 .
	cp (ix+000h)		;0875	dd be 00	. . .
	ret nc			;0878	d0		.
	ld (ix+000h),000h	;0879	dd 36 00 00	. 6 . .
	ld a,(0e008h)		;087d	3a 08 e0	: . .
	or a			;0880	b7		.
	ret z			;0881	c8		.
	jr MouseDriver		;0882	18 22		. "
l0884h:
	cp 020h			;0884	fe 20		.  
	jr c,MouseDriver	;0886	38 1e		8 .
	cp 07fh			;0888	fe 7f		. .
	jr nz,l088eh		;088a	20 02		  .
	ld a,02eh		;088c	3e 2e		> .
l088eh:
	ld e,a			;088e	5f		_
	ld a,(ix+005h)		;088f	dd 7e 05	. ~ .
	or a			;0892	b7		.
	jr z,l08a5h		;0893	28 10		( .
	inc (ix+001h)		;0895	dd 34 01	. 4 .
	cp (ix+001h)		;0898	dd be 01	. . .
	jr nc,l08a5h		;089b	30 08		0 .
	push de			;089d	d5		.
	rst 20h			;089e	e7		.
	adc a,l			;089f	8d		.
	pop de			;08a0	d1		.
	xor a			;08a1	af		.
	ld (ix+001h),a		;08a2	dd 77 01	. w .
l08a5h:
	ld a,e			;08a5	7b		{
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MouseDriver: AY-port mouse - buttons sampled via AY register 7
; inputs, motion from the PSG counters; state kept in RAM.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MouseDriver:
	ld d,(iy+015h)		;08a6	fd 56 15	. V .
	bit 7,d			;08a9	cb 7a		. z
	jr z,l08aeh		;08ab	28 01		( .
	cpl			;08ad	2f		/
l08aeh:
	bit 0,d			;08ae	cb 42		. B
	jr z,l090dh		;08b0	28 5b		( [
	push af			;08b2	f5		.
	xor 0ffh		;08b3	ee ff		. .
	rlca			;08b5	07		.
	rlca			;08b6	07		.
	rlca			;08b7	07		.
	push af			;08b8	f5		.
	call ReadPort1FFD	;08b9	cd 15 04	. . .
	pop bc			;08bc	c1		.
	ld c,b			;08bd	48		H
l08beh:
	call sub_0930h		;08be	cd 30 09	. 0 .
	jr nc,l092ch		;08c1	30 69		0 i
	ld a,0ffh		;08c3	3e ff		> .
	in a,(0feh)		;08c5	db fe		. .
	bit 5,a			;08c7	cb 6f		. o
	jr z,l08beh		;08c9	28 f3		( .
	ld b,008h		;08cb	06 08		. .
	bit 2,d			;08cd	cb 52		. R
	jr z,l08d2h		;08cf	28 01		( .
	dec b			;08d1	05		.
l08d2h:
	push bc			;08d2	c5		.
	ld bc,01ffdh		;08d3	01 fd 1f	. . .
	ld a,01ah		;08d6	3e 1a		> .
	out (c),a		;08d8	ed 79		. y
	pop bc			;08da	c1		.
l08dbh:
	call sub_093ch		;08db	cd 3c 09	. < .
	push bc			;08de	c5		.
	ld a,c			;08df	79		y
	ld bc,01ffdh		;08e0	01 fd 1f	. . .
	and 008h		;08e3	e6 08		. .
	or 012h			;08e5	f6 12		. .
	out (c),a		;08e7	ed 79		. y
	pop bc			;08e9	c1		.
	rrc c			;08ea	cb 09		. .
	djnz l08dbh		;08ec	10 ed		. .
	call sub_093ch		;08ee	cd 3c 09	. < .
	ld bc,01ffdh		;08f1	01 fd 1f	. . .
	ld a,012h		;08f4	3e 12		> .
	out (c),a		;08f6	ed 79		. y
	call sub_093ch		;08f8	cd 3c 09	. < .
	bit 3,d			;08fb	cb 5a		. Z
	call z,sub_093ch	;08fd	cc 3c 09	. < .
	pop af			;0900	f1		.
	ret			;0901	c9		.
sub_0902h:
	ld a,(0e006h)		;0902	3a 06 e0	: . .
	or a			;0905	b7		.
	ret z			;0906	c8		.
	push bc			;0907	c5		.
	call MouseDriver	;0908	cd a6 08	. . .
	pop bc			;090b	c1		.
	ret			;090c	c9		.
l090dh:
	ld c,a			;090d	4f		O
l090eh:
	call sub_0930h		;090e	cd 30 09	. 0 .
	jr nc,l092ch		;0911	30 19		0 .
	ld a,0ffh		;0913	3e ff		> .
	in a,(0feh)		;0915	db fe		. .
	rlca			;0917	07		.
	jr c,l090eh		;0918	38 f4		8 .
	ld a,c			;091a	79		y
	ld bc,0ffddh		;091b	01 dd ff	. . .
	out (c),a		;091e	ed 79		. y
	ld bc,01ffdh		;0920	01 fd 1f	. . .
	ld a,032h		;0923	3e 32		> 2
	out (c),a		;0925	ed 79		. y
	ld a,012h		;0927	3e 12		> .
	out (c),a		;0929	ed 79		. y
	ret			;092b	c9		.
l092ch:
	ld hl,(0de12h)		;092c	2a 12 de	* . .
	jp (hl)			;092f	e9		.
sub_0930h:
	ld a,07fh		;0930	3e 7f		> .
	in a,(0feh)		;0932	db fe		. .
	rra			;0934	1f		.
	ret c			;0935	d8		.
	ld a,0feh		;0936	3e fe		> .
	in a,(0feh)		;0938	db fe		. .
	rra			;093a	1f		.
	ret			;093b	c9		.
sub_093ch:
	push af			;093c	f5		.
	ld hl,(0e009h)		;093d	2a 09 e0	* . .
	bit 1,d			;0940	cb 4a		. J
	jr z,l0947h		;0942	28 03		( .
	ld hl,(0e00bh)		;0944	2a 0b e0	* . .
l0947h:
	dec hl			;0947	2b		+
	ld a,h			;0948	7c		|
	or l			;0949	b5		.
	jr nz,l0947h		;094a	20 fb		  .
	pop af			;094c	f1		.
	ret			;094d	c9		.
sub_094eh:
	ld bc,WriteAnyBankByte	;094e	01 00 00	. . .
l0951h:
	set 5,(iy+009h)		;0951	fd cb 09 ee	. . . .
	bit 5,(iy+015h)		;0955	fd cb 15 6e	. . . n
	ld a,001h		;0959	3e 01		> .
	jr z,l095fh		;095b	28 02		( .
	ld a,003h		;095d	3e 03		> .
l095fh:
	ld (0e013h),a		;095f	32 13 e0	2 . .
	push bc			;0962	c5		.
	ld hl,0e00dh		;0963	21 0d e0	! . .
	call sub_09cfh		;0966	cd cf 09	. . .
	pop bc			;0969	c1		.
l096ah:
	push bc			;096a	c5		.
	bit 5,(iy+015h)		;096b	fd cb 15 6e	. . . n
	ld d,008h		;096f	16 08		. .
	jr z,l0975h		;0971	28 02		( .
	ld d,004h		;0973	16 04		. .
l0975h:
	push bc			;0975	c5		.
	ld a,b			;0976	78		x
	and a			;0977	a7		.
	rra			;0978	1f		.
	scf			;0979	37		7
	rra			;097a	1f		.
	and a			;097b	a7		.
	rra			;097c	1f		.
	xor b			;097d	a8		.
	and 0f8h		;097e	e6 f8		. .
	xor b			;0980	a8		.
	ld h,a			;0981	67		g
	ld a,c			;0982	79		y
	rlca			;0983	07		.
	rlca			;0984	07		.
	rlca			;0985	07		.
	xor b			;0986	a8		.
	and 0c7h		;0987	e6 c7		. .
	xor b			;0989	a8		.
	rlca			;098a	07		.
	rlca			;098b	07		.
	ld l,a			;098c	6f		o
	ld a,c			;098d	79		y
	and 007h		;098e	e6 07		. .
	ld b,a			;0990	47		G
	inc b			;0991	04		.
	ld a,(hl)		;0992	7e		~
l0993h:
	rlca			;0993	07		.
	djnz l0993h		;0994	10 fd		. .
	push af			;0996	f5		.
	rl e			;0997	cb 13		. .
	pop af			;0999	f1		.
	bit 5,(iy+015h)		;099a	fd cb 15 6e	. . . n
	jr z,l09a2h		;099e	28 02		( .
	rl e			;09a0	cb 13		. .
l09a2h:
	pop bc			;09a2	c1		.
	inc b			;09a3	04		.
	dec d			;09a4	15		.
	jr nz,l0975h		;09a5	20 ce		  .
	ld a,e			;09a7	7b		{
	bit 2,(iy+012h)		;09a8	fd cb 12 56	. . . V
	jr z,l09afh		;09ac	28 01		( .
	cpl			;09ae	2f		/
l09afh:
	rst 10h			;09af	d7		.
	bit 5,(iy+015h)		;09b0	fd cb 15 6e	. . . n
	jr z,l09b8h		;09b4	28 02		( .
	rst 10h			;09b6	d7		.
	rst 10h			;09b7	d7		.
l09b8h:
	inc c			;09b8	0c		.
	ld a,c			;09b9	79		y
	jr z,l09c0h		;09ba	28 04		( .
	pop bc			;09bc	c1		.
	ld c,a			;09bd	4f		O
	jr l096ah		;09be	18 aa		. .
l09c0h:
	pop de			;09c0	d1		.
	res 5,(iy+009h)		;09c1	fd cb 09 ae	. . . .
	rst 20h			;09c5	e7		.
	adc a,l			;09c6	8d		.
	ld a,0bfh		;09c7	3e bf		> .
	cp b			;09c9	b8		.
	jr nc,l0951h		;09ca	30 85		0 .
	ld hl,0e015h		;09cc	21 15 e0	! . .
sub_09cfh:
	ld b,(hl)		;09cf	46		F
	inc hl			;09d0	23		#
	jp l187fh		;09d1	c3 7f 18	. . .
	res 0,(iy+012h)		;09d4	fd cb 12 86	. . . .
	ld sp,0e25eh		;09d8	31 5e e2	1 ^ .
	jp l11beh		;09db	c3 be 11	. . .
sub_09deh:
	ld hl,0e043h		;09de	21 43 e0	! C .
	rst 30h			;09e1	f7		.
sub_09e2h:
	ld hl,l3c1bh		;09e2	21 1b 3c	! . <
	ld (ErrorTextTable),hl	;09e5	22 d8 dd	" . .
	ld hl,l39b4h		;09e8	21 b4 39	! . 9
	ld (ErrorTextTable2),hl	;09eb	22 da dd	" . .
	ret			;09ee	c9		.
sub_09efh:
	call sub_09deh		;09ef	cd de 09	. . .
	rlc a			;09f2	cb 07		. .
	jp z,l13b1h		;09f4	ca b1 13	. . .
	call sub_0a00h		;09f7	cd 00 0a	. . .
	rst 18h			;09fa	df		.
	ld b,e			;09fb	43		C
	ld (hl),0d8h		;09fc	36 d8		6 .
	ld a,00ch		;09fe	3e 0c		> .
sub_0a00h:
	push af			;0a00	f5		.
	ld hl,0de17h		;0a01	21 17 de	! . .
	ld (0de15h),hl		;0a04	22 15 de	" . .
	call c,sub_13adh	;0a07	dc ad 13	. . .
	call sub_265bh		;0a0a	cd 5b 26	. [ &
	pop af			;0a0d	f1		.
	push af			;0a0e	f5		.
	srl a			;0a0f	cb 3f		. ?
	ld hl,05ac0h		;0a11	21 c0 5a	! . Z
	ld e,l			;0a14	5d		]
	ld d,h			;0a15	54		T
	ld (hl),029h		;0a16	36 29		6 )
	cp 00ah			;0a18	fe 0a		. .
	jr c,l0a1eh		;0a1a	38 02		8 .
	ld (hl),02ah		;0a1c	36 2a		6 *
l0a1eh:
	inc de			;0a1e	13		.
	ld bc,Filler001B_end+1	;0a1f	01 1f 00	. . .
	ldir			;0a22	ed b0		. .
	inc (ix+001h)		;0a24	dd 34 01	. 4 .
	ld b,a			;0a27	47		G
	rst 8			;0a28	cf		.
	inc (ix+000h)		;0a29	dd 34 00	. 4 .
	pop af			;0a2c	f1		.
	ret			;0a2d	c9		.
l0a2eh:
	ld (iy+00ah),000h	;0a2e	fd 36 0a 00	. 6 . .
	push af			;0a32	f5		.
	call CommandLoop	;0a33	cd e2 13	. . .
	pop af			;0a36	f1		.
	jr ExitToError		;0a37	18 1b		. .
l0a39h:
	ld sp,0e25eh		;0a39	31 5e e2	1 ^ .
	set 0,(iy+012h)		;0a3c	fd cb 12 c6	. . . .
l0a40h:
	call ReadPort7FFD	;0a40	cd 11 04	. . .
	xor a			;0a43	af		.
	ld (0dfe9h),a		;0a44	32 e9 df	2 . .
	jr l0a4ah		;0a47	18 01		. .
l0a49h:
	scf			;0a49	37		7
l0a4ah:
	push af			;0a4a	f5		.
	call CommandLoop	;0a4b	cd e2 13	. . .
	pop af			;0a4e	f1		.
	call nc,TapeMenu	;0a4f	d4 6b 2b	. k +
	ld a,081h		;0a52	3e 81		> .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ExitToError: unwind to the command loop through the error
; window path (cf. InitErrorWindow $35CB).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ExitToError:
	ld sp,0e25eh		;0a54	31 5e e2	1 ^ .
	call sub_049bh		;0a57	cd 9b 04	. . .
	ld hl,ExitToError	;0a5a	21 54 0a	! T .
	push hl			;0a5d	e5		.
	call sub_09efh		;0a5e	cd ef 09	. . .
	jr nc,ParseCommand	;0a61	30 27		0 '
	call ScanTokens		;0a63	cd b5 13	. . .
	inc b			;0a66	04		.
	ld h,(hl)		;0a67	66		f
	rla			;0a68	17		.
	dec b			;0a69	05		.
	ld l,c			;0a6a	69		i
	rla			;0a6b	17		.
	rrca			;0a6c	0f		.
	ld h,e			;0a6d	63		c
	rrca			;0a6e	0f		.
	jp po,l0f49h		;0a6f	e2 49 0f	. I .
	rst 0			;0a72	c7		.
	ccf			;0a73	3f		?
	rrca			;0a74	0f		.
	ret z			;0a75	c8		.
	ld b,h			;0a76	44		D
	rrca			;0a77	0f		.
	ret			;0a78	c9		.
	dec (hl)		;0a79	35		5
	rrca			;0a7a	0f		.
	jp l0f3ah		;0a7b	c3 3a 0f	. : .
	call z,sub_2cb9h	;0a7e	cc b9 2c	. . ,
	call sub_3037h		;0a81	cd 37 30	. 7 0
	set 6,l			;0a84	cb f5		. .
	dec l			;0a86	2d		-
	rst 38h			;0a87	ff		.
	xor a			;0a88	af		.
	ret			;0a89	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ParseCommand: the command/expression reader.  Accepts a typed
; line into the IX work buffer, evaluates operands (bank extension
; syntax handled by EvalOperand at $136F) and reduces the input
; to tokens for DispatchMode.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ParseCommand:
	call sub_0e29h		;0a8a	cd 29 0e	. ) .
	ld e,000h		;0a8d	1e 00		. .
	cp 00dh			;0a8f	fe 0d		. .
	jp z,l0a40h		;0a91	ca 40 0a	. @ .
	cp 05bh			;0a94	fe 5b		. [
	jr z,l0aa0h		;0a96	28 08		( .
	cp 028h			;0a98	fe 28		. (
	jr nz,l0aa3h		;0a9a	20 07		  .
	ld e,004h		;0a9c	1e 04		. .
	jr l0aa2h		;0a9e	18 02		. .
l0aa0h:
	ld e,084h		;0aa0	1e 84		. .
l0aa2h:
	inc hl			;0aa2	23		#
l0aa3h:
	ld (iy+000h),e		;0aa3	fd 73 00	. s .
	bit 2,e			;0aa6	cb 53		. S
	jr nz,l0b16h		;0aa8	20 6c		  l
	ld de,l0ee4h		;0aaa	11 e4 0e	. . .
	ld bc,KeyScanTableB_start	;0aad	01 a5 0e	. . .
	call sub_0e6dh		;0ab0	cd 6d 0e	. m .
	jp z,DispatchMode	;0ab3	ca 59 0b	. Y .
	ex de,hl		;0ab6	eb		.
	cp 021h			;0ab7	fe 21		. !
	jr nc,l0ac7h		;0ab9	30 0c		0 .
	cp 00dh			;0abb	fe 0d		. .
	jr nc,l0ac8h		;0abd	30 09		0 .
	set 3,(iy+000h)		;0abf	fd cb 00 de	. . . .
	sla c			;0ac3	cb 21		. !
	jr l0acch		;0ac5	18 05		. .
l0ac7h:
	inc bc			;0ac7	03		.
l0ac8h:
	ld a,c			;0ac8	79		y
	sub 007h		;0ac9	d6 07		. .
	ld c,a			;0acb	4f		O
l0acch:
	ld a,(de)		;0acc	1a		.
	cp 03dh			;0acd	fe 3d		. =
	jp nz,DispatchMode	;0acf	c2 59 0b	. Y .
	inc de			;0ad2	13		.
	ex de,hl		;0ad3	eb		.
	push bc			;0ad4	c5		.
	call sub_0e00h		;0ad5	cd 00 0e	. . .
	pop bc			;0ad8	c1		.
	cp 001h			;0ad9	fe 01		. .
	ld a,00ah		;0adb	3e 0a		> .
	ret c			;0add	d8		.
	ld a,00bh		;0ade	3e 0b		> .
	ret nz			;0ae0	c0		.
	bit 2,(iy+000h)		;0ae1	fd cb 00 56	. . . V
	jr nz,l0b3ah		;0ae5	20 53		  S
	ld hl,0dd69h		;0ae7	21 69 dd	! i .
	add hl,bc		;0aea	09		.
	bit 3,(iy+000h)		;0aeb	fd cb 00 5e	. . . ^
	jr nz,l0afbh		;0aef	20 0a		  .
	ld a,d			;0af1	7a		z
	or a			;0af2	b7		.
	jr nz,l0b13h		;0af3	20 1e		  .
	call sub_0df0h		;0af5	cd f0 0d	. . .
	ld (hl),e		;0af8	73		s
	jr l0b10h		;0af9	18 15		. .
l0afbh:
	ld a,c			;0afb	79		y
	cp 002h			;0afc	fe 02		. .
	jr nz,l0b04h		;0afe	20 04		  .
	res 6,(iy+00ah)		;0b00	fd cb 0a b6	. . . .
l0b04h:
	or a			;0b04	b7		.
	push af			;0b05	f5		.
	call z,sub_0df7h	;0b06	cc f7 0d	. . .
	pop af			;0b09	f1		.
	call nz,sub_0df0h	;0b0a	c4 f0 0d	. . .
	ld (hl),e		;0b0d	73		s
	inc hl			;0b0e	23		#
	ld (hl),d		;0b0f	72		r
l0b10h:
	jp l0a49h		;0b10	c3 49 0a	. I .
l0b13h:
	ld a,00ch		;0b13	3e 0c		> .
	ret			;0b15	c9		.
l0b16h:
	call sub_0be2h		;0b16	cd e2 0b	. . .
	ret c			;0b19	d8		.
	ex de,hl		;0b1a	eb		.
	ld c,l			;0b1b	4d		M
	ld b,h			;0b1c	44		D
	ld a,(de)		;0b1d	1a		.
	inc de			;0b1e	13		.
	ld hl,IyWorkBase	;0b1f	21 df df	! . .
	cp 029h			;0b22	fe 29		. )
	jr z,l0b33h		;0b24	28 0d		( .
	cp 05dh			;0b26	fe 5d		. ]
	jp nz,DispatchMode	;0b28	c2 59 0b	. Y .
	bit 7,(hl)		;0b2b	cb 7e		. ~
	jr z,l0b37h		;0b2d	28 08		( .
	set 3,(hl)		;0b2f	cb de		. .
	jr l0acch		;0b31	18 99		. .
l0b33h:
	bit 7,(hl)		;0b33	cb 7e		. ~
	jr z,l0acch		;0b35	28 95		( .
l0b37h:
	ld a,015h		;0b37	3e 15		> .
	ret			;0b39	c9		.
l0b3ah:
	bit 3,(iy+000h)		;0b3a	fd cb 00 5e	. . . ^
	ld l,c			;0b3e	69		i
	ld h,b			;0b3f	60		`
	jr nz,l0b52h		;0b40	20 10		  .
	ld a,d			;0b42	7a		z
	or a			;0b43	b7		.
	jr nz,l0b13h		;0b44	20 cd		  .
	ld a,e			;0b46	7b		{
	rst 0			;0b47	c7		.
l0b48h:
	ld a,(0dfe9h)		;0b48	3a e9 df	: . .
	and 081h		;0b4b	e6 81		. .
	ld (0dfe9h),a		;0b4d	32 e9 df	2 . .
	jr l0b10h		;0b50	18 be		. .
l0b52h:
	ld a,e			;0b52	7b		{
	rst 0			;0b53	c7		.
	inc hl			;0b54	23		#
	ld a,d			;0b55	7a		z
	rst 0			;0b56	c7		.
	jr l0b48h		;0b57	18 ef		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DispatchMode: routes between line editor, disassembler and
; register views according to the monitor flags (IY+12/IY+16
; bits), then falls into CommandLoop for the next command.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DispatchMode:
	ld hl,0e0aeh		;0b59	21 ae e0	! . .
	ld b,003h		;0b5c	06 03		. .
l0b5eh:
	ld e,(hl)		;0b5e	5e		^
	inc hl			;0b5f	23		#
	ld d,(hl)		;0b60	56		V
	inc hl			;0b61	23		#
	ld (0dda7h),de		;0b62	ed 53 a7 dd	. S . .
	ld a,e			;0b66	7b		{
	or d			;0b67	b2		.
	jr z,l0b7ch		;0b68	28 12		( .
	ld e,(hl)		;0b6a	5e		^
	inc hl			;0b6b	23		#
	ld d,(hl)		;0b6c	56		V
	inc hl			;0b6d	23		#
	push hl			;0b6e	e5		.
	push bc			;0b6f	c5		.
	call sub_0e29h		;0b70	cd 29 0e	. ) .
	call FindKeyword	;0b73	cd 36 0e	. 6 .
	jr nz,l0b81h		;0b76	20 09		  .
	pop bc			;0b78	c1		.
	pop hl			;0b79	e1		.
	djnz l0b5eh		;0b7a	10 e2		. .
l0b7ch:
	ld a,016h		;0b7c	3e 16		> .
	jp ExitToError		;0b7e	c3 54 0a	. T .
l0b81h:
	pop de			;0b81	d1		.
	ex (sp),hl		;0b82	e3		.
	add a,c			;0b83	81		.
	add a,c			;0b84	81		.
	ld c,a			;0b85	4f		O
	ld hl,(0dda7h)		;0b86	2a a7 dd	* . .
	add hl,bc		;0b89	09		.
	ld e,(hl)		;0b8a	5e		^
	inc hl			;0b8b	23		#
	ld d,(hl)		;0b8c	56		V
	inc hl			;0b8d	23		#
	ld c,(hl)		;0b8e	4e		N
	pop hl			;0b8f	e1		.
	push de			;0b90	d5		.
	bit 7,c			;0b91	cb 79		. y
	jr nz,l0bb1h		;0b93	20 1c		  .
	bit 5,c			;0b95	cb 69		. i
	ret nz			;0b97	c0		.
	push bc			;0b98	c5		.
	call sub_0e00h		;0b99	cd 00 0e	. . .
	pop bc			;0b9c	c1		.
	bit 6,c			;0b9d	cb 71		. q
	jr nz,l0bach		;0b9f	20 0b		  .
	cp c			;0ba1	b9		.
	jr z,l0bach		;0ba2	28 08		( .
	ld a,00ah		;0ba4	3e 0a		> .
	jr c,l0ba9h		;0ba6	38 01		8 .
	inc a			;0ba8	3c		<
l0ba9h:
	jp ExitToError		;0ba9	c3 54 0a	. T .
l0bach:
	ld bc,(0dd9dh)		;0bac	ed 4b 9d dd	. K . .
	ret			;0bb0	c9		.
l0bb1h:
	ld b,000h		;0bb1	06 00		. .
	push hl			;0bb3	e5		.
l0bb4h:
	dec c			;0bb4	0d		.
	bit 7,c			;0bb5	cb 79		. y
	jr nz,l0bbdh		;0bb7	20 04		  .
	set 1,(iy+000h)		;0bb9	fd cb 00 ce	. . . .
l0bbdh:
	bit 1,(iy+000h)		;0bbd	fd cb 00 4e	. . . N
	push bc			;0bc1	c5		.
	jr nz,l0bc9h		;0bc2	20 05		  .
	call sub_0be2h		;0bc4	cd e2 0b	. . .
	jr l0bcch		;0bc7	18 03		. .
l0bc9h:
	call sub_0c9fh		;0bc9	cd 9f 0c	. . .
l0bcch:
	pop bc			;0bcc	c1		.
	jr z,l0bd4h		;0bcd	28 05		( .
	jp nc,ExitToError	;0bcf	d2 54 0a	. T .
	pop hl			;0bd2	e1		.
	ret			;0bd3	c9		.
l0bd4h:
	inc b			;0bd4	04		.
	bit 7,c			;0bd5	cb 79		. y
	jr nz,l0bb4h		;0bd7	20 db		  .
	ld a,d			;0bd9	7a		z
	or a			;0bda	b7		.
	jr z,l0bb4h		;0bdb	28 d7		( .
	ld a,00ch		;0bdd	3e 0c		> .
	jp ExitToError		;0bdf	c3 54 0a	. T .
sub_0be2h:
	dec hl			;0be2	2b		+
l0be3h:
	inc hl			;0be3	23		#
	call sub_0c8ch		;0be4	cd 8c 0c	. . .
	jr nz,l0bf1h		;0be7	20 08		  .
	jr nc,l0be3h		;0be9	30 f8		0 .
	xor a			;0beb	af		.
	inc a			;0bec	3c		<
	ld a,012h		;0bed	3e 12		> .
	scf			;0bef	37		7
	ret			;0bf0	c9		.
l0bf1h:
	ld (iy+00fh),000h	;0bf1	fd 36 0f 00	. 6 . .
l0bf5h:
	ld de,WriteAnyBankByte	;0bf5	11 00 00	. . .
	push de			;0bf8	d5		.
	ld d,02bh		;0bf9	16 2b		. +
	ld a,(hl)		;0bfb	7e		~
	call sub_0c82h		;0bfc	cd 82 0c	. . .
	jr nz,l0c03h		;0bff	20 02		  .
	inc hl			;0c01	23		#
l0c02h:
	ld d,a			;0c02	57		W
l0c03h:
	push de			;0c03	d5		.
	ld a,(hl)		;0c04	7e		~
	cp 028h			;0c05	fe 28		. (
	jr z,l0c0dh		;0c07	28 04		( .
	cp 05bh			;0c09	fe 5b		. [
	jr nz,l0c16h		;0c0b	20 09		  .
l0c0dh:
	inc hl			;0c0d	23		#
	inc (iy+00fh)		;0c0e	fd 34 0f	. 4 .
	pop de			;0c11	d1		.
	ld e,a			;0c12	5f		_
	push de			;0c13	d5		.
	jr l0bf5h		;0c14	18 df		. .
l0c16h:
	call sub_0c9fh		;0c16	cd 9f 0c	. . .
	jr nz,l0c42h		;0c19	20 27		  '
l0c1bh:
	pop af			;0c1b	f1		.
	ex (sp),hl		;0c1c	e3		.
	cp 02dh			;0c1d	fe 2d		. -
	jr z,l0c24h		;0c1f	28 03		( .
	add hl,de		;0c21	19		.
	jr l0c27h		;0c22	18 03		. .
l0c24h:
	or a			;0c24	b7		.
	sbc hl,de		;0c25	ed 52		. R
l0c27h:
	ex (sp),hl		;0c27	e3		.
	ld a,(hl)		;0c28	7e		~
	inc hl			;0c29	23		#
	call sub_0c82h		;0c2a	cd 82 0c	. . .
	jr z,l0c02h		;0c2d	28 d3		( .
	cp 029h			;0c2f	fe 29		. )
	jr z,l0c45h		;0c31	28 12		( .
	cp 05dh			;0c33	fe 5d		. ]
	jr z,l0c58h		;0c35	28 21		( !
	dec hl			;0c37	2b		+
	xor a			;0c38	af		.
	cp (iy+00fh)		;0c39	fd be 0f	. . .
	jr nz,l0c40h		;0c3c	20 02		  .
	pop de			;0c3e	d1		.
	ret			;0c3f	c9		.
l0c40h:
	ld a,013h		;0c40	3e 13		> .
l0c42h:
	jp ExitToError		;0c42	c3 54 0a	. T .
l0c45h:
	pop de			;0c45	d1		.
	call sub_0c6ch		;0c46	cd 6c 0c	. l .
	pop af			;0c49	f1		.
	push af			;0c4a	f5		.
	ld a,015h		;0c4b	3e 15		> .
	jr c,l0c42h		;0c4d	38 f3		8 .
	push hl			;0c4f	e5		.
	ex de,hl		;0c50	eb		.
	rst 28h			;0c51	ef		.
	ld e,a			;0c52	5f		_
	ld d,000h		;0c53	16 00		. .
	pop hl			;0c55	e1		.
	jr l0c1bh		;0c56	18 c3		. .
l0c58h:
	pop de			;0c58	d1		.
	call sub_0c6ch		;0c59	cd 6c 0c	. l .
	pop af			;0c5c	f1		.
	push af			;0c5d	f5		.
	ld a,015h		;0c5e	3e 15		> .
	jr nc,l0c42h		;0c60	30 e0		0 .
	push hl			;0c62	e5		.
	ex de,hl		;0c63	eb		.
	rst 28h			;0c64	ef		.
	ld e,a			;0c65	5f		_
	inc hl			;0c66	23		#
	rst 28h			;0c67	ef		.
	ld d,a			;0c68	57		W
	pop hl			;0c69	e1		.
	jr l0c1bh		;0c6a	18 af		. .
sub_0c6ch:
	xor a			;0c6c	af		.
	cp (iy+00fh)		;0c6d	fd be 0f	. . .
	jr z,l0c76h		;0c70	28 04		( .
	dec (iy+00fh)		;0c72	fd 35 0f	. 5 .
	ret			;0c75	c9		.
l0c76h:
	bit 2,(iy+000h)		;0c76	fd cb 00 56	. . . V
	ld a,014h		;0c7a	3e 14		> .
	jr z,l0c42h		;0c7c	28 c4		( .
	pop bc			;0c7e	c1		.
	dec hl			;0c7f	2b		+
	xor a			;0c80	af		.
	ret			;0c81	c9		.
sub_0c82h:
	cp 02bh			;0c82	fe 2b		. +
	ret z			;0c84	c8		.
	cp 02dh			;0c85	fe 2d		. -
	ret			;0c87	c9		.
	ld a,(hl)		;0c88	7e		~
	cp 03ah			;0c89	fe 3a		. :
	ret z			;0c8b	c8		.
sub_0c8ch:
	ld a,(hl)		;0c8c	7e		~
	cp 020h			;0c8d	fe 20		.  
	ret z			;0c8f	c8		.
	ld a,(hl)		;0c90	7e		~
	cp 02ch			;0c91	fe 2c		. ,
	ret z			;0c93	c8		.
	cp 03bh			;0c94	fe 3b		. ;
	ret z			;0c96	c8		.
	or a			;0c97	b7		.
	scf			;0c98	37		7
	ret z			;0c99	c8		.
	cp 00dh			;0c9a	fe 0d		. .
	ret nz			;0c9c	c0		.
	scf			;0c9d	37		7
	ret			;0c9e	c9		.
sub_0c9fh:
	bit 0,(iy+000h)		;0c9f	fd cb 00 46	. . . F
	ld a,010h		;0ca3	3e 10		> .
	ld (0dfefh),a		;0ca5	32 ef df	2 . .
	jr nz,l0cbeh		;0ca8	20 14		  .
	dec hl			;0caa	2b		+
l0cabh:
	inc hl			;0cab	23		#
	res 0,(iy+000h)		;0cac	fd cb 00 86	. . . .
	call sub_0c8ch		;0cb0	cd 8c 0c	. . .
	jr nz,l0cb9h		;0cb3	20 04		  .
	jr c,l0cf0h		;0cb5	38 39		8 9
	jr l0cabh		;0cb7	18 f2		. .
l0cb9h:
	cp 022h			;0cb9	fe 22		. "
	jr nz,l0cf6h		;0cbb	20 39		  9
	inc hl			;0cbd	23		#
l0cbeh:
	ld a,(hl)		;0cbe	7e		~
	cp 00dh			;0cbf	fe 0d		. .
	jr z,l0cech		;0cc1	28 29		( )
	cp 022h			;0cc3	fe 22		. "
	jr z,l0ce6h		;0cc5	28 1f		( .
	ld e,a			;0cc7	5f		_
	ld d,000h		;0cc8	16 00		. .
	bit 1,(iy+000h)		;0cca	fd cb 00 4e	. . . N
	jr nz,l0cd8h		;0cce	20 08		  .
	inc hl			;0cd0	23		#
	ld a,022h		;0cd1	3e 22		> "
	cp (hl)			;0cd3	be		.
	jr nz,l0cech		;0cd4	20 16		  .
	jr l0cdch		;0cd6	18 04		. .
l0cd8h:
	set 0,(iy+000h)		;0cd8	fd cb 00 c6	. . . .
l0cdch:
	inc hl			;0cdc	23		#
	xor a			;0cdd	af		.
	ld (0dfefh),a		;0cde	32 ef df	2 . .
	ret			;0ce1	c9		.
	ld a,00dh		;0ce2	3e 0d		> .
	or a			;0ce4	b7		.
	ret			;0ce5	c9		.
l0ce6h:
	bit 1,(iy+000h)		;0ce6	fd cb 00 4e	. . . N
	jr nz,l0cabh		;0cea	20 bf		  .
l0cech:
	ld a,00eh		;0cec	3e 0e		> .
	or a			;0cee	b7		.
	ret			;0cef	c9		.
l0cf0h:
	xor a			;0cf0	af		.
	inc a			;0cf1	3c		<
	ld a,00fh		;0cf2	3e 0f		> .
	scf			;0cf4	37		7
	ret			;0cf5	c9		.
l0cf6h:
	ld de,l0edfh		;0cf6	11 df 0e	. . .
	ld bc,l0e80h		;0cf9	01 80 0e	. . .
	call sub_0e6dh		;0cfc	cd 6d 0e	. m .
	jr nz,l0d07h		;0cff	20 06		  .
	call 00d3eh		;0d01	cd 3e 0d	. > .
	ret z			;0d04	c8		.
	or a			;0d05	b7		.
	ret			;0d06	c9		.
l0d07h:
	ld de,l0001h		;0d07	11 01 00	. . .
	ld a,c			;0d0a	79		y
	or a			;0d0b	b7		.
	ret z			;0d0c	c8		.
	dec de			;0d0d	1b		.
	dec a			;0d0e	3d		=
	ret z			;0d0f	c8		.
	push hl			;0d10	e5		.
	ld hl,0dd84h		;0d11	21 84 dd	! . .
	ld a,c			;0d14	79		y
	sub 023h		;0d15	d6 23		. #
	jr nc,l0d2eh		;0d17	30 15		0 .
	ld hl,0dd6fh		;0d19	21 6f dd	! o .
	ld a,c			;0d1c	79		y
	sub 00fh		;0d1d	d6 0f		. .
	jr nc,l0d2eh		;0d1f	30 0d		0 .
	dec bc			;0d21	0b		.
	dec bc			;0d22	0b		.
	ld hl,0dd69h		;0d23	21 69 dd	! i .
	add hl,bc		;0d26	09		.
	add hl,bc		;0d27	09		.
	ld e,(hl)		;0d28	5e		^
	inc hl			;0d29	23		#
	ld d,(hl)		;0d2a	56		V
l0d2bh:
	pop hl			;0d2b	e1		.
	xor a			;0d2c	af		.
	ret			;0d2d	c9		.
l0d2eh:
	add a,l			;0d2e	85		.
	ld l,a			;0d2f	6f		o
	jr nc,l0d33h		;0d30	30 01		0 .
	inc h			;0d32	24		$
l0d33h:
	ld e,(hl)		;0d33	5e		^
	jr l0d2bh		;0d34	18 f5		. .
l0d36h:
	dec h			;0d36	25		%
	ld (bc),a		;0d37	02		.
	ld b,b			;0d38	40		@
	ex af,af'		;0d39	08		.
	ld l,00ah		;0d3a	2e 0a		. .
	inc hl			;0d3c	23		#
	djnz l0d79h		;0d3d	10 3a		. :
	or 0dfh			;0d3f	f6 df		. .
	ld b,00ah		;0d41	06 0a		. .
	or a			;0d43	b7		.
	jr z,l0d47h		;0d44	28 01		( .
	ld b,a			;0d46	47		G
l0d47h:
	call sub_0da3h		;0d47	cd a3 0d	. . .
	jr nc,l0d61h		;0d4a	30 15		0 .
	ld de,l0d36h		;0d4c	11 36 0d	. 6 .
	ld b,004h		;0d4f	06 04		. .
l0d51h:
	ld a,(de)		;0d51	1a		.
	inc de			;0d52	13		.
	cp (hl)			;0d53	be		.
	ld a,(de)		;0d54	1a		.
	inc de			;0d55	13		.
	jr z,l0d5fh		;0d56	28 07		( .
	djnz l0d51h		;0d58	10 f7		. .
	ld b,(iy+017h)		;0d5a	fd 46 17	. F .
	jr l0d61h		;0d5d	18 02		. .
l0d5fh:
	inc hl			;0d5f	23		#
	ld b,a			;0d60	47		G
l0d61h:
	ld (iy+010h),b		;0d61	fd 70 10	. p .
	call sub_0d69h		;0d64	cd 69 0d	. i .
	or a			;0d67	b7		.
	ret			;0d68	c9		.
sub_0d69h:
	call sub_0dabh		;0d69	cd ab 0d	. . .
	ld a,010h		;0d6c	3e 10		> .
	ret c			;0d6e	d8		.
	ld b,000h		;0d6f	06 00		. .
	ld d,b			;0d71	50		P
	ld e,c			;0d72	59		Y
	inc hl			;0d73	23		#
l0d74h:
	call sub_0dabh		;0d74	cd ab 0d	. . .
	jr c,l0d97h		;0d77	38 1e		8 .
l0d79h:
	push hl			;0d79	e5		.
	ld hl,(0dfefh)		;0d7a	2a ef df	* . .
	ld h,000h		;0d7d	26 00		& .
	ex de,hl		;0d7f	eb		.
	push bc			;0d80	c5		.
	ld (iy+00eh),000h	;0d81	fd 36 0e 00	. 6 . .
	call sub_0dd3h		;0d85	cd d3 0d	. . .
	pop bc			;0d88	c1		.
	bit 0,(iy+00eh)		;0d89	fd cb 0e 46	. . . F
	jr nz,l0d9fh		;0d8d	20 10		  .
	add hl,bc		;0d8f	09		.
	jr c,l0d9fh		;0d90	38 0d		8 .
	ex de,hl		;0d92	eb		.
	pop hl			;0d93	e1		.
	inc hl			;0d94	23		#
	jr l0d74h		;0d95	18 dd		. .
l0d97h:
	call sub_0da3h		;0d97	cd a3 0d	. . .
	ld a,010h		;0d9a	3e 10		> .
	ret nc			;0d9c	d0		.
	xor a			;0d9d	af		.
	ret			;0d9e	c9		.
l0d9fh:
	pop hl			;0d9f	e1		.
	ld a,011h		;0da0	3e 11		> .
	ret			;0da2	c9		.
sub_0da3h:
	ld a,(hl)		;0da3	7e		~
	cp 030h			;0da4	fe 30		. 0
	ret c			;0da6	d8		.
	cp 03ah			;0da7	fe 3a		. :
	ccf			;0da9	3f		?
	ret			;0daa	c9		.
sub_0dabh:
	ld a,(hl)		;0dab	7e		~
	cp 061h			;0dac	fe 61		. a
	jr c,l0db2h		;0dae	38 02		8 .
	sub 020h		;0db0	d6 20		.  
l0db2h:
	sub 030h		;0db2	d6 30		. 0
	ret c			;0db4	d8		.
	cp 00ah			;0db5	fe 0a		. .
	jr c,l0dbeh		;0db7	38 05		8 .
	cp 011h			;0db9	fe 11		. .
	ret c			;0dbb	d8		.
	sub 007h		;0dbc	d6 07		. .
l0dbeh:
	ld c,a			;0dbe	4f		O
	ld a,(0dfefh)		;0dbf	3a ef df	: . .
	dec a			;0dc2	3d		=
	cp c			;0dc3	b9		.
	ret			;0dc4	c9		.
	ld a,d			;0dc5	7a		z
	or e			;0dc6	b3		.
	jr z,l0dceh		;0dc7	28 05		( .
	ld a,h			;0dc9	7c		|
	ld c,l			;0dca	4d		M
	jp Divide16		;0dcb	c3 6a 19	. j .
l0dceh:
	set 2,(iy+00eh)		;0dce	fd cb 0e d6	. . . .
	ret			;0dd2	c9		.
sub_0dd3h:
	ld c,h			;0dd3	4c		L
	ld a,l			;0dd4	7d		}
	ld b,010h		;0dd5	06 10		. .
	ld hl,WriteAnyBankByte	;0dd7	21 00 00	! . .
l0ddah:
	add hl,hl		;0dda	29		)
	jr nc,l0de1h		;0ddb	30 04		0 .
	set 0,(iy+00eh)		;0ddd	fd cb 0e c6	. . . .
l0de1h:
	rla			;0de1	17		.
	rl c			;0de2	cb 11		. .
	jr nc,l0dedh		;0de4	30 07		0 .
	add hl,de		;0de6	19		.
	jr nc,l0dedh		;0de7	30 04		0 .
	set 0,(iy+00eh)		;0de9	fd cb 0e c6	. . . .
l0dedh:
	djnz l0ddah		;0ded	10 eb		. .
	ret			;0def	c9		.
sub_0df0h:
	ld a,(0dfe9h)		;0df0	3a e9 df	: . .
	and 0f5h		;0df3	e6 f5		. .
	jr l0dfch		;0df5	18 05		. .
sub_0df7h:
	ld a,(0dfe9h)		;0df7	3a e9 df	: . .
	and 0cfh		;0dfa	e6 cf		. .
l0dfch:
	ld (0dfe9h),a		;0dfc	32 e9 df	2 . .
	ret			;0dff	c9		.
sub_0e00h:
	xor a			;0e00	af		.
	ld bc,RegisterBackup	;0e01	01 99 dd	. . .
l0e04h:
	push af			;0e04	f5		.
	push bc			;0e05	c5		.
	call sub_0be2h		;0e06	cd e2 0b	. . .
	pop bc			;0e09	c1		.
	jr c,l0e20h		;0e0a	38 14		8 .
	jp nz,ExitToError	;0e0c	c2 54 0a	. T .
	ld a,e			;0e0f	7b		{
	ld (bc),a		;0e10	02		.
	inc bc			;0e11	03		.
	ld a,d			;0e12	7a		z
	ld (bc),a		;0e13	02		.
	inc bc			;0e14	03		.
	pop af			;0e15	f1		.
	inc a			;0e16	3c		<
	cp 006h			;0e17	fe 06		. .
	jr nz,l0e04h		;0e19	20 e9		  .
	ld a,00bh		;0e1b	3e 0b		> .
	jp ExitToError		;0e1d	c3 54 0a	. T .
l0e20h:
	pop af			;0e20	f1		.
	ld de,(RegisterBackup)	;0e21	ed 5b 99 dd	. [ . .
	ld hl,(0dd9bh)		;0e25	2a 9b dd	* . .
	ret			;0e28	c9		.
sub_0e29h:
	ld hl,0de17h		;0e29	21 17 de	! . .
l0e2ch:
	ld a,(hl)		;0e2c	7e		~
	cp 020h			;0e2d	fe 20		.  
	inc hl			;0e2f	23		#
	jr z,l0e2ch		;0e30	28 fa		( .
	dec hl			;0e32	2b		+
	cp 00dh			;0e33	fe 0d		. .
	ret			;0e35	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FindKeyword: match (possibly abbreviated) input against the
; command/mnemonic token tables.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FindKeyword:
	ld c,000h		;0e36	0e 00		. .
l0e38h:
	push hl			;0e38	e5		.
l0e39h:
	ld a,(de)		;0e39	1a		.
	ld b,a			;0e3a	47		G
	and 07fh		;0e3b	e6 7f		. .
	cp 061h			;0e3d	fe 61		. a
	jr c,l0e43h		;0e3f	38 02		8 .
	set 7,c			;0e41	cb f9		. .
l0e43h:
	ld a,b			;0e43	78		x
	call KeyScanTableB_end	;0e44	cd c8 0e	. . .
	ld b,a			;0e47	47		G
	ld a,(hl)		;0e48	7e		~
	call KeyScanTableB_end	;0e49	cd c8 0e	. . .
	cp b			;0e4c	b8		.
	jr nz,l0e5dh		;0e4d	20 0e		  .
	ld a,(de)		;0e4f	1a		.
	bit 7,a			;0e50	cb 7f		. .
	inc hl			;0e52	23		#
	inc de			;0e53	13		.
	jr z,l0e39h		;0e54	28 e3		( .
l0e56h:
	pop de			;0e56	d1		.
	res 7,c			;0e57	cb b9		. .
	ld a,c			;0e59	79		y
	ld b,000h		;0e5a	06 00		. .
	ret			;0e5c	c9		.
l0e5dh:
	bit 7,c			;0e5d	cb 79		. y
	jr nz,l0e56h		;0e5f	20 f5		  .
	pop hl			;0e61	e1		.
l0e62h:
	ld a,(de)		;0e62	1a		.
	rlca			;0e63	07		.
	inc de			;0e64	13		.
	jr nc,l0e62h		;0e65	30 fb		0 .
	inc c			;0e67	0c		.
	ld a,(de)		;0e68	1a		.
	or a			;0e69	b7		.
	jr nz,l0e38h		;0e6a	20 cc		  .
	ret			;0e6c	c9		.
sub_0e6dh:
	push bc			;0e6d	c5		.
	call FindKeyword	;0e6e	cd 36 0e	. 6 .
	pop de			;0e71	d1		.
	ret z			;0e72	c8		.
	push de			;0e73	d5		.
	ex (sp),hl		;0e74	e3		.
	push af			;0e75	f5		.
	add a,l			;0e76	85		.
	ld l,a			;0e77	6f		o
	jr nc,l0e7bh		;0e78	30 01		0 .
	inc h			;0e7a	24		$
l0e7bh:
	pop af			;0e7b	f1		.
	ld a,(hl)		;0e7c	7e		~
	ld c,a			;0e7d	4f		O
	pop hl			;0e7e	e1		.
	ret			;0e7f	c9		.
l0e80h:
	nop			;0e80	00		.
	ld bc,l2302h		;0e81	01 02 23	. . #
	ld c,00dh		;0e84	0e 0d		. .
	ld (l2120h),hl		;0e86	22 20 21	"   !
	rra			;0e89	1f		.
	rrca			;0e8a	0f		.
	djnz KeyScanTableA_end	;0e8b	10 11		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyScanTableA: first key-translate table of
; the editor - internal key codes in scan
; order, consumed by the loop that ends at
; $0E8B; continues in KeyScanTableB ($0EA5).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KeyScanTableA' (start 0x0e8d end 0x0e9e)
KeyScanTableA_start:
	defb 012h		;0e8d	12		.
	defb 005h		;0e8e	05		.
	defb 006h		;0e8f	06		.
	defb 003h		;0e90	03		.
	defb 004h		;0e91	04		.
	defb 00ah		;0e92	0a		.
	defb 00bh		;0e93	0b		.
	defb 00ch		;0e94	0c		.
	defb 007h		;0e95	07		.
	defb 008h		;0e96	08		.
	defb 009h		;0e97	09		.
	defb 019h		;0e98	19		.
	defb 01ah		;0e99	1a		.
	defb 01bh		;0e9a	1b		.
	defb 01ch		;0e9b	1c		.
	defb 01dh		;0e9c	1d		.
	defb 01eh		;0e9d	1e		.
KeyScanTableA_end:
	inc de			;0e9e	13		.
	inc d			;0e9f	14		.
	dec d			;0ea0	15		.
	ld d,017h		;0ea1	16 17		. .
	jr $+38			;0ea3	18 24		. $
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyScanTableB: second key-translate table,
; loaded as BC by the command editor ($0AAD);
; the final $22 byte acts as a sentinel.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KeyScanTableB' (start 0x0ea5 end 0x0ec8)
KeyScanTableB_start:
	defb 000h		;0ea5	00		.
	defb 021h		;0ea6	21		!
	defb 00ch		;0ea7	0c		.
	defb 00bh		;0ea8	0b		.
	defb 020h		;0ea9	20		 
	defb 01eh		;0eaa	1e		.
	defb 01fh		;0eab	1f		.
	defb 01dh		;0eac	1d		.
	defb 00dh		;0ead	0d		.
	defb 00eh		;0eae	0e		.
	defb 00fh		;0eaf	0f		.
	defb 010h		;0eb0	10		.
	defb 003h		;0eb1	03		.
	defb 004h		;0eb2	04		.
	defb 001h		;0eb3	01		.
	defb 002h		;0eb4	02		.
	defb 008h		;0eb5	08		.
	defb 009h		;0eb6	09		.
	defb 00ah		;0eb7	0a		.
	defb 005h		;0eb8	05		.
	defb 006h		;0eb9	06		.
	defb 007h		;0eba	07		.
	defb 017h		;0ebb	17		.
	defb 018h		;0ebc	18		.
	defb 019h		;0ebd	19		.
	defb 01ah		;0ebe	1a		.
	defb 01bh		;0ebf	1b		.
	defb 01ch		;0ec0	1c		.
	defb 011h		;0ec1	11		.
	defb 012h		;0ec2	12		.
	defb 013h		;0ec3	13		.
	defb 014h		;0ec4	14		.
	defb 015h		;0ec5	15		.
	defb 016h		;0ec6	16		.
	defb 022h		;0ec7	22		"
KeyScanTableB_end:
	and 07fh		;0ec8	e6 7f		. .
	cp 040h			;0eca	fe 40		. @
	ret c			;0ecc	d8		.
	and 05fh		;0ecd	e6 5f		. _
	ret			;0ecf	c9		.
l0ed0h:
	ld d,b			;0ed0	50		P
	jp 0d053h		;0ed1	c3 53 d0	. S .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegisterNameStrings: pool of short register-name
; strings, the last character of each carrying
; bit 7 ('I','I','H','D','B','AF','A','IX','IX',
; 'IY','IY','P','S','HL','DE','BC' plus single
; letters); indexed by the register display and
; editor. See also the inline-string printer at
; RegisterNameTable ($1531).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'RegisterNameStrings' (start 0x0ed4 end 0x0f2a)
RegisterNameStrings_start:
	defb 049h		;0ed4	49		I
	defb 0d8h		;0ed5	d8		.
	defb 049h		;0ed6	49		I
	defb 0d9h		;0ed7	d9		.
	defb 048h		;0ed8	48		H
	defb 0cch		;0ed9	cc		.
	defb 044h		;0eda	44		D
	defb 0c5h		;0edb	c5		.
	defb 042h		;0edc	42		B
	defb 0c3h		;0edd	c3		.
	defb 000h		;0ede	00		.
l0edfh:
	defb 04fh		;0edf	4f		O
	defb 0ceh		;0ee0	ce		.
l0ee1h:
	defb 04fh		;0ee1	4f		O
	defb 046h		;0ee2	46		F
	defb 0c6h		;0ee3	c6		.
l0ee4h:
	defb 04dh		;0ee4	4d		M
	defb 045h		;0ee5	45		E
	defb 0cdh		;0ee6	cd		.
	defb 0d2h		;0ee7	d2		.
	defb 041h		;0ee8	41		A
	defb 046h		;0ee9	46		F
	defb 0a7h		;0eea	a7		.
	defb 041h		;0eeb	41		A
	defb 0c6h		;0eec	c6		.
	defb 041h		;0eed	41		A
	defb 0a7h		;0eee	a7		.
	defb 0c1h		;0eef	c1		.
	defb 046h		;0ef0	46		F
	defb 0a7h		;0ef1	a7		.
	defb 0c6h		;0ef2	c6		.
	defb 049h		;0ef3	49		I
	defb 058h		;0ef4	58		X
	defb 0cch		;0ef5	cc		.
	defb 049h		;0ef6	49		I
	defb 058h		;0ef7	58		X
	defb 0c8h		;0ef8	c8		.
	defb 049h		;0ef9	49		I
	defb 059h		;0efa	59		Y
	defb 0cch		;0efb	cc		.
	defb 049h		;0efc	49		I
	defb 059h		;0efd	59		Y
	defb 0c8h		;0efe	c8		.
	defb 049h		;0eff	49		I
	defb 0d8h		;0f00	d8		.
	defb 049h		;0f01	49		I
	defb 0d9h		;0f02	d9		.
	defb 050h		;0f03	50		P
	defb 0c3h		;0f04	c3		.
	defb 053h		;0f05	53		S
	defb 0d0h		;0f06	d0		.
	defb 048h		;0f07	48		H
	defb 04ch		;0f08	4c		L
	defb 0a7h		;0f09	a7		.
	defb 044h		;0f0a	44		D
	defb 045h		;0f0b	45		E
	defb 0a7h		;0f0c	a7		.
	defb 042h		;0f0d	42		B
	defb 043h		;0f0e	43		C
	defb 0a7h		;0f0f	a7		.
	defb 048h		;0f10	48		H
	defb 0cch		;0f11	cc		.
	defb 044h		;0f12	44		D
	defb 0c5h		;0f13	c5		.
	defb 042h		;0f14	42		B
	defb 0c3h		;0f15	c3		.
	defb 04ch		;0f16	4c		L
	defb 0a7h		;0f17	a7		.
	defb 048h		;0f18	48		H
	defb 0a7h		;0f19	a7		.
	defb 045h		;0f1a	45		E
	defb 0a7h		;0f1b	a7		.
	defb 044h		;0f1c	44		D
	defb 0a7h		;0f1d	a7		.
	defb 043h		;0f1e	43		C
	defb 0a7h		;0f1f	a7		.
	defb 042h		;0f20	42		B
	defb 0a7h		;0f21	a7		.
	defb 0cch		;0f22	cc		.
	defb 0c8h		;0f23	c8		.
	defb 0c5h		;0f24	c5		.
	defb 0c4h		;0f25	c4		.
	defb 0c3h		;0f26	c3		.
	defb 0c2h		;0f27	c2		.
	defb 0c9h		;0f28	c9		.
	defb 000h		;0f29	00		.
RegisterNameStrings_end:
	ex de,hl		;0f2a	eb		.
l0f2bh:
	dec b			;0f2b	05		.
	ret z			;0f2c	c8		.
l0f2dh:
	ld a,(de)		;0f2d	1a		.
	inc de			;0f2e	13		.
	and 080h		;0f2f	e6 80		. .
	jr nz,l0f2bh		;0f31	20 f8		  .
	jr l0f2dh		;0f33	18 f8		. .
	ld bc,0fff8h		;0f35	01 f8 ff	. . .
	jr l0f4fh		;0f38	18 15		. .
l0f3ah:
	ld bc,Rst08Vector	;0f3a	01 08 00	. . .
	jr l0f4fh		;0f3d	18 10		. .
	ld bc,0ffffh		;0f3f	01 ff ff	. . .
	jr l0f4fh		;0f42	18 0b		. .
	ld bc,l0001h		;0f44	01 01 00	. . .
	jr l0f4fh		;0f47	18 06		. .
l0f49h:
	ld hl,(0dd69h)		;0f49	2a 69 dd	* i .
	call FetchPrefixBytes	;0f4c	cd b1 16	. . .
l0f4fh:
	ld hl,(0dd69h)		;0f4f	2a 69 dd	* i .
	add hl,bc		;0f52	09		.
	ld (0dd69h),hl		;0f53	22 69 dd	" i .
	ld a,001h		;0f56	3e 01		> .
	bit 0,(iy+00ah)		;0f58	fd cb 0a 46	. . . F
	jr z,l0f75h		;0f5c	28 17		( .
	call MemoryDumpWindow	;0f5e	cd 78 15	. x .
	xor a			;0f61	af		.
	ret			;0f62	c9		.
	ld hl,(UserPc)		;0f63	2a 6b dd	* k .
	call FetchPrefixBytes	;0f66	cd b1 16	. . .
	ld (UserPc),hl		;0f69	22 6b dd	" k .
	ld a,(iy+00ah)		;0f6c	fd 7e 0a	. ~ .
	and 0b5h		;0f6f	e6 b5		. .
	ld (iy+00ah),a		;0f71	fd 77 0a	. w .
	xor a			;0f74	af		.
l0f75h:
	push af			;0f75	f5		.
	call CommandLoop	;0f76	cd e2 13	. . .
	pop af			;0f79	f1		.
	ret			;0f7a	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ReportError - RST 08h backend, error code in B.  Walks the
; message table at (ErrorTextTable) with C as a rotating bit mask
; ($80, sla per entry); bit 7 chain-loads sub-strings.  Codes
; without a table entry fall back to printing the number.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ReportError:
	ld hl,(ErrorTextTable)	;0f7b	2a d8 dd	* . .
	ld c,080h		;0f7e	0e 80		. .
	call RegisterNameStrings_end	;0f80	cd 2a 0f	. * .
	ex de,hl		;0f83	eb		.
l0f84h:
	ld a,(hl)		;0f84	7e		~
	and 07fh		;0f85	e6 7f		. .
	jr z,l0fb3h		;0f87	28 2a		( *
	ld b,a			;0f89	47		G
	push hl			;0f8a	e5		.
	ld hl,(ErrorTextTable2)	;0f8b	2a da dd	* . .
	call RegisterNameStrings_end	;0f8e	cd 2a 0f	. * .
	pop hl			;0f91	e1		.
l0f92h:
	ld a,(de)		;0f92	1a		.
	sla c			;0f93	cb 21		. !
	jr nc,l0fa4h		;0f95	30 0d		0 .
	push af			;0f97	f5		.
	and 07fh		;0f98	e6 7f		. .
	cp 040h			;0f9a	fe 40		. @
	jr c,l0fa3h		;0f9c	38 05		8 .
	pop af			;0f9e	f1		.
	and 0dfh		;0f9f	e6 df		. .
	jr l0fa4h		;0fa1	18 01		. .
l0fa3h:
	pop af			;0fa3	f1		.
l0fa4h:
	call PrintCharBit7	;0fa4	cd b7 26	. . &
	rla			;0fa7	17		.
	inc de			;0fa8	13		.
	jr nc,l0f92h		;0fa9	30 e7		0 .
	rst 20h			;0fab	e7		.
	and b			;0fac	a0		.
l0fadh:
	bit 7,(hl)		;0fad	cb 7e		. ~
	inc hl			;0faf	23		#
	jr z,l0f84h		;0fb0	28 d2		( .
	ret			;0fb2	c9		.
l0fb3h:
	push hl			;0fb3	e5		.
	sla c			;0fb4	cb 21		. !
	push bc			;0fb6	c5		.
	ld hl,(0dda7h)		;0fb7	2a a7 dd	* . .
	call PrintMarkerChar	;0fba	cd d2 18	. . .
	pop bc			;0fbd	c1		.
	pop hl			;0fbe	e1		.
	jr l0fadh		;0fbf	18 ec		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BootContinue - boot bookkeeping at $C000..$C006: the signature
; must read byte i = i (walked down from $C006).  Broken -> RAM
; re-initialised and control loops back to the cold path; intact
; -> pass counter bumped.  v2.95 adds a ROM checksum check here.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BootContinue:
	ld de,0c006h		;0fc1	11 06 c0	. . .
	ld a,(de)		;0fc4	1a		.
	sub e			;0fc5	93		.
	dec de			;0fc6	1b		.
	jr z,l0fd1h		;0fc7	28 08		( .
	call sub_3811h		;0fc9	cd 11 38	. . 8
	jr c,l0fd8h		;0fcc	38 0a		8 .
	ex de,hl		;0fce	eb		.
	inc (hl)		;0fcf	34		4
	ex de,hl		;0fd0	eb		.
l0fd1h:
	ld a,(de)		;0fd1	1a		.
	sbc a,e			;0fd2	9b		.
	and 007h		;0fd3	e6 07		. .
	jp z,Filler001B_end	;0fd5	ca 1e 00	. . .
l0fd8h:
	push af			;0fd8	f5		.
	call sub_048ch		;0fd9	cd 8c 04	. . .
	ld iy,IyWorkBase	;0fdc	fd 21 df df	. ! . .
	ld hl,RegWindowDef	;0fe0	21 89 e0	! . .
	rst 30h			;0fe3	f7		.
	call SetErrorTables	;0fe4	cd 6a 11	. j .
	ld b,059h		;0fe7	06 59		. Y
	rst 8			;0fe9	cf		.
	pop af			;0fea	f1		.
	jr nc,l0ff6h		;0feb	30 09		0 .
	call l0ff6h		;0fed	cd f6 0f	. . .
	ld a,0a1h		;0ff0	3e a1		> .
	ld (0ffd0h),a		;0ff2	32 d0 ff	2 . .
	ret			;0ff5	c9		.
l0ff6h:
	xor a			;0ff6	af		.
	ld (0ffcah),a		;0ff7	32 ca ff	2 . .
	rst 18h			;0ffa	df		.
	call z,0d838h		;0ffb	cc 38 d8	. 8 .
	call sub_09e2h		;0ffe	cd e2 09	. . .
	ld b,006h		;1001	06 06		. .
	rst 8			;1003	cf		.
	ld hl,0e3f7h		;1004	21 f7 e3	! . .
	push hl			;1007	e5		.
	ld bc,l0105h		;1008	01 05 01	. . .
	ld de,l000dh+2		;100b	11 0f 00	. . .
	call sub_02d6h		;100e	cd d6 02	. . .
	pop hl			;1011	e1		.
	ret c			;1012	d8		.
	ld a,041h		;1013	3e 41		> A
	cpi			;1015	ed a1		. .
	ret nz			;1017	c0		.
	ld a,04dh		;1018	3e 4d		> M
	cpi			;101a	ed a1		. .
	ret nz			;101c	c0		.
	jp (hl)			;101d	e9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EnterTrdos - boot into TR-DOS: prepares state through the
; $1177 helper, selects service 2 and issues a RST 18h RAM-
; extension call that hands control to the DOS page of this
; bundle (page 3).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EnterTrdos:
	ld a,089h		;101e	3e 89		> .
	call sub_1177h		;1020	cd 77 11	. w .
l1023h:
	ld b,002h		;1023	06 02		. .
	rst 18h			;1025	df		.
	ld d,03ah		;1026	16 3a		. :
	jr c,$+22		;1028	38 14		8 .
	call CallTrdos		;102a	cd 57 35	. W 5
	ld hl,ZeroPad3F91_end	;102d	21 00 40	! . @
	ld bc,01b00h		;1030	01 00 1b	. . .
	call FillMemoryRange	;1033	cd 46 28	. F (
	jr c,$+8		;1036	38 06		8 .
	rst 18h			;1038	df		.
	and h			;1039	a4		.
	ld a,(0bed2h)		;103a	3a d2 be	: . .
	ld de,074cdh		;103d	11 cd 74	. . t
	inc (hl)		;1040	34		4
	jp l2fd2h		;1041	c3 d2 2f	. . /
l1044h:
	jp 0eb00h		;1044	c3 00 eb	. . .
	jp 0eb03h		;1047	c3 03 eb	. . .
	ld hl,0eb00h		;104a	21 00 eb	! . .
	jr l1052h		;104d	18 03		. .
	ld hl,0eb03h		;104f	21 03 eb	! . .
l1052h:
	ld a,(hl)		;1052	7e		~
	sub 0c3h		;1053	d6 c3		. .
	jr l1097h		;1055	18 40		. @
	jr EnterTrdos		;1057	18 c5		. .
	xor a			;1059	af		.
	ld (0e932h),a		;105a	32 32 e9	2 2 .
	jp l11beh		;105d	c3 be 11	. . .
	ld hl,WriteAnyBankByte	;1060	21 00 00	! . .
	ld (0e019h),hl		;1063	22 19 e0	" . .
	ret			;1066	c9		.
	scf			;1067	37		7
	ld hl,0dff4h		;1068	21 f4 df	! . .
	set 5,(hl)		;106b	cb ee		. .
	jr nc,l1071h		;106d	30 02		0 .
	res 5,(hl)		;106f	cb ae		. .
l1071h:
	ld hl,l11beh		;1071	21 be 11	! . .
	ex (sp),hl		;1074	e3		.
	jp l07d2h		;1075	c3 d2 07	. . .
	bit 1,(iy+012h)		;1078	fd cb 12 4e	. . . N
	ret z			;107c	c8		.
	scf			;107d	37		7
	ret			;107e	c9		.
	bit 0,(iy+015h)		;107f	fd cb 15 46	. . . F
	ret nz			;1083	c0		.
	scf			;1084	37		7
	ret			;1085	c9		.
	scf			;1086	37		7
	bit 4,(iy-002h)		;1087	fd cb fe 66	. . . f
	ret z			;108b	c8		.
	ld a,(iy-001h)		;108c	fd 7e ff	. ~ .
	and 013h		;108f	e6 13		. .
	ret z			;1091	c8		.
	scf			;1092	37		7
	ret			;1093	c9		.
	ld a,(0c063h)		;1094	3a 63 c0	: c .
l1097h:
	or a			;1097	b7		.
	ret z			;1098	c8		.
	scf			;1099	37		7
	ret			;109a	c9		.
	ld a,(0dff8h)		;109b	3a f8 df	: . .
	rlca			;109e	07		.
	ccf			;109f	3f		?
	ret			;10a0	c9		.
	jp l0a39h		;10a1	c3 39 0a	. 9 .
	ld a,001h		;10a4	3e 01		> .
	call sub_10bfh		;10a6	cd bf 10	. . .
	call sub_11b3h		;10a9	cd b3 11	. . .
	xor a			;10ac	af		.
	bit 2,(iy+012h)		;10ad	fd cb 12 56	. . . V
	jr z,l10b4h		;10b1	28 01		( .
	inc a			;10b3	3c		<
l10b4h:
	ld (hl),a		;10b4	77		w
	ret			;10b5	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg10B6: separator line of nine < characters
; (bit-7 terminator).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg10B6' (start 0x10b6 end 0x10c0)
Msg10B6_start:
	defb 03ch		;10b6	3c		<
	defb 03ch		;10b7	3c		<
	defb 03ch		;10b8	3c		<
	defb 03ch		;10b9	3c		<
	defb 03ch		;10ba	3c		<
	defb 03ch		;10bb	3c		<
	defb 03ch		;10bc	3c		<
	defb 03ch		;10bd	3c		<
	defb 03ch		;10be	3c		<
sub_10bfh:
	defb 0f5h		;10bf	f5		.
Msg10B6_end:
	ld hl,(0dffah)		;10c0	2a fa df	* . .
	ld a,(0ddffh)		;10c3	3a ff dd	: . .
	ld (hl),a		;10c6	77		w
	inc hl			;10c7	23		#
	ld (0dffah),hl		;10c8	22 fa df	" . .
	pop af			;10cb	f1		.
	ld (0ddffh),a		;10cc	32 ff dd	2 . .
l10cfh:
	jp l11beh		;10cf	c3 be 11	. . .
	ld de,0de08h		;10d2	11 08 de	. . .
	ld hl,(0dffah)		;10d5	2a fa df	* . .
	xor a			;10d8	af		.
	sbc hl,de		;10d9	ed 52		. R
	add hl,de		;10db	19		.
	jr z,l10e3h		;10dc	28 05		( .
	dec hl			;10de	2b		+
	ld (0dffah),hl		;10df	22 fa df	" . .
	ld a,(hl)		;10e2	7e		~
l10e3h:
	ld (0ddffh),a		;10e3	32 ff dd	2 . .
	jr l10cfh		;10e6	18 e7		. .
	jp l0119h		;10e8	c3 19 01	. . .
	jp l352dh		;10eb	c3 2d 35	. - 5
	ld hl,0ffcah		;10ee	21 ca ff	! . .
	ld bc,l0001h+1		;10f1	01 02 00	. . .
	jr l110bh		;10f4	18 15		. .
	ld hl,0ffcch		;10f6	21 cc ff	! . .
	jr l10feh		;10f9	18 03		. .
	ld hl,0ffd0h		;10fb	21 d0 ff	! . .
l10feh:
	ld bc,(0ffcah)		;10fe	ed 4b ca ff	. K . .
	ld b,000h		;1102	06 00		. .
	add hl,bc		;1104	09		.
	ld a,(hl)		;1105	7e		~
	and 0e0h		;1106	e6 e0		. .
	ld b,a			;1108	47		G
	ld c,004h		;1109	0e 04		. .
l110bh:
	ld a,(hl)		;110b	7e		~
	and 003h		;110c	e6 03		. .
	inc a			;110e	3c		<
	cp c			;110f	b9		.
	jr c,l1113h		;1110	38 01		8 .
	xor a			;1112	af		.
l1113h:
	or b			;1113	b0		.
	ld (hl),a		;1114	77		w
	ret			;1115	c9		.
	scf			;1116	37		7
	ld hl,0dff2h		;1117	21 f2 df	! . .
	rr (hl)			;111a	cb 1e		. .
	call sub_09deh		;111c	cd de 09	. . .
	ld a,00ch		;111f	3e 0c		> .
	or a			;1121	b7		.
	call sub_0a00h		;1122	cd 00 0a	. . .
	call SetErrorTables	;1125	cd 6a 11	. j .
	jp l32a5h		;1128	c3 a5 32	. . 2
	ld hl,l11beh		;112b	21 be 11	! . .
	ex (sp),hl		;112e	e3		.
	jp l316ch		;112f	c3 6c 31	. l 1
	ld a,0ffh		;1132	3e ff		> .
	ld (AyShadowReg),a	;1134	32 d7 df	2 . .
	call sub_1354h		;1137	cd 54 13	. T .
	ld a,(de)		;113a	1a		.
	xor (hl)		;113b	ae		.
	ld (de),a		;113c	12		.
	ret			;113d	c9		.
	call sub_1354h		;113e	cd 54 13	. T .
	ld c,(hl)		;1141	4e		N
	inc hl			;1142	23		#
	ld b,(hl)		;1143	46		F
	push de			;1144	d5		.
	push bc			;1145	c5		.
	ld a,087h		;1146	3e 87		> .
l1148h:
	call sub_1177h		;1148	cd 77 11	. w .
	jp c,l11beh		;114b	da be 11	. . .
	call 00d3eh		;114e	cd 3e 0d	. > .
	jr nz,l1148h		;1151	20 f5		  .
	ld a,d			;1153	7a		z
	or a			;1154	b7		.
	ld a,00ch		;1155	3e 0c		> .
	jr nz,l1148h		;1157	20 ef		  .
	pop bc			;1159	c1		.
	push bc			;115a	c5		.
	ld a,e			;115b	7b		{
	call CheckRangeInclusive	;115c	cd 9e 13	. . .
	ld a,00ch		;115f	3e 0c		> .
	jr c,l1148h		;1161	38 e5		8 .
	pop bc			;1163	c1		.
	ld a,e			;1164	7b		{
	pop de			;1165	d1		.
	ld (de),a		;1166	12		.
	jp l11beh		;1167	c3 be 11	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SetErrorTables: ErrorTextTable <- $3B7F (messages),
; ErrorTextTable2 <- $3818 (bit map).  RAM extensions can point
; these at custom error sets.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SetErrorTables:
	ld hl,WordLists_end	;116a	21 7f 3b	! . ;
	ld (ErrorTextTable),hl	;116d	22 d8 dd	" . .
	ld hl,KeywordTable_end	;1170	21 18 38	! . 8
	ld (ErrorTextTable2),hl	;1173	22 da dd	" . .
	ret			;1176	c9		.
sub_1177h:
	call sub_09efh		;1177	cd ef 09	. . .
	jr nc,l1183h		;117a	30 07		0 .
	cp 007h			;117c	fe 07		. .
	scf			;117e	37		7
	ret z			;117f	c8		.
	xor a			;1180	af		.
	jr sub_1177h		;1181	18 f4		. .
l1183h:
	call sub_0e29h		;1183	cd 29 0e	. ) .
	scf			;1186	37		7
	ccf			;1187	3f		?
	ret			;1188	c9		.
sub_1189h:
	ld hl,l3d6bh		;1189	21 6b 3d	! k =
	ld a,(0ddffh)		;118c	3a ff dd	: . .
	add a,a			;118f	87		.
	add a,l			;1190	85		.
	ld l,a			;1191	6f		o
	jr nc,l1195h		;1192	30 01		0 .
	inc h			;1194	24		$
l1195h:
	ld a,(hl)		;1195	7e		~
	inc hl			;1196	23		#
	ld h,(hl)		;1197	66		f
	ld l,a			;1198	6f		o
l1199h:
	ld a,(hl)		;1199	7e		~
	inc hl			;119a	23		#
	ret			;119b	c9		.
sub_119ch:
	call sub_11a9h		;119c	cd a9 11	. . .
sub_119fh:
	and 01fh		;119f	e6 1f		. .
	jr nz,l11a6h		;11a1	20 03		  .
	ld a,030h		;11a3	3e 30		> 0
	ret			;11a5	c9		.
l11a6h:
	or 040h			;11a6	f6 40		. @
	ret			;11a8	c9		.
sub_11a9h:
	ld a,c			;11a9	79		y
	add a,a			;11aa	87		.
	add a,a			;11ab	87		.
	add a,l			;11ac	85		.
	ld l,a			;11ad	6f		o
	jr nc,l1199h		;11ae	30 e9		0 .
	inc h			;11b0	24		$
	jr l1199h		;11b1	18 e6		. .
sub_11b3h:
	ld hl,0de00h		;11b3	21 00 de	! . .
	ld a,(0ddffh)		;11b6	3a ff dd	: . .
	add a,l			;11b9	85		.
	ld l,a			;11ba	6f		o
	ret nc			;11bb	d0		.
	inc h			;11bc	24		$
	ret			;11bd	c9		.
l11beh:
	ld sp,0e25eh		;11be	31 5e e2	1 ^ .
	call ReadPort7FFD	;11c1	cd 11 04	. . .
	call SetErrorTables	;11c4	cd 6a 11	. j .
	ld hl,RegWindowDef	;11c7	21 89 e0	! . .
	rst 30h			;11ca	f7		.
	ld a,(0e0a5h)		;11cb	3a a5 e0	: . .
	ld (ix+006h),a		;11ce	dd 77 06	. w .
	call sub_265bh		;11d1	cd 5b 26	. [ &
	ld hl,0e0bah		;11d4	21 ba e0	! . .
	call PrintJustifiedHeader	;11d7	cd 9c 18	. . .
	call RunCommand		;11da	cd bf 2f	. . /
	inc sp			;11dd	33		3
	jp pe,08921h		;11de	ea 21 89	. ! .
	ret po			;11e1	e0		.
	rst 30h			;11e2	f7		.
	ld a,(0e0a6h)		;11e3	3a a6 e0	: . .
	ld (ix+006h),a		;11e6	dd 77 06	. w .
	ld b,001h		;11e9	06 01		. .
	rst 8			;11eb	cf		.
	call sub_1189h		;11ec	cd 89 11	. . .
	ld b,(hl)		;11ef	46		F
	rst 8			;11f0	cf		.
	inc (ix+000h)		;11f1	dd 34 00	. 4 .
	ld c,000h		;11f4	0e 00		. .
	ld hl,011dfh		;11f6	21 df 11	! . .
	push hl			;11f9	e5		.
	ld a,(0e0a5h)		;11fa	3a a5 e0	: . .
	ld (ix+006h),a		;11fd	dd 77 06	. w .
l1200h:
	call sub_1189h		;1200	cd 89 11	. . .
	inc a			;1203	3c		<
	cp c			;1204	b9		.
	jp z,l12aeh		;1205	ca ae 12	. . .
	inc hl			;1208	23		#
	call sub_11a9h		;1209	cd a9 11	. . .
	ld b,a			;120c	47		G
	call sub_12feh		;120d	cd fe 12	. . .
l1210h:
	push hl			;1210	e5		.
	ld a,(0e0a7h)		;1211	3a a7 e0	: . .
	jr c,l1223h		;1214	38 0d		8 .
	ld a,b			;1216	78		x
	rlca			;1217	07		.
	rlca			;1218	07		.
	rlca			;1219	07		.
	and 007h		;121a	e6 07		. .
	ld l,a			;121c	6f		o
	ld a,(0e0a5h)		;121d	3a a5 e0	: . .
	and 0f8h		;1220	e6 f8		. .
	or l			;1222	b5		.
l1223h:
	ld (ix+006h),a		;1223	dd 77 06	. w .
	ld a,b			;1226	78		x
	call sub_119fh		;1227	cd 9f 11	. . .
	ld b,a			;122a	47		G
	call sub_11b3h		;122b	cd b3 11	. . .
	ld a,(hl)		;122e	7e		~
	cp c			;122f	b9		.
	jr nz,l1245h		;1230	20 13		  .
	ld a,(ix+006h)		;1232	dd 7e 06	. ~ .
	and 007h		;1235	e6 07		. .
	ld (ix+006h),a		;1237	dd 77 06	. w .
	ld a,(0e0a8h)		;123a	3a a8 e0	: . .
	and 038h		;123d	e6 38		. 8
	or (ix+006h)		;123f	dd b6 06	. . .
	ld (ix+006h),a		;1242	dd 77 06	. w .
l1245h:
	pop hl			;1245	e1		.
	call sub_130fh		;1246	cd 0f 13	. . .
	ld a,(hl)		;1249	7e		~
	push af			;124a	f5		.
	rlca			;124b	07		.
	rlca			;124c	07		.
	and 003h		;124d	e6 03		. .
	inc hl			;124f	23		#
	add a,(ix+000h)		;1250	dd 86 00	. . .
	ld (ix+000h),a		;1253	dd 77 00	. w .
	ld (ix+001h),009h	;1256	dd 36 01 09	. 6 . .
	rst 20h			;125a	e7		.
	and b			;125b	a0		.
	ld a,b			;125c	78		x
	rst 10h			;125d	d7		.
	rst 20h			;125e	e7		.
	ld l,0a0h		;125f	2e a0		. .
	push bc			;1261	c5		.
	push hl			;1262	e5		.
	ld b,(hl)		;1263	46		F
	rst 8			;1264	cf		.
	rst 20h			;1265	e7		.
	and b			;1266	a0		.
	pop hl			;1267	e1		.
	inc hl			;1268	23		#
	pop bc			;1269	c1		.
	pop af			;126a	f1		.
	and 03fh		;126b	e6 3f		. ?
	jr z,l12a2h		;126d	28 33		( 3
	push bc			;126f	c5		.
	call sub_1359h		;1270	cd 59 13	. Y .
	and 003h		;1273	e6 03		. .
	cp 001h			;1275	fe 01		. .
	jr nz,l1287h		;1277	20 0e		  .
	ld a,(de)		;1279	1a		.
	and (hl)		;127a	a6		.
	inc hl			;127b	23		#
	ld b,(hl)		;127c	46		F
	inc hl			;127d	23		#
	jr z,l1281h		;127e	28 01		( .
l1280h:
	ld b,(hl)		;1280	46		F
l1281h:
	rst 8			;1281	cf		.
l1282h:
	rst 20h			;1282	e7		.
	and b			;1283	a0		.
	pop bc			;1284	c1		.
	jr l12a2h		;1285	18 1b		. .
l1287h:
	cp 002h			;1287	fe 02		. .
	jr nz,l1297h		;1289	20 0c		  .
	ld a,(de)		;128b	1a		.
	ld c,a			;128c	4f		O
	ld b,000h		;128d	06 00		. .
	ld de,sub_0a00h		;128f	11 00 0a	. . .
	call sub_18ech		;1292	cd ec 18	. . .
	jr l1282h		;1295	18 eb		. .
l1297h:
	ld a,(de)		;1297	1a		.
	and 003h		;1298	e6 03		. .
	jr z,l1280h		;129a	28 e4		( .
	ld b,a			;129c	47		G
l129dh:
	inc hl			;129d	23		#
	djnz l129dh		;129e	10 fd		. .
	jr l1280h		;12a0	18 de		. .
l12a2h:
	ld a,024h		;12a2	3e 24		> $
	call sub_170ch		;12a4	cd 0c 17	. . .
	inc c			;12a7	0c		.
	inc (ix+000h)		;12a8	dd 34 00	. 4 .
	jp l1200h		;12ab	c3 00 12	. . .
l12aeh:
	call AutoRepeatKey	;12ae	cd 16 17	. . .
	push af			;12b1	f5		.
	ld a,(0e0a5h)		;12b2	3a a5 e0	: . .
	ld (ix+006h),a		;12b5	dd 77 06	. w .
	ld bc,010e1h		;12b8	01 e1 10	. . .
l12bbh:
	dec bc			;12bb	0b		.
	ld a,b			;12bc	78		x
	or c			;12bd	b1		.
	jr nz,l12bbh		;12be	20 fb		  .
	pop af			;12c0	f1		.
	call ScanTokens		;12c1	cd b5 13	. . .
	ld a,(bc)		;12c4	0a		.
	inc sp			;12c5	33		3
	inc de			;12c6	13		.
	dec bc			;12c7	0b		.
	ld b,h			;12c8	44		D
	inc de			;12c9	13		.
	dec c			;12ca	0d		.
	ld h,013h		;12cb	26 13		& .
	rlca			;12cd	07		.
	rla			;12ce	17		.
	inc de			;12cf	13		.
	rst 38h			;12d0	ff		.
	cp 07bh			;12d1	fe 7b		. {
	jr nc,l12dbh		;12d3	30 06		0 .
	cp 040h			;12d5	fe 40		. @
	jr c,l12dbh		;12d7	38 02		8 .
	and 05fh		;12d9	e6 5f		. _
l12dbh:
	ld b,a			;12db	47		G
	ld c,0ffh		;12dc	0e ff		. .
l12deh:
	inc c			;12de	0c		.
	call sub_1189h		;12df	cd 89 11	. . .
	inc a			;12e2	3c		<
	cp c			;12e3	b9		.
	ret z			;12e4	c8		.
	inc hl			;12e5	23		#
	call sub_119ch		;12e6	cd 9c 11	. . .
	cp b			;12e9	b8		.
	jr nz,l12deh		;12ea	20 f2		  .
	push hl			;12ec	e5		.
	call sub_11b3h		;12ed	cd b3 11	. . .
	ld (hl),c		;12f0	71		q
	pop hl			;12f1	e1		.
	call sub_12feh		;12f2	cd fe 12	. . .
	ret c			;12f5	d8		.
	push hl			;12f6	e5		.
	call sub_130fh		;12f7	cd 0f 13	. . .
	ld c,l			;12fa	4d		M
	ld b,h			;12fb	44		D
	pop hl			;12fc	e1		.
	inc hl			;12fd	23		#
sub_12feh:
	ld e,(hl)		;12fe	5e		^
	ld d,000h		;12ff	16 00		. .
	inc hl			;1301	23		#
	push hl			;1302	e5		.
	ld hl,l1044h		;1303	21 44 10	! D .
	add hl,de		;1306	19		.
	ld de,l130dh		;1307	11 0d 13	. . .
	push de			;130a	d5		.
	xor a			;130b	af		.
	jp (hl)			;130c	e9		.
l130dh:
	pop hl			;130d	e1		.
	ret			;130e	c9		.
sub_130fh:
	ld e,(hl)		;130f	5e		^
	ld d,000h		;1310	16 00		. .
	ld hl,l3d7fh		;1312	21 7f 3d	! . =
	add hl,de		;1315	19		.
	ret			;1316	c9		.
	ld hl,l11beh		;1317	21 be 11	! . .
	ex (sp),hl		;131a	e3		.
	xor a			;131b	af		.
	ld (0ddffh),a		;131c	32 ff dd	2 . .
	ld hl,0de08h		;131f	21 08 de	! . .
	ld (0dffah),hl		;1322	22 fa df	" . .
	ret			;1325	c9		.
	call sub_11b3h		;1326	cd b3 11	. . .
	ld c,(hl)		;1329	4e		N
	call sub_1189h		;132a	cd 89 11	. . .
	inc hl			;132d	23		#
	call sub_119ch		;132e	cd 9c 11	. . .
	jr l12dbh		;1331	18 a8		. .
	call sub_11b3h		;1333	cd b3 11	. . .
	push hl			;1336	e5		.
	ld c,(hl)		;1337	4e		N
	inc c			;1338	0c		.
	call sub_1189h		;1339	cd 89 11	. . .
	cp c			;133c	b9		.
	jr nc,l1341h		;133d	30 02		0 .
	ld c,000h		;133f	0e 00		. .
l1341h:
	pop hl			;1341	e1		.
	ld (hl),c		;1342	71		q
	ret			;1343	c9		.
	call sub_11b3h		;1344	cd b3 11	. . .
	ld a,(hl)		;1347	7e		~
	or a			;1348	b7		.
	jr z,l134dh		;1349	28 02		( .
	dec (hl)		;134b	35		5
	ret			;134c	c9		.
l134dh:
	push hl			;134d	e5		.
	call sub_1189h		;134e	cd 89 11	. . .
	pop hl			;1351	e1		.
	ld (hl),a		;1352	77		w
	ret			;1353	c9		.
sub_1354h:
	ld l,c			;1354	69		i
	ld h,b			;1355	60		`
	ld a,(bc)		;1356	0a		.
	inc hl			;1357	23		#
	inc hl			;1358	23		#
sub_1359h:
	bit 4,a			;1359	cb 67		. g
	jr z,l1371h		;135b	28 14		( .
	ld e,(hl)		;135d	5e		^
	inc hl			;135e	23		#
	ld d,(hl)		;135f	56		V
	inc hl			;1360	23		#
	ld a,(de)		;1361	1a		.
	and 003h		;1362	e6 03		. .
	inc a			;1364	3c		<
l1365h:
	dec a			;1365	3d		=
	ld b,(hl)		;1366	46		F
	inc hl			;1367	23		#
	jr z,EvalOperand	;1368	28 05		( .
l136ah:
	inc hl			;136a	23		#
	djnz l136ah		;136b	10 fd		. .
	jr l1365h		;136d	18 f6		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EvalOperand: evaluate one command/assembler operand, including
; the bank-extended indirect syntax (addresses may carry a bank
; qualifier routed through PeekPokeAnyBank).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EvalOperand:
	ld a,(hl)		;136f	7e		~
	inc hl			;1370	23		#
l1371h:
	ld e,(hl)		;1371	5e		^
	inc hl			;1372	23		#
	bit 2,a			;1373	cb 57		. W
	jr z,l137bh		;1375	28 04		( .
	ld d,(hl)		;1377	56		V
	inc hl			;1378	23		#
	jr l1382h		;1379	18 07		. .
l137bh:
	bit 7,e			;137b	cb 7b		. {
	ld d,000h		;137d	16 00		. .
	jr z,l1382h		;137f	28 01		( .
	dec d			;1381	15		.
l1382h:
	push iy			;1382	fd e5		. .
	ex (sp),hl		;1384	e3		.
	add hl,de		;1385	19		.
	ex de,hl		;1386	eb		.
	pop hl			;1387	e1		.
	bit 3,a			;1388	cb 5f		. _
	ret z			;138a	c8		.
	push af			;138b	f5		.
	ld b,(hl)		;138c	46		F
	inc hl			;138d	23		#
	ld a,(hl)		;138e	7e		~
	inc hl			;138f	23		#
	push hl			;1390	e5		.
	ld l,b			;1391	68		h
	ld h,a			;1392	67		g
	ld a,(hl)		;1393	7e		~
	and 003h		;1394	e6 03		. .
	ld l,a			;1396	6f		o
	ld h,000h		;1397	26 00		& .
	add hl,de		;1399	19		.
	ex de,hl		;139a	eb		.
	pop hl			;139b	e1		.
	pop af			;139c	f1		.
	ret			;139d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckRangeInclusive: carry set when A lies within C..B
; (cp c / ret c, then cp b / ccf).  Validating addresses and
; command indices.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckRangeInclusive:
	cp c			;139e	b9		.
	ret c			;139f	d8		.
	cp b			;13a0	b8		.
	ccf			;13a1	3f		?
	ret			;13a2	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SetDefaultWorkspace: HL <- $E05F (WorkBuffer), then falls into
; SetWorkspace.  Convenience entry before window operations.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SetDefaultWorkspace:
	ld hl,WorkBuffer	;13a3	21 5f e0	! _ .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SetWorkspace - RST 30h backend: (WorkBufferPtr) <- HL and
; IX <- HL.  Window routines address their descriptors
; IX-relative, so this is how the monitor switches windows.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SetWorkspace:
	ld (WorkBufferPtr),hl	;13a6	22 d4 e2	" . .
	push hl			;13a9	e5		.
	pop ix			;13aa	dd e1		. .
	ret			;13ac	c9		.
sub_13adh:
	rst 18h			;13ad	df		.
	adc a,d			;13ae	8a		.
	scf			;13af	37		7
	ret			;13b0	c9		.
l13b1h:
	rst 18h			;13b1	df		.
	ld a,036h		;13b2	3e 36		> 6
	ret			;13b4	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScanTokens: compressed table walk - each entry matches one
; character and holds a 2-byte skip to the next node; the
; terminator entry yields an action address executed via
; jp (HL).  The command tokenizer of the monitor.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScanTokens:
	pop hl			;13b5	e1		.
	jr l13bah		;13b6	18 02		. .
l13b8h:
	inc hl			;13b8	23		#
	inc hl			;13b9	23		#
l13bah:
	ld e,(hl)		;13ba	5e		^
	inc e			;13bb	1c		.
	jr z,l13c7h		;13bc	28 09		( .
	cp (hl)			;13be	be		.
	inc hl			;13bf	23		#
	jr nz,l13b8h		;13c0	20 f6		  .
	ld e,(hl)		;13c2	5e		^
	inc hl			;13c3	23		#
	ld d,(hl)		;13c4	56		V
	dec de			;13c5	1b		.
	ex de,hl		;13c6	eb		.
l13c7h:
	inc hl			;13c7	23		#
	jp (hl)			;13c8	e9		.
	rst 20h			;13c9	e7		.
	add a,a			;13ca	87		.
	ret			;13cb	c9		.
sub_13cch:
	call BuildStepStub	;13cc	cd b3 1a	. . .
	call FindWatchpoint	;13cf	cd e8 29	. . )
	ld e,0ffh		;13d2	1e ff		. .
	jp nc,l0175h		;13d4	d2 75 01	. u .
l13d7h:
	ld a,(0dff5h)		;13d7	3a f5 df	: . .
	and 07fh		;13da	e6 7f		. .
	bit 2,a			;13dc	cb 57		. W
	ret nz			;13de	c0		.
	ld (0dfe9h),a		;13df	32 e9 df	2 . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CommandLoop - the heart of the monitor: print the prompt in
; the E035 window, read a line through the editor, parse and
; execute it via the command table (RunCommand $2FBF), repeat.
; Errors funnel back here through ExitToError.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CommandLoop:
	call sub_049bh		;13e2	cd 9b 04	. . .
	ld hl,PromptWindowDef	;13e5	21 35 e0	! 5 .
	rst 30h			;13e8	f7		.
	bit 0,(iy+00ah)		;13e9	fd cb 0a 46	. . . F
	jr nz,$+55		;13ed	20 35		  5
	call sub_265bh		;13ef	cd 5b 26	. [ &
	rst 20h			;13f2	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegHeaderLine: header row of the register window
; ('IR  SZ-H-PNC  INT RAM ROM SCR  ZX' with
; embedded control bytes for the column gaps);
; printed above the dump by ShowRegisters ($14AB).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'RegHeaderLine' (start 0x13f3 end 0x1417)
RegHeaderLine_start:
	defb 049h		;13f3	49		I
	defb 052h		;13f4	52		R
	defb 01bh		;13f5	1b		.
	defb 008h		;13f6	08		.
	defb 00ah		;13f7	0a		.
	defb 053h		;13f8	53		S
	defb 05ah		;13f9	5a		Z
	defb 02dh		;13fa	2d		-
	defb 048h		;13fb	48		H
	defb 02dh		;13fc	2d		-
	defb 050h		;13fd	50		P
	defb 04eh		;13fe	4e		N
	defb 043h		;13ff	43		C
	defb 00dh		;1400	0d		.
	defb 00dh		;1401	0d		.
	defb 049h		;1402	49		I
	defb 04eh		;1403	4e		N
	defb 054h		;1404	54		T
	defb 020h		;1405	20		 
	defb 052h		;1406	52		R
	defb 041h		;1407	41		A
	defb 04dh		;1408	4d		M
	defb 020h		;1409	20		 
	defb 052h		;140a	52		R
	defb 04fh		;140b	4f		O
	defb 04dh		;140c	4d		M
	defb 020h		;140d	20		 
	defb 053h		;140e	53		S
	defb 043h		;140f	43		C
	defb 052h		;1410	52		R
	defb 01bh		;1411	1b		.
l1412h:
	defb 000h		;1412	00		.
	defb 00bh		;1413	0b		.
	defb 05ah		;1414	5a		Z
	defb 058h		;1415	58		X
	defb 0a0h		;1416	a0		.
RegHeaderLine_end:
	bit 5,(iy-002h)		;1417	fd cb fe 6e	. . . n
	jr z,l1421h		;141b	28 04		( .
	rst 20h			;141d	e7		.
	or h			;141e	b4		.
	jr $+5			;141f	18 03		. .
l1421h:
	rst 20h			;1421	e7		.
	ld sp,0e7b2h		;1422	31 b2 e7	1 . .
	jr c,l1412h		;1425	38 eb		8 .
	ld (ix+000h),000h	;1427	dd 36 00 00	. 6 . .
	ld (ix+001h),004h	;142b	dd 36 01 04	. 6 . .
	ld hl,(0dd84h)		;142f	2a 84 dd	* . .
	call PrintHexWord	;1432	cd 9e 19	. . .
	inc (ix+000h)		;1435	dd 34 00	. 4 .
	ld (ix+001h),000h	;1438	dd 36 01 00	. 6 . .
	call ShowRegisters	;143c	cd ab 14	. . .
	ld (ix+000h),009h	;143f	dd 36 00 09	. 6 . .
	call RegisterNameTable	;1443	cd 31 15	. 1 .
	ld hl,0dfe9h		;1446	21 e9 df	! . .
	set 1,(hl)		;1449	cb ce		. .
	set 2,(hl)		;144b	cb d6		. .
	ld (ix+000h),00bh	;144d	dd 36 00 0b	. 6 . .
	ld hl,l0edfh		;1451	21 df 0e	! . .
	ld a,(UserIff)		;1454	3a 83 dd	: . .
	and 004h		;1457	e6 04		. .
	jr nz,l145eh		;1459	20 03		  .
	ld hl,l0ee1h		;145b	21 e1 0e	! . .
l145eh:
	call PrintBit7Chars	;145e	cd 93 18	. . .
	call PrintSpace		;1461	cd bc 26	. . &
	ld (ix+001h),005h	;1464	dd 36 01 05	. 6 . .
	ld a,(PagingState)	;1468	3a dd df	: . .
	and 007h		;146b	e6 07		. .
	ld c,a			;146d	4f		O
	ld a,(0dfdeh)		;146e	3a de df	: . .
	rrca			;1471	0f		.
	and 008h		;1472	e6 08		. .
	or c			;1474	b1		.
	call sub_19ach		;1475	cd ac 19	. . .
	ld (ix+001h),009h	;1478	dd 36 01 09	. 6 . .
	ld a,(PagingState)	;147c	3a dd df	: . .
	rrca			;147f	0f		.
	rrca			;1480	0f		.
	rrca			;1481	0f		.
	rrca			;1482	0f		.
	and 001h		;1483	e6 01		. .
	call sub_19ach		;1485	cd ac 19	. . .
	ld (ix+001h),00dh	;1488	dd 36 01 0d	. 6 . .
	ld c,035h		;148c	0e 35		. 5
	ld a,(PagingState)	;148e	3a dd df	: . .
	and 008h		;1491	e6 08		. .
	jr z,l1497h		;1493	28 02		( .
	ld c,037h		;1495	0e 37		. 7
l1497h:
	ld a,c			;1497	79		y
	rst 10h			;1498	d7		.
	ld (iy+006h),004h	;1499	fd 36 06 04	. 6 . .
	call sub_1573h		;149d	cd 73 15	. s .
	call sub_1659h		;14a0	cd 59 16	. Y .
	ld hl,0dfe9h		;14a3	21 e9 df	! . .
	ld a,(hl)		;14a6	7e		~
	or 051h			;14a7	f6 51		. Q
	ld (hl),a		;14a9	77		w
	ret			;14aa	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ShowRegisters: dump the saved context - register names are
; inline RST 20h strings at RegisterNameTable ($1531), values
; printed in the current number base; the flag byte comes out
; as eight binary digits.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ShowRegisters:
	bit 1,(iy+00ah)		;14ab	fd cb 0a 4e	. . . N
	ret nz			;14af	c0		.
	ld hl,l0ed0h		;14b0	21 d0 0e	! . .
	ld c,(iy+00ch)		;14b3	fd 4e 0c	. N .
	ld b,007h		;14b6	06 07		. .
	ld de,UserPc		;14b8	11 6b dd	. k .
	jr l14c0h		;14bb	18 03		. .
l14bdh:
	inc (ix+000h)		;14bd	dd 34 00	. 4 .
l14c0h:
	ld a,003h		;14c0	3e 03		> .
	cp b			;14c2	b8		.
	jr nz,l14ceh		;14c3	20 09		  .
	bit 0,(iy+00bh)		;14c5	fd cb 0b 46	. . . F
	jr z,l14ceh		;14c9	28 03		( .
	ld de,0dd79h		;14cb	11 79 dd	. y .
l14ceh:
	sla c			;14ce	cb 21		. !
	jr nc,l14d6h		;14d0	30 04		0 .
	inc hl			;14d2	23		#
	inc hl			;14d3	23		#
	jr l151ah		;14d4	18 44		. D
l14d6h:
	bit 2,(iy+00ah)		;14d6	fd cb 0a 56	. . . V
	jr nz,l14e9h		;14da	20 0d		  .
	call PrintBit7Chars	;14dc	cd 93 18	. . .
	ld a,003h		;14df	3e 03		> .
	cp b			;14e1	b8		.
	jr c,l14ebh		;14e2	38 07		8 .
	call sub_1523h		;14e4	cd 23 15	. # .
	jr l14ebh		;14e7	18 02		. .
l14e9h:
	inc hl			;14e9	23		#
	inc hl			;14ea	23		#
l14ebh:
	ld (ix+001h),004h	;14eb	dd 36 01 04	. 6 . .
	push hl			;14ef	e5		.
	push de			;14f0	d5		.
	ex de,hl		;14f1	eb		.
	ld a,(hl)		;14f2	7e		~
	inc hl			;14f3	23		#
	ld h,(hl)		;14f4	66		f
	ld l,a			;14f5	6f		o
	call PrintHexWord	;14f6	cd 9e 19	. . .
	bit 3,(iy+00ah)		;14f9	fd cb 0a 5e	. . . ^
	jr nz,l1518h		;14fd	20 19		  .
	push bc			;14ff	c5		.
	ld bc,l0003h+1		;1500	01 04 00	. . .
	ld de,0ddach		;1503	11 ac dd	. . .
	push de			;1506	d5		.
	call CopyAcrossBanks	;1507	cd 46 06	. F .
	pop hl			;150a	e1		.
	ld b,004h		;150b	06 04		. .
l150dh:
	inc (ix+001h)		;150d	dd 34 01	. 4 .
	ld a,(hl)		;1510	7e		~
	call PrintHexByte	;1511	cd a3 19	. . .
	inc hl			;1514	23		#
	djnz l150dh		;1515	10 f6		. .
	pop bc			;1517	c1		.
l1518h:
	pop de			;1518	d1		.
	pop hl			;1519	e1		.
l151ah:
	inc de			;151a	13		.
	inc de			;151b	13		.
	ld (ix+001h),000h	;151c	dd 36 01 00	. 6 . .
	djnz l14bdh		;1520	10 9b		. .
	ret			;1522	c9		.
sub_1523h:
	bit 0,(iy+00bh)		;1523	fd cb 0b 46	. . . F
sub_1527h:
	ld a,020h		;1527	3e 20		>  
	jp z,PrintChar		;1529	ca be 26	. . &
	ld a,027h		;152c	3e 27		> '
	jp PrintChar		;152e	c3 be 26	. . &
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegisterNameTable: inline register-name strings (bit-7
; terminated, printed by RST 20h) for the dump above.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RegisterNameTable:
	ld b,(iy+00ah)		;1531	fd 46 0a	. F .
	bit 1,b			;1534	cb 48		. H
	ret nz			;1536	c0		.
	bit 0,(iy+00ch)		;1537	fd cb 0c 46	. . . F
	ret nz			;153b	c0		.
	bit 2,b			;153c	cb 50		. P
	ld b,(iy+00bh)		;153e	fd 46 0b	. F .
	jr nz,l154bh		;1541	20 08		  .
	rst 20h			;1543	e7		.
	ld b,c			;1544	41		A
	add a,0cbh		;1545	c6 cb		. .
	ld c,b			;1547	48		H
	call sub_1527h		;1548	cd 27 15	. ' .
l154bh:
	ld (ix+001h),004h	;154b	dd 36 01 04	. 6 . .
	bit 1,b			;154f	cb 48		. H
	ld hl,(0dd7fh)		;1551	2a 7f dd	* . .
	jr z,l1559h		;1554	28 03		( .
	ld hl,(0dd81h)		;1556	2a 81 dd	* . .
l1559h:
	call PrintHexWord	;1559	cd 9e 19	. . .
	ld (ix+001h),00ah	;155c	dd 36 01 0a	. 6 . .
sub_1560h:
	ld b,008h		;1560	06 08		. .
l1562h:
	ld a,030h		;1562	3e 30		> 0
	sla l			;1564	cb 25		. %
	jr nc,l1569h		;1566	30 01		0 .
	inc a			;1568	3c		<
l1569h:
	call PrintChar		;1569	cd be 26	. . &
	djnz l1562h		;156c	10 f4		. .
	ld (ix+001h),000h	;156e	dd 36 01 00	. 6 . .
	ret			;1572	c9		.
sub_1573h:
	bit 4,(iy+00ah)		;1573	fd cb 0a 66	. . . f
	ret nz			;1577	c0		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MemoryDumpWindow - the D-command view: default work buffer,
; scroll check, prompt-window cursor placement, then eight rows
; of HexDumpRow; the ASCII half re-fetches every byte across
; banks.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MemoryDumpWindow:
	call SetDefaultWorkspace	;1578	cd a3 13	. . .
	call ScrollWindow	;157b	cd e7 15	. . .
	bit 5,(iy+00ah)		;157e	fd cb 0a 6e	. . . n
	jr nz,l1594h		;1582	20 10		  .
	ld hl,PromptWindowDef	;1584	21 35 e0	! 5 .
	rst 30h			;1587	f7		.
	ld (ix+000h),00bh	;1588	dd 36 00 0b	. 6 . .
	ld (ix+001h),015h	;158c	dd 36 01 15	. 6 . .
	ex de,hl		;1590	eb		.
	call DisasmLine		;1591	cd 9b 16	. . .
l1594h:
	call SetDefaultWorkspace	;1594	cd a3 13	. . .
	call WindowHome		;1597	cd 46 27	. F '
	ld hl,(0dd87h)		;159a	2a 87 dd	* . .
	ld b,008h		;159d	06 08		. .
l159fh:
	push bc			;159f	c5		.
	call HexDumpRow		;15a0	cd 04 16	. . .
	inc (ix+000h)		;15a3	dd 34 00	. 4 .
	pop bc			;15a6	c1		.
	djnz l159fh		;15a7	10 f6		. .
	ld bc,l3e3ch		;15a9	01 3c 3e	. < >
	ld hl,(0dd69h)		;15ac	2a 69 dd	* i .
l15afh:
	call PositionCursor	;15af	cd c8 15	. . .
	ld a,b			;15b2	78		x
	call PrintChar		;15b3	cd be 26	. . &
	inc (ix+001h)		;15b6	dd 34 01	. 4 .
	inc (ix+001h)		;15b9	dd 34 01	. 4 .
	ld a,c			;15bc	79		y
	jp PrintChar		;15bd	c3 be 26	. . &
sub_15c0h:
	ld bc,l2020h		;15c0	01 20 20	.    
	ld hl,(0dd89h)		;15c3	2a 89 dd	* . .
	jr l15afh		;15c6	18 e7		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PositionCursor: translate the dump address (DD87/DD89) into
; window cursor coordinates (IX+0/IX+1).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PositionCursor:
	ld (0dd89h),hl		;15c8	22 89 dd	" . .
	ld de,(0dd87h)		;15cb	ed 5b 87 dd	. [ . .
	or a			;15cf	b7		.
	sbc hl,de		;15d0	ed 52		. R
	ld a,l			;15d2	7d		}
	and 007h		;15d3	e6 07		. .
	ld h,a			;15d5	67		g
	add a,a			;15d6	87		.
	add a,h			;15d7	84		.
	add a,007h		;15d8	c6 07		. .
	ld (ix+001h),a		;15da	dd 77 01	. w .
	ld a,l			;15dd	7d		}
	and 038h		;15de	e6 38		. 8
	rra			;15e0	1f		.
	rra			;15e1	1f		.
	rra			;15e2	1f		.
	ld (ix+000h),a		;15e3	dd 77 00	. w .
	ret			;15e6	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScrollWindow: scroll the current window when the cursor is
; about to leave the bottom edge.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScrollWindow:
	call sub_15c0h		;15e7	cd c0 15	. . .
	ld hl,(0dd87h)		;15ea	2a 87 dd	* . .
	ld de,(0dd69h)		;15ed	ed 5b 69 dd	. [ i .
	ld bc,0003fh		;15f1	01 3f 00	. ? .
	or a			;15f4	b7		.
	sbc hl,de		;15f5	ed 52		. R
	ret z			;15f7	c8		.
	jr nc,l15fch		;15f8	30 02		0 .
	add hl,bc		;15fa	09		.
	ret c			;15fb	d8		.
l15fch:
	ld hl,0ffe8h		;15fc	21 e8 ff	! . .
	add hl,de		;15ff	19		.
	ld (0dd87h),hl		;1600	22 87 dd	" . .
	ret			;1603	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; HexDumpRow: one dump row - address word, 8 bytes in hex
; (fetched through the bank window), then the same 8 as ASCII.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
HexDumpRow:
	ld (ix+001h),002h	;1604	dd 36 01 02	. 6 . .
l1608h:
	call PrintHexWord	;1608	cd 9e 19	. . .
	call sub_164bh		;160b	cd 4b 16	. K .
	call sub_164bh		;160e	cd 4b 16	. K .
	ld de,0ddach		;1611	11 ac dd	. . .
	ld bc,Rst08Vector	;1614	01 08 00	. . .
	push de			;1617	d5		.
	call CopyAcrossBanks	;1618	cd 46 06	. F .
	pop de			;161b	d1		.
	push de			;161c	d5		.
	ld b,008h		;161d	06 08		. .
	call sub_1630h		;161f	cd 30 16	. 0 .
	pop de			;1622	d1		.
	call sub_164bh		;1623	cd 4b 16	. K .
	ld b,008h		;1626	06 08		. .
	call sub_163bh		;1628	cd 3b 16	. ; .
	ld (ix+001h),000h	;162b	dd 36 01 00	. 6 . .
	ret			;162f	c9		.
sub_1630h:
	ld a,(de)		;1630	1a		.
	inc de			;1631	13		.
	call PrintHexByte	;1632	cd a3 19	. . .
	call sub_164bh		;1635	cd 4b 16	. K .
	djnz sub_1630h		;1638	10 f6		. .
	ret			;163a	c9		.
sub_163bh:
	ld a,(de)		;163b	1a		.
	inc de			;163c	13		.
	and 07fh		;163d	e6 7f		. .
	cp 020h			;163f	fe 20		.  
	jr nc,l1645h		;1641	30 02		0 .
	ld a,02eh		;1643	3e 2e		> .
l1645h:
	call PrintChar		;1645	cd be 26	. . &
	djnz sub_163bh		;1648	10 f1		. .
	ret			;164a	c9		.
sub_164bh:
	inc (ix+001h)		;164b	dd 34 01	. 4 .
	bit 4,(iy+009h)		;164e	fd cb 09 66	. . . f
	ret z			;1652	c8		.
	dec (ix+001h)		;1653	dd 35 01	. 5 .
	jp PrintSpace		;1656	c3 bc 26	. . &
sub_1659h:
	ld b,(iy+00ah)		;1659	fd 46 0a	. F .
	bit 6,b			;165c	cb 70		. p
	ret nz			;165e	c0		.
	bit 0,b			;165f	cb 40		. @
	jr nz,l1670h		;1661	20 0d		  .
	ld hl,05830h		;1663	21 30 58	! 0 X
	ld (hl),030h		;1666	36 30		6 0
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DisasmWindow: the disassembly view.  Attributes through the
; $5831 copy, workspace E051; renders 5 lines (1 while
; stepping) from DD87/DD89, advancing by each decoded length.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DisasmWindow:
	ld bc,l000dh+2		;1668	01 0f 00	. . .
	ld de,05831h		;166b	11 31 58	. 1 X
	ldir			;166e	ed b0		. .
l1670h:
	ld hl,DisasmWindowDef	;1670	21 51 e0	! Q .
	rst 30h			;1673	f7		.
	bit 0,(iy+00ah)		;1674	fd cb 0a 46	. . . F
	call nz,ScrollUp	;1678	c4 8b 25	. . %
	ld (ix+000h),001h	;167b	dd 36 00 01	. 6 . .
	ld (ix+001h),000h	;167f	dd 36 01 00	. 6 . .
	ld hl,(UserPc)		;1683	2a 6b dd	* k .
	ld b,005h		;1686	06 05		. .
	bit 2,(iy+005h)		;1688	fd cb 05 56	. . . V
	jr z,l1690h		;168c	28 02		( .
	ld b,001h		;168e	06 01		. .
l1690h:
	push bc			;1690	c5		.
	call DisasmLine		;1691	cd 9b 16	. . .
	inc (ix+000h)		;1694	dd 34 00	. 4 .
	pop bc			;1697	c1		.
	djnz l1690h		;1698	10 f6		. .
	ret			;169a	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DisasmLine: disassemble one instruction and print it
; (address, opcode bytes, mnemonic via Disassemble).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DisasmLine:
	call PrintHexWord	;169b	cd 9e 19	. . .
	call FetchPrefixBytes	;169e	cd b1 16	. . .
	push hl			;16a1	e5		.
	ld hl,0ddb7h		;16a2	21 b7 dd	! . .
	call Disassemble	;16a5	cd 98 17	. . .
	call PadColumns		;16a8	cd 08 17	. . .
	pop hl			;16ab	e1		.
	ld (ix+001h),000h	;16ac	dd 36 01 00	. 6 . .
	ret			;16b0	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FetchPrefixBytes: pull up to 4 opcode bytes for the
; disassembler across the bank window (DD/DDCB/FD/FDCB
; prefixes included).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FetchPrefixBytes:
	ld (0ddaah),hl		;16b1	22 aa dd	" . .
	push hl			;16b4	e5		.
	ld de,0ddach		;16b5	11 ac dd	. . .
	ld bc,l0003h+1		;16b8	01 04 00	. . .
	call CopyAcrossBanks	;16bb	cd 46 06	. F .
	call AnalyzeDataPool_end	;16be	cd 52 1c	. R .
	pop hl			;16c1	e1		.
	add hl,bc		;16c2	09		.
	ld (0ddaah),hl		;16c3	22 aa dd	" . .
	ret			;16c6	c9		.
sub_16c7h:
	bit 0,(iy+005h)		;16c7	fd cb 05 46	. . . F
	push af			;16cb	f5		.
	call z,PrintHexWord	;16cc	cc 9e 19	. . .
	pop af			;16cf	f1		.
	ld c,l			;16d0	4d		M
	ld b,h			;16d1	44		D
	ld (iy+008h),020h	;16d2	fd 36 08 20	. 6 .  
	call nz,sub_1929h	;16d6	c4 29 19	. ) .
	call FetchPrefixBytes	;16d9	cd b1 16	. . .
	push hl			;16dc	e5		.
	push bc			;16dd	c5		.
	ld hl,0ddb7h		;16de	21 b7 dd	! . .
	call Disassemble	;16e1	cd 98 17	. . .
	pop bc			;16e4	c1		.
	bit 1,(iy+005h)		;16e5	fd cb 05 4e	. . . N
	jr z,l1706h		;16e9	28 1b		( .
	ld a,017h		;16eb	3e 17		> .
	call sub_170ch		;16ed	cd 0c 17	. . .
	ld a,03bh		;16f0	3e 3b		> ;
	rst 10h			;16f2	d7		.
	ld b,c			;16f3	41		A
	push bc			;16f4	c5		.
	ld de,0ddach		;16f5	11 ac dd	. . .
	push de			;16f8	d5		.
	call sub_1630h		;16f9	cd 30 16	. 0 .
	ld a,025h		;16fc	3e 25		> %
	call sub_170ch		;16fe	cd 0c 17	. . .
	pop de			;1701	d1		.
	pop bc			;1702	c1		.
	call sub_163bh		;1703	cd 3b 16	. ; .
l1706h:
	pop hl			;1706	e1		.
	ret			;1707	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PadColumns: pad the listing to its fixed column layout.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PadColumns:
	ld a,(ix+00bh)		;1708	dd 7e 0b	. ~ .
	dec a			;170b	3d		=
sub_170ch:
	ld b,(ix+001h)		;170c	dd 46 01	. F .
	sub b			;170f	90		.
	ret z			;1710	c8		.
	ret c			;1711	d8		.
	ld b,a			;1712	47		G
	jp l18b4h		;1713	c3 b4 18	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AutoRepeatKey: keys held down (state at KeyState, E000-E004)
; repeat with a ramp-up delay; feeds editor and dump windows.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AutoRepeatKey:
	call BlinkCursor	;1716	cd ef 24	. . $
	call sub_07aah		;1719	cd aa 07	. . .
	call sub_173bh		;171c	cd 3b 17	. ; .
	push bc			;171f	c5		.
	ld bc,(KeyState)	;1720	ed 4b 00 e0	. K . .
l1724h:
	ld b,000h		;1724	06 00		. .
l1726h:
	djnz l1726h		;1726	10 fe		. .
	dec c			;1728	0d		.
	jr nz,l1724h		;1729	20 f9		  .
	pop bc			;172b	c1		.
	jr nc,AutoRepeatKey	;172c	30 e8		0 .
	cp 0c5h			;172e	fe c5		. .
	jr nz,l1734h		;1730	20 02		  .
	ld a,05dh		;1732	3e 5d		> ]
l1734h:
	cp 0c6h			;1734	fe c6		. .
	jr nz,l173ah		;1736	20 02		  .
	ld a,05bh		;1738	3e 5b		> [
l173ah:
	ret			;173a	c9		.
sub_173bh:
	jr c,l1742h		;173b	38 05		8 .
l173dh:
	xor a			;173d	af		.
	ld (0e004h),a		;173e	32 04 e0	2 . .
	ret			;1741	c9		.
l1742h:
	push af			;1742	f5		.
	call sub_07aah		;1743	cd aa 07	. . .
	pop hl			;1746	e1		.
	cp h			;1747	bc		.
	jr nz,l173dh		;1748	20 f3		  .
	ld hl,0e004h		;174a	21 04 e0	! . .
	cp (hl)			;174d	be		.
	jr z,l175ch		;174e	28 0c		( .
	ld (hl),a		;1750	77		w
	ld a,(0e001h)		;1751	3a 01 e0	: . .
l1754h:
	ld (0e003h),a		;1754	32 03 e0	2 . .
	ld a,(0e004h)		;1757	3a 04 e0	: . .
	scf			;175a	37		7
	ret			;175b	c9		.
l175ch:
	ld hl,0e003h		;175c	21 03 e0	! . .
	dec (hl)		;175f	35		5
	ret nz			;1760	c0		.
	ld a,(0e002h)		;1761	3a 02 e0	: . .
	jr l1754h		;1764	18 ee		. .
	xor a			;1766	af		.
	jr l176bh		;1767	18 02		. .
	ld a,080h		;1769	3e 80		> .
l176bh:
	bit 1,(iy+012h)		;176b	fd cb 12 4e	. . . N
	jr z,l1773h		;176f	28 02		( .
	or 001h			;1771	f6 01		. .
l1773h:
	ld (0dff5h),a		;1773	32 f5 df	2 . .
	call sub_13cch		;1776	cd cc 13	. . .
	ld a,081h		;1779	3e 81		> .
	ret			;177b	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CharClasses: character-class predicates (hex digit,
; separator, ...) used by parsers and the disassembler.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CharClasses:
	cp 07eh			;177c	fe 7e		. ~
	ret c			;177e	d8		.
	cp 0c4h			;177f	fe c4		. .
	ccf			;1781	3f		?
	ret			;1782	c9		.
sub_1783h:
	cp 02ch			;1783	fe 2c		. ,
	ret c			;1785	d8		.
	cp 081h			;1786	fe 81		. .
	ccf			;1788	3f		?
	ret			;1789	c9		.
sub_178ah:
	cp 02ch			;178a	fe 2c		. ,
	ret c			;178c	d8		.
	cp 0c4h			;178d	fe c4		. .
	ccf			;178f	3f		?
	ret			;1790	c9		.
sub_1791h:
	cp 001h			;1791	fe 01		. .
	ret c			;1793	d8		.
	cp 02ch			;1794	fe 2c		. ,
	ccf			;1796	3f		?
	ret			;1797	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Disassemble - the mnemonic printer core.  Mnemonic strings
; sit in MnemonicTable ($2012) with high-bit placeholder
; characters marking where operands splice in; this walk
; decodes operands (16-bit, 8-bit, indexed, condition codes)
; and emits the line via RST 10h.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Disassemble:
	res 6,(iy+00eh)		;1798	fd cb 0e b6	. . . .
	call PrintSpace		;179c	cd bc 26	. . &
	ld a,(hl)		;179f	7e		~
	call CharClasses	;17a0	cd 7c 17	. | .
	jr c,l17beh		;17a3	38 19		8 .
	push hl			;17a5	e5		.
	sub 07eh		;17a6	d6 7e		. ~
	add a,a			;17a8	87		.
	ld hl,MnemonicTable	;17a9	21 12 20	! .  
	ld e,a			;17ac	5f		_
	ld d,000h		;17ad	16 00		. .
	add hl,de		;17af	19		.
	ld a,(hl)		;17b0	7e		~
	call sub_17f6h		;17b1	cd f6 17	. . .
	ld a,(hl)		;17b4	7e		~
	and 07fh		;17b5	e6 7f		. .
	pop hl			;17b7	e1		.
	inc hl			;17b8	23		#
	call sub_1805h		;17b9	cd 05 18	. . .
	jr l17c9h		;17bc	18 0b		. .
l17beh:
	call sub_1783h		;17be	cd 83 17	. . .
	jr c,l17ceh		;17c1	38 0b		8 .
	call sub_17f6h		;17c3	cd f6 17	. . .
l17c6h:
	call sub_181fh		;17c6	cd 1f 18	. . .
l17c9h:
	ld a,(hl)		;17c9	7e		~
	call sub_178ah		;17ca	cd 8a 17	. . .
	ret nc			;17cd	d0		.
l17ceh:
	cp 0f1h			;17ce	fe f1		. .
	ret z			;17d0	c8		.
	cp 0f7h			;17d1	fe f7		. .
	ret z			;17d3	c8		.
	cp 0eeh			;17d4	fe ee		. .
	jr nz,l17e7h		;17d6	20 0f		  .
	call PrintSpace		;17d8	cd bc 26	. . &
	ld a,03bh		;17db	3e 3b		> ;
	rst 10h			;17dd	d7		.
	inc hl			;17de	23		#
l17dfh:
	ld a,(hl)		;17df	7e		~
	inc hl			;17e0	23		#
	cp 0f0h			;17e1	fe f0		. .
	ret z			;17e3	c8		.
	rst 10h			;17e4	d7		.
	jr l17dfh		;17e5	18 f8		. .
l17e7h:
	bit 6,(iy+00eh)		;17e7	fd cb 0e 76	. . . v
	set 6,(iy+00eh)		;17eb	fd cb 0e f6	. . . .
	jr nz,l17c6h		;17ef	20 d5		  .
	ld a,02ch		;17f1	3e 2c		> ,
	rst 10h			;17f3	d7		.
	jr l17c6h		;17f4	18 d0		. .
sub_17f6h:
	ld c,(iy+006h)		;17f6	fd 4e 06	. N .
	sub 02ch		;17f9	d6 2c		. ,
	ld de,l2100h		;17fb	11 00 21	. . !
	call sub_18bdh		;17fe	cd bd 18	. . .
	inc hl			;1801	23		#
	jp PrintSpace		;1802	c3 bc 26	. . &
sub_1805h:
	call sub_1791h		;1805	cd 91 17	. . .
	jr c,l1823h		;1808	38 19		8 .
	push af			;180a	f5		.
	sub 001h		;180b	d6 01		. .
	ld de,l209fh		;180d	11 9f 20	. .  
	call sub_18bbh		;1810	cd bb 18	. . .
	pop af			;1813	f1		.
	cp 027h			;1814	fe 27		. '
	jr c,l185dh		;1816	38 45		8 E
	call sub_181fh		;1818	cd 1f 18	. . .
	ld a,029h		;181b	3e 29		> )
	rst 10h			;181d	d7		.
	ret			;181e	c9		.
sub_181fh:
	ld a,(hl)		;181f	7e		~
	inc hl			;1820	23		#
	jr sub_1805h		;1821	18 e2		. .
l1823h:
	cp 0f8h			;1823	fe f8		. .
	jr c,l182ch		;1825	38 05		8 .
	call PickNumberBase	;1827	cd e6 18	. . .
	jr l185dh		;182a	18 31		. 1
l182ch:
	cp 0ech			;182c	fe ec		. .
	jr nz,l1835h		;182e	20 05		  .
	call PrintJustifiedHeader	;1830	cd 9c 18	. . .
	jr l185dh		;1833	18 28		. (
l1835h:
	cp 0e9h			;1835	fe e9		. .
	jr nz,l183eh		;1837	20 05		  .
	ld a,024h		;1839	3e 24		> $
	rst 10h			;183b	d7		.
	jr l185dh		;183c	18 1f		. .
l183eh:
	cp 0ebh			;183e	fe eb		. .
	jr nz,l184ah		;1840	20 08		  .
	ld a,022h		;1842	3e 22		> "
	rst 10h			;1844	d7		.
	call ScanQuotedString	;1845	cd 5f 18	. _ .
	jr l185ah		;1848	18 10		. .
l184ah:
	cp 0edh			;184a	fe ed		. .
	dec hl			;184c	2b		+
	ret nz			;184d	c0		.
	inc hl			;184e	23		#
	ld a,022h		;184f	3e 22		> "
	rst 10h			;1851	d7		.
l1852h:
	call ScanQuotedString	;1852	cd 5f 18	. _ .
	ld a,(hl)		;1855	7e		~
	or a			;1856	b7		.
	jr nz,l1852h		;1857	20 f9		  .
	inc hl			;1859	23		#
l185ah:
	ld a,022h		;185a	3e 22		> "
	rst 10h			;185c	d7		.
l185dh:
	ld a,(hl)		;185d	7e		~
	ret			;185e	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScanQuotedString: assembler string literals - backslash
; escapes and quotes handled while copying to the work buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScanQuotedString:
	ld a,(hl)		;185f	7e		~
	ld c,a			;1860	4f		O
	inc hl			;1861	23		#
	cp 05ch			;1862	fe 5c		. \
	jr z,l1871h		;1864	28 0b		( .
	cp 022h			;1866	fe 22		. "
	jr z,l1871h		;1868	28 07		( .
	cp 0f8h			;186a	fe f8		. .
	jr nc,l1878h		;186c	30 0a		0 .
	jp PrintChar		;186e	c3 be 26	. . &
l1871h:
	ld a,05ch		;1871	3e 5c		> \
	rst 10h			;1873	d7		.
	ld a,c			;1874	79		y
	jp PrintChar		;1875	c3 be 26	. . &
l1878h:
	ld a,05ch		;1878	3e 5c		> \
	rst 10h			;187a	d7		.
	ld a,c			;187b	79		y
	jp PickNumberBase	;187c	c3 e6 18	. . .
l187fh:
	ld a,(hl)		;187f	7e		~
	rst 10h			;1880	d7		.
	inc hl			;1881	23		#
	djnz l187fh		;1882	10 fb		. .
	ret			;1884	c9		.
l1885h:
	ld a,(hl)		;1885	7e		~
	cp 020h			;1886	fe 20		.  
	jr nc,l188ch		;1888	30 02		0 .
	ld a,03fh		;188a	3e 3f		> ?
l188ch:
	call PrintCharBit7	;188c	cd b7 26	. . &
	inc hl			;188f	23		#
	djnz l1885h		;1890	10 f3		. .
	ret			;1892	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintBit7Chars: print a string whose last character carries
; bit 7 (the RST 20h message convention).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintBit7Chars:
	ld a,(hl)		;1893	7e		~
	call PrintCharBit7	;1894	cd b7 26	. . &
	inc hl			;1897	23		#
	rlca			;1898	07		.
	ret c			;1899	d8		.
	jr PrintBit7Chars	;189a	18 f7		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintJustifiedHeader: window headers padded to window width.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintJustifiedHeader:
	ld b,001h		;189c	06 01		. .
sub_189eh:
	or a			;189e	b7		.
l189fh:
	ld a,(hl)		;189f	7e		~
	bit 7,a			;18a0	cb 7f		. .
	call PrintCharBit7	;18a2	cd b7 26	. . &
	inc hl			;18a5	23		#
	jr z,l18adh		;18a6	28 05		( .
	ret c			;18a8	d8		.
	ld a,020h		;18a9	3e 20		>  
	jr l18b7h		;18ab	18 0a		. .
l18adh:
	djnz l189fh		;18ad	10 f0		. .
	scf			;18af	37		7
	ld b,000h		;18b0	06 00		. .
	jr l189fh		;18b2	18 eb		. .
l18b4h:
	ld a,020h		;18b4	3e 20		>  
l18b6h:
	rst 10h			;18b6	d7		.
l18b7h:
	djnz l18b6h		;18b7	10 fd		. .
	or a			;18b9	b7		.
	ret			;18ba	c9		.
sub_18bbh:
	ld c,001h		;18bb	0e 01		. .
sub_18bdh:
	ex de,hl		;18bd	eb		.
	call sub_18c7h		;18be	cd c7 18	. . .
	ld b,c			;18c1	41		A
	call sub_189eh		;18c2	cd 9e 18	. . .
	ex de,hl		;18c5	eb		.
	ret			;18c6	c9		.
sub_18c7h:
	or a			;18c7	b7		.
	ret z			;18c8	c8		.
	ld b,a			;18c9	47		G
l18cah:
	bit 7,(hl)		;18ca	cb 7e		. ~
	inc hl			;18cc	23		#
	jr z,l18cah		;18cd	28 fb		( .
	djnz l18cah		;18cf	10 f9		. .
	ret			;18d1	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintMarkerChar: the listing marker column - "#" or a
; character derived from the address value (IY+05 bit 0
; selects the mode).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintMarkerChar:
	ld c,l			;18d2	4d		M
	ld b,h			;18d3	44		D
	push de			;18d4	d5		.
	bit 0,(iy+005h)		;18d5	fd cb 05 46	. . . F
	ld de,sub_0a00h		;18d9	11 00 0a	. . .
	jr nz,l18e1h		;18dc	20 03		  .
	ld de,l1023h		;18de	11 23 10	. # .
l18e1h:
	call sub_18ech		;18e1	cd ec 18	. . .
	pop de			;18e4	d1		.
	ret			;18e5	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PickNumberBase: choose hex/decimal/binary presentation from
; the monitor flags before printing numbers.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PickNumberBase:
	call sub_18f8h		;18e6	cd f8 18	. . .
sub_18e9h:
	call sub_1902h		;18e9	cd 02 19	. . .
sub_18ech:
	ld a,d			;18ec	7a		z
	ld (0dfefh),a		;18ed	32 ef df	2 . .
	ld a,e			;18f0	7b		{
	rst 10h			;18f1	d7		.
	ld (iy+007h),001h	;18f2	fd 36 07 01	. 6 . .
	jr l1933h		;18f6	18 3b		. ;
sub_18f8h:
	ld b,000h		;18f8	06 00		. .
	ld c,(hl)		;18fa	4e		N
	inc hl			;18fb	23		#
	bit 0,a			;18fc	cb 47		. G
	ret nz			;18fe	c0		.
	ld b,(hl)		;18ff	46		F
	inc hl			;1900	23		#
	ret			;1901	c9		.
sub_1902h:
	and 006h		;1902	e6 06		. .
	ld de,00225h		;1904	11 25 02	. % .
	cp 002h			;1907	fe 02		. .
	ret c			;1909	d8		.
	ld de,l083eh+2		;190a	11 40 08	. @ .
	ret z			;190d	c8		.
	cp 006h			;190e	fe 06		. .
	ld de,sub_0a00h		;1910	11 00 0a	. . .
	ret c			;1913	d8		.
	ld de,l1023h		;1914	11 23 10	. # .
	ret			;1917	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintDecimal: recursive divider method - DE=$000A, divide,
; print remainders (recursion state at IY+07).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintDecimal:
	ld c,a			;1918	4f		O
	ld a,b			;1919	78		x
	ld b,000h		;191a	06 00		. .
	jr l192bh		;191c	18 0d		. .
sub_191eh:
	scf			;191e	37		7
	jr l1922h		;191f	18 01		. .
sub_1921h:
	or a			;1921	b7		.
l1922h:
	ld a,020h		;1922	3e 20		>  
	rst 10h			;1924	d7		.
	ld a,001h		;1925	3e 01		> .
	jr c,l192bh		;1927	38 02		8 .
sub_1929h:
	ld a,005h		;1929	3e 05		> .
l192bh:
	ld (iy+007h),a		;192b	fd 77 07	. w .
sub_192eh:
	ld de,Rst08Vector+2	;192e	11 0a 00	. . .
	jr l1937h		;1931	18 04		. .
l1933h:
	ld de,(0dfefh)		;1933	ed 5b ef df	. [ . .
l1937h:
	push hl			;1937	e5		.
	call sub_1941h		;1938	cd 41 19	. A .
	pop hl			;193b	e1		.
	ret			;193c	c9		.
sub_193dh:
	ld a,b			;193d	78		x
	or c			;193e	b1		.
	jr z,l195eh		;193f	28 1d		( .
sub_1941h:
	dec (iy+007h)		;1941	fd 35 07	. 5 .
	ld a,b			;1944	78		x
	call Divide16		;1945	cd 6a 19	. j .
	push hl			;1948	e5		.
	call sub_193dh		;1949	cd 3d 19	. = .
	pop hl			;194c	e1		.
	ld a,l			;194d	7d		}
	add a,090h		;194e	c6 90		. .
	daa			;1950	27		'
	adc a,040h		;1951	ce 40		. @
	daa			;1953	27		'
	and 07fh		;1954	e6 7f		. .
	cp 020h			;1956	fe 20		.  
	jr nc,l195ch		;1958	30 02		0 .
	ld a,020h		;195a	3e 20		>  
l195ch:
	rst 10h			;195c	d7		.
	ret			;195d	c9		.
l195eh:
	dec (iy+007h)		;195e	fd 35 07	. 5 .
	ret m			;1961	f8		.
	call sub_193dh		;1962	cd 3d 19	. = .
	ld a,(iy+008h)		;1965	fd 7e 08	. ~ .
	rst 10h			;1968	d7		.
	ret			;1969	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Divide16: 16-bit division helper behind all number output.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Divide16:
	ld hl,WriteAnyBankByte	;196a	21 00 00	! . .
	ld b,010h		;196d	06 10		. .
l196fh:
	rl c			;196f	cb 11		. .
	rla			;1971	17		.
	adc hl,hl		;1972	ed 6a		. j
	sbc hl,de		;1974	ed 52		. R
	ccf			;1976	3f		?
	jr nc,l1988h		;1977	30 0f		0 .
l1979h:
	djnz l196fh		;1979	10 f4		. .
	rl c			;197b	cb 11		. .
	rla			;197d	17		.
	ld b,a			;197e	47		G
	ret			;197f	c9		.
l1980h:
	rl c			;1980	cb 11		. .
	rla			;1982	17		.
	adc hl,hl		;1983	ed 6a		. j
	add hl,de		;1985	19		.
	jr c,l1979h		;1986	38 f1		8 .
l1988h:
	djnz l1980h		;1988	10 f6		. .
	rl c			;198a	cb 11		. .
	rla			;198c	17		.
	add hl,de		;198d	19		.
	ld b,a			;198e	47		G
	ret			;198f	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Rst20Handler - print inline message: EX (SP),HL swaps the
; return address into HL (pointing at the inline text), prints
; until a bit-7 character, then stacks the corrected resume
; address - the classic ZX-ROM inline-print trick, on RST 20h.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst20Handler:
	ex (sp),hl		;1990	e3		.
	push af			;1991	f5		.
l1992h:
	ld a,(hl)		;1992	7e		~
	call PrintCharBit7	;1993	cd b7 26	. . &
	and 080h		;1996	e6 80		. .
	inc hl			;1998	23		#
	jr z,l1992h		;1999	28 f7		( .
	pop af			;199b	f1		.
	ex (sp),hl		;199c	e3		.
	ret			;199d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintHexWord / PrintHexByte ($19A3): nibble conversion via
; the DAA trick (add $90, daa, adc $40, daa per nibble).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintHexWord:
	ld a,h			;199e	7c		|
	call PrintHexByte	;199f	cd a3 19	. . .
	ld a,l			;19a2	7d		}
PrintHexByte:
	push af			;19a3	f5		.
	rlca			;19a4	07		.
	rlca			;19a5	07		.
	rlca			;19a6	07		.
	rlca			;19a7	07		.
	call sub_19ach		;19a8	cd ac 19	. . .
	pop af			;19ab	f1		.
sub_19ach:
	and 00fh		;19ac	e6 0f		. .
	add a,090h		;19ae	c6 90		. .
	daa			;19b0	27		'
	adc a,040h		;19b1	ce 40		. @
	daa			;19b3	27		'
	rst 10h			;19b4	d7		.
	ret			;19b5	c9		.
sub_19b6h:
	ld l,(iy+002h)		;19b6	fd 6e 02	. n .
	jr l1a04h		;19b9	18 49		. I
sub_19bbh:
	ld l,001h		;19bb	2e 01		. .
	jr l1a04h		;19bd	18 45		. E
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintExtMenu: walk the extension menu table at ExtMenuTable
; ($E7F7) calling the RAM hook at $EB06 per entry - the
; documented way for RAM extensions to add monitor commands.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintExtMenu:
	bit 4,(iy+014h)		;19bf	fd cb 14 66	. . . f
	ret z			;19c3	c8		.
	ld hl,ExtMenuTable	;19c4	21 f7 e7	! . .
	xor a			;19c7	af		.
l19c8h:
	push af			;19c8	f5		.
	ld e,(hl)		;19c9	5e		^
	inc hl			;19ca	23		#
	ld d,(hl)		;19cb	56		V
	inc hl			;19cc	23		#
	push hl			;19cd	e5		.
	ld a,d			;19ce	7a		z
	or e			;19cf	b3		.
	call nz,0eb06h		;19d0	c4 06 eb	. . .
	pop hl			;19d3	e1		.
	pop af			;19d4	f1		.
	inc a			;19d5	3c		<
	cp 00ah			;19d6	fe 0a		. .
	jr nz,l19c8h		;19d8	20 ee		  .
	ret			;19da	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrepareStep: swap the register file with the step context at
; StepContext (E0AA), park the monitor SP at $E2B5, arm the
; trace enable (StepFlags bit 5), then let the class patchers
; below adjust the step context for the next instruction.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrepareStep:
	ld hl,(UserPc)		;19db	2a 6b dd	* k .
	push hl			;19de	e5		.
	ld hl,(StepContext)	;19df	2a aa e0	* . .
	ld (UserPc),hl		;19e2	22 6b dd	" k .
	ld hl,l19fah		;19e5	21 fa 19	! . .
	push hl			;19e8	e5		.
	ld (MonitorStack),sp	;19e9	ed 73 b5 e2	. s . .
	ld hl,StepFlags		;19ed	21 f1 df	! . .
	set 5,(hl)		;19f0	cb ee		. .
	bit 3,(hl)		;19f2	cb 5e		. ^
	call nz,sub_04cch	;19f4	c4 cc 04	. . .
	jp l0122h		;19f7	c3 22 01	. " .
l19fah:
	res 5,(iy+012h)		;19fa	fd cb 12 ae	. . . .
	pop hl			;19fe	e1		.
	ld (UserPc),hl		;19ff	22 6b dd	" k .
sub_1a02h:
	ld l,0ffh		;1a02	2e ff		. .
l1a04h:
	ld a,(0dd84h)		;1a04	3a 84 dd	: . .
	ld h,a			;1a07	67		g
	add a,l			;1a08	85		.
	rlca			;1a09	07		.
	sla h			;1a0a	cb 24		. $
	rra			;1a0c	1f		.
	ld (0dd84h),a		;1a0d	32 84 dd	2 . .
	ret			;1a10	c9		.
sub_1a11h:
	ld hl,(StepContext)	;1a11	2a aa e0	* . .
	inc hl			;1a14	23		#
	inc hl			;1a15	23		#
	ld (hl),003h		;1a16	36 03		6 .
	inc hl			;1a18	23		#
	ld (hl),000h		;1a19	36 00		6 .
	jr l1a45h		;1a1b	18 28		. (
sub_1a1dh:
	ld hl,(0ddaah)		;1a1d	2a aa dd	* . .
	rst 28h			;1a20	ef		.
	ld e,a			;1a21	5f		_
	inc hl			;1a22	23		#
	rst 28h			;1a23	ef		.
	ld d,a			;1a24	57		W
	inc hl			;1a25	23		#
	ld (0ddaah),hl		;1a26	22 aa dd	" . .
	ld hl,(StepContext)	;1a29	2a aa e0	* . .
	inc hl			;1a2c	23		#
	inc hl			;1a2d	23		#
	ld (hl),e		;1a2e	73		s
	inc hl			;1a2f	23		#
	ld (hl),d		;1a30	72		r
	inc hl			;1a31	23		#
	ld (hl),0cfh		;1a32	36 cf		6 .
	jr PrepareStep		;1a34	18 a5		. .
sub_1a36h:
	ld hl,(StepContext)	;1a36	2a aa e0	* . .
	inc hl			;1a39	23		#
	inc hl			;1a3a	23		#
	push hl			;1a3b	e5		.
	ld de,l0003h+1		;1a3c	11 04 00	. . .
	add hl,de		;1a3f	19		.
	ex de,hl		;1a40	eb		.
	pop hl			;1a41	e1		.
	ld (hl),e		;1a42	73		s
	inc hl			;1a43	23		#
	ld (hl),d		;1a44	72		r
l1a45h:
	inc hl			;1a45	23		#
	ld (hl),0b7h		;1a46	36 b7		6 .
	inc hl			;1a48	23		#
	ld (hl),0cfh		;1a49	36 cf		6 .
	inc hl			;1a4b	23		#
	ld (hl),037h		;1a4c	36 37		6 7
	inc hl			;1a4e	23		#
	ld (hl),0cfh		;1a4f	36 cf		6 .
	ld a,(0dd7fh)		;1a51	3a 7f dd	: . .
	push af			;1a54	f5		.
	call PrepareStep	;1a55	cd db 19	. . .
	call sub_1a02h		;1a58	cd 02 1a	. . .
	ld a,(0dd7fh)		;1a5b	3a 7f dd	: . .
	rrca			;1a5e	0f		.
	pop hl			;1a5f	e1		.
	ld a,h			;1a60	7c		|
	ld (0dd7fh),a		;1a61	32 7f dd	2 . .
	ret			;1a64	c9		.
SwapAltRegs:
	ld hl,RegisterFile	;1a65	21 73 dd	! s .
	ld de,0dd79h		;1a68	11 79 dd	. y .
	ld b,006h		;1a6b	06 06		. .
l1a6dh:
	ld a,(de)		;1a6d	1a		.
	ld c,(hl)		;1a6e	4e		N
	ex de,hl		;1a6f	eb		.
	ld (de),a		;1a70	12		.
	ld (hl),c		;1a71	71		q
	inc de			;1a72	13		.
	inc hl			;1a73	23		#
	djnz l1a6dh		;1a74	10 f7		. .
	ret			;1a76	c9		.
sub_1a77h:
	ld hl,0dd7fh		;1a77	21 7f dd	! . .
	ld de,0dd81h		;1a7a	11 81 dd	. . .
	ld b,002h		;1a7d	06 02		. .
	jr l1a6dh		;1a7f	18 ec		. .
l1a81h:
	ld de,Rst08Vector	;1a81	11 08 00	. . .
	ld hl,(StepPc)		;1a84	2a d0 dd	* . .
	or a			;1a87	b7		.
	sbc hl,de		;1a88	ed 52		. R
	jr nz,l1acah		;1a8a	20 3e		  >
	ld hl,(UserPc)		;1a8c	2a 6b dd	* k .
	ld e,0ffh		;1a8f	1e ff		. .
	jp l01cdh		;1a91	c3 cd 01	. . .
l1a94h:
	ld a,(UserIff)		;1a94	3a 83 dd	: . .
	bit 2,a			;1a97	cb 57		. W
	jr nz,l1aadh		;1a99	20 12		  .
	ld a,0abh		;1a9b	3e ab		> .
	jp l0a2eh		;1a9d	c3 2e 0a	. . .
ToggleAltDisplay:
	ld a,(0ddb4h)		;1aa0	3a b4 dd	: . .
	cp 008h			;1aa3	fe 08		. .
	push af			;1aa5	f5		.
	call z,sub_1a77h	;1aa6	cc 77 1a	. w .
	pop af			;1aa9	f1		.
	call nz,SwapAltRegs	;1aaa	c4 65 1a	. e .
l1aadh:
	call sub_19bbh		;1aad	cd bb 19	. . .
	jp l1b97h		;1ab0	c3 97 1b	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BuildStepStub: build the one-instruction trampoline at
; StepStub (E2B7) - DI (or EI, per the saved user IFF, DD83
; bit 2), the instruction bytes fetched across banks, then $CF
; (RST 08) as the trap.  Running the stub executes exactly one
; user instruction with interrupts in their saved state.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BuildStepStub:
	ld hl,(UserPc)		;1ab3	2a 6b dd	* k .
	call sub_1bf8h		;1ab6	cd f8 1b	. . .
	jp nc,l1bcbh		;1ab9	d2 cb 1b	. . .
	call FetchPrefixBytes	;1abc	cd b1 16	. . .
	ld a,(0ddb7h)		;1abf	3a b7 dd	: . .
	cp 032h			;1ac2	fe 32		. 2
	jr z,l1a94h		;1ac4	28 ce		( .
	cp 072h			;1ac6	fe 72		. r
	jr z,l1a81h		;1ac8	28 b7		( .
l1acah:
	ld hl,(StepContext)	;1aca	2a aa e0	* . .
	push hl			;1acd	e5		.
	ld de,StepStub		;1ace	11 b7 e2	. . .
	ld bc,Rst08Vector	;1ad1	01 08 00	. . .
	ldir			;1ad4	ed b0		. .
	pop de			;1ad6	d1		.
	ld a,(UserIff)		;1ad7	3a 83 dd	: . .
	bit 2,a			;1ada	cb 57		. W
	ld a,0f3h		;1adc	3e f3		> .
	jr z,l1ae2h		;1ade	28 02		( .
	ld a,0fbh		;1ae0	3e fb		> .
l1ae2h:
	ld (de),a		;1ae2	12		.
	inc de			;1ae3	13		.
	ld hl,(UserPc)		;1ae4	2a 6b dd	* k .
	ld c,(iy+002h)		;1ae7	fd 4e 02	. N .
	ld b,000h		;1aea	06 00		. .
	call CopyAcrossBanks	;1aec	cd 46 06	. F .
	ld a,0cfh		;1aef	3e cf		> .
	ld (de),a		;1af1	12		.
	ld a,(iy+003h)		;1af2	fd 7e 03	. ~ .
	or a			;1af5	b7		.
	jp z,l1b94h		;1af6	ca 94 1b	. . .
	bit 7,a			;1af9	cb 7f		. .
	jr nz,ToggleAltDisplay	;1afb	20 a3		  .
	bit 1,a			;1afd	cb 4f		. O
	jr z,StepDispatch	;1aff	28 1e		( .
	bit 5,a			;1b01	cb 6f		. o
	jr nz,l1b16h		;1b03	20 11		  .
	ld hl,(StepContext)	;1b05	2a aa e0	* . .
	inc hl			;1b08	23		#
	ld a,(hl)		;1b09	7e		~
	and 038h		;1b0a	e6 38		. 8
	or 0c2h			;1b0c	f6 c2		. .
	ld (hl),a		;1b0e	77		w
	call sub_1a36h		;1b0f	cd 36 1a	. 6 .
	jr nc,l1b26h		;1b12	30 12		0 .
	jr l1b19h		;1b14	18 03		. .
l1b16h:
	call sub_19b6h		;1b16	cd b6 19	. . .
l1b19h:
	call sub_2de0h		;1b19	cd e0 2d	. . -
	ex de,hl		;1b1c	eb		.
	jr l1b9ah		;1b1d	18 7b		. {
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; StepDispatch: after the trap - classify the executed
; instruction from the AnalyzeInstruction results (DDB4 area):
; HALT-like, jump taken, CALL entered, RET - and fix the saved
; user PC accordingly.  SetTempBreakpoint below makes CALLs
; stop at their callee.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
StepDispatch:
	bit 2,a			;1b1f	cb 57		. W
	jr z,l1b2ah		;1b21	28 07		( .
	call sub_1a11h		;1b23	cd 11 1a	. . .
l1b26h:
	jr nc,l1b97h		;1b26	30 6f		0 o
	jr l1b6dh		;1b28	18 43		. C
l1b2ah:
	bit 0,a			;1b2a	cb 47		. G
	jr z,l1b5ah		;1b2c	28 2c		( ,
	bit 7,(iy+016h)		;1b2e	fd cb 16 7e	. . . ~
	jr nz,l1bafh		;1b32	20 7b		  {
	call CheckTrace		;1b34	cd e9 1b	. . .
	jr c,l1bb4h		;1b37	38 7b		8 {
l1b39h:
	ld hl,(StepContext)	;1b39	2a aa e0	* . .
	inc hl			;1b3c	23		#
	ld a,(hl)		;1b3d	7e		~
	bit 0,a			;1b3e	cb 47		. G
	jr nz,l1b4eh		;1b40	20 0c		  .
	and 038h		;1b42	e6 38		. 8
	or 0c2h			;1b44	f6 c2		. .
	ld (hl),a		;1b46	77		w
	call sub_1a36h		;1b47	cd 36 1a	. 6 .
	jr nc,l1b97h		;1b4a	30 4b		0 K
	jr l1b51h		;1b4c	18 03		. .
l1b4eh:
	call sub_19bbh		;1b4e	cd bb 19	. . .
l1b51h:
	ld de,(0ddaah)		;1b51	ed 5b aa dd	. [ . .
	call sub_354ah		;1b55	cd 4a 35	. J 5
	jr l1b6dh		;1b58	18 13		. .
l1b5ah:
	bit 3,a			;1b5a	cb 5f		. _
	jr z,l1b72h		;1b5c	28 14		( .
	call sub_1a36h		;1b5e	cd 36 1a	. 6 .
	jr nc,l1b97h		;1b61	30 34		0 4
	call CheckTrace		;1b63	cd e9 1b	. . .
	push af			;1b66	f5		.
	call c,sub_1a02h	;1b67	dc 02 1a	. . .
	pop af			;1b6a	f1		.
	jr c,SetTempBreakpoint	;1b6b	38 61		8 a
l1b6dh:
	ld hl,(StepPc)		;1b6d	2a d0 dd	* . .
	jr l1b9ah		;1b70	18 28		. (
l1b72h:
	bit 4,a			;1b72	cb 67		. g
	jr z,l1b8fh		;1b74	28 19		( .
	call sub_19bbh		;1b76	cd bb 19	. . .
	ld hl,(RegisterFile)	;1b79	2a 73 dd	* s .
	ld a,(iy+001h)		;1b7c	fd 7e 01	. ~ .
	bit 1,a			;1b7f	cb 4f		. O
	jr z,l1b9ah		;1b81	28 17		( .
	ld hl,(0dd6fh)		;1b83	2a 6f dd	* o .
	bit 0,a			;1b86	cb 47		. G
	jr z,l1b9ah		;1b88	28 10		( .
	ld hl,(0dd71h)		;1b8a	2a 71 dd	* q .
	jr l1b9ah		;1b8d	18 0b		. .
l1b8fh:
	ld a,0aah		;1b8f	3e aa		> .
	jp l0a2eh		;1b91	c3 2e 0a	. . .
l1b94h:
	call PrepareStep	;1b94	cd db 19	. . .
l1b97h:
	ld hl,(0ddaah)		;1b97	2a aa dd	* . .
l1b9ah:
	ld (UserPc),hl		;1b9a	22 6b dd	" k .
	push hl			;1b9d	e5		.
	ld hl,StepStub		;1b9e	21 b7 e2	! . .
	ld de,(StepContext)	;1ba1	ed 5b aa e0	. [ . .
	ld bc,Rst08Vector	;1ba5	01 08 00	. . .
	ldir			;1ba8	ed b0		. .
	call PrintExtMenu	;1baa	cd bf 19	. . .
	pop hl			;1bad	e1		.
	ret			;1bae	c9		.
l1bafh:
	call sub_1bf5h		;1baf	cd f5 1b	. . .
	jr nc,l1b39h		;1bb2	30 85		0 .
l1bb4h:
	ld hl,(StepPc)		;1bb4	2a d0 dd	* . .
	ld de,ReadAnyBankByte	;1bb7	11 28 00	. ( .
	or a			;1bba	b7		.
	sbc hl,de		;1bbb	ed 52		. R
	jr nz,l1b94h		;1bbd	20 d5		  .
	bit 4,(iy-002h)		;1bbf	fd cb fe 66	. . . f
	jp nz,l1b39h		;1bc3	c2 39 1b	. 9 .
	call sub_1a1dh		;1bc6	cd 1d 1a	. . .
	jr l1b97h		;1bc9	18 cc		. .
l1bcbh:
	ld (StepPc),hl		;1bcb	22 d0 dd	" . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SetTempBreakpoint: plant CALL nnnn + RST 08 into the shadow
; context so stepping over a CALL breaks at the target.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SetTempBreakpoint:
	ld hl,(StepContext)	;1bce	2a aa e0	* . .
	inc hl			;1bd1	23		#
	ld (hl),0cdh		;1bd2	36 cd		6 .
	inc hl			;1bd4	23		#
	ld de,(StepPc)		;1bd5	ed 5b d0 dd	. [ . .
	ld (hl),e		;1bd9	73		s
	inc hl			;1bda	23		#
	ld (hl),d		;1bdb	72		r
	inc hl			;1bdc	23		#
	ld (hl),0cfh		;1bdd	36 cf		6 .
	call sub_2de0h		;1bdf	cd e0 2d	. . -
	push de			;1be2	d5		.
	call PrepareStep	;1be3	cd db 19	. . .
	pop hl			;1be6	e1		.
	jr l1b9ah		;1be7	18 b1		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckTrace: trace-mode gate evaluated after every step.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckTrace:
	or a			;1be9	b7		.
	bit 3,(iy+005h)		;1bea	fd cb 05 5e	. . . ^
	ret z			;1bee	c8		.
	ld a,(0ddd1h)		;1bef	3a d1 dd	: . .
	cp 040h			;1bf2	fe 40		. @
	ret nc			;1bf4	d0		.
sub_1bf5h:
	ld hl,(StepPc)		;1bf5	2a d0 dd	* . .
sub_1bf8h:
	bit 4,(iy-002h)		;1bf8	fd cb fe 66	. . . f
	scf			;1bfc	37		7
	ret z			;1bfd	c8		.
	ld de,l3d00h		;1bfe	11 00 3d	. . =
	or a			;1c01	b7		.
	sbc hl,de		;1c02	ed 52		. R
	add hl,de		;1c04	19		.
	ret c			;1c05	d8		.
	ld de,l3e00h		;1c06	11 00 3e	. . >
	sbc hl,de		;1c09	ed 52		. R
	add hl,de		;1c0b	19		.
	ccf			;1c0c	3f		?
	ret			;1c0d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AnalyzeInstruction - the static analyser: opcode class (EB/
; DD/FD/76/CB handled), length and control-flow effect from
; OpcodeClassTable ($21D7); results returned through the stack
; frame (SP=$DDCB on return).  Feeds StepDispatch and the
; step-over/breakpoint logic.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AnalyzeInstruction:
	ld (ix+000h),0f7h	;1c0e	dd 36 00 f7	. 6 . .
	pop ix			;1c12	dd e1		. .
	ret			;1c14	c9		.
l1c15h:
	ld (ix+000h),0aah	;1c15	dd 36 00 aa	. 6 . .
	inc ix			;1c19	dd 23		. #
	ld (ix+000h),007h	;1c1b	dd 36 00 07	. 6 . .
l1c1fh:
	inc ix			;1c1f	dd 23		. #
l1c21h:
	ld (iy+002h),001h	;1c21	fd 36 02 01	. 6 . .
	jp l1d4bh		;1c25	c3 4b 1d	. K .
l1c28h:
	ld (ix+000h),032h	;1c28	dd 36 00 32	. 6 . 2
	ld a,(hl)		;1c2c	7e		~
	or a			;1c2d	b7		.
	jr z,l1c1fh		;1c2e	28 ef		( .
l1c30h:
	ld hl,AnalyzeDataPool_start	;1c30	21 4a 1c	! J .
	ld de,0ddb7h		;1c33	11 b7 dd	. . .
	ld bc,Rst08Vector	;1c36	01 08 00	. . .
	ldir			;1c39	ed b0		. .
	push de			;1c3b	d5		.
	pop ix			;1c3c	dd e1		. .
	set 6,(iy+003h)		;1c3e	fd cb 03 f6	. . . .
	ld a,(0ddach)		;1c42	3a ac dd	: . .
	ld (0ddb9h),a		;1c45	32 b9 dd	2 . .
	jr l1c21h		;1c48	18 d7		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AnalyzeDataPool: four scratch bytes followed by
; the 'BAD' text ($F0-terminated) shown for invalid
; opcodes; HL is aimed here by AnalyzeInstruction
; ($1C30) and the walker at $1C52 is called from
; $16BE.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'AnalyzeDataPool' (start 0x1c4a end 0x1c52)
AnalyzeDataPool_start:
	defb 073h		;1c4a	73		s
	defb 0ffh		;1c4b	ff		.
	defb 000h		;1c4c	00		.
	defb 0eeh		;1c4d	ee		.
	defb 042h		;1c4e	42		B
	defb 041h		;1c4f	41		A
	defb 044h		;1c50	44		D
	defb 0f0h		;1c51	f0		.
AnalyzeDataPool_end:
	push ix			;1c52	dd e5		. .
	ld ix,0ddb7h		;1c54	dd 21 b7 dd	. ! . .
	ld de,(0ddaah)		;1c58	ed 5b aa dd	. [ . .
	ld hl,AnalyzeInstruction	;1c5c	21 0e 1c	! . .
	push hl			;1c5f	e5		.
	ld (0ddcbh),sp		;1c60	ed 73 cb dd	. s . .
	bit 4,(iy+000h)		;1c64	fd cb 00 66	. . . f
	jr z,l1c72h		;1c68	28 08		( .
	call FindBreakpoint	;1c6a	cd f5 1f	. . .
	ld b,0f1h		;1c6d	06 f1		. .
	call c,WordToHexBuf	;1c6f	dc ca 1f	. . .
l1c72h:
	ld e,009h		;1c72	1e 09		. .
	ld hl,0dfe0h		;1c74	21 e0 df	! . .
	xor a			;1c77	af		.
	ld d,a			;1c78	57		W
	ld (hl),a		;1c79	77		w
	ld (0ddcdh),a		;1c7a	32 cd dd	2 . .
	ld (0dfe2h),a		;1c7d	32 e2 df	2 . .
	ld (0dfe3h),a		;1c80	32 e3 df	2 . .
	ld bc,0ddach		;1c83	01 ac dd	. . .
	ld a,(bc)		;1c86	0a		.
	cp 0ebh			;1c87	fe eb		. .
	jr z,l1c15h		;1c89	28 8a		( .
	cp 0ddh			;1c8b	fe dd		. .
	jr nz,l1c95h		;1c8d	20 06		  .
	ld (hl),002h		;1c8f	36 02		6 .
	inc bc			;1c91	03		.
	inc d			;1c92	14		.
	jr l1c9dh		;1c93	18 08		. .
l1c95h:
	cp 0fdh			;1c95	fe fd		. .
	jr nz,l1c9dh		;1c97	20 04		  .
	ld (hl),003h		;1c99	36 03		6 .
	inc bc			;1c9b	03		.
	inc d			;1c9c	14		.
l1c9dh:
	ld a,(bc)		;1c9d	0a		.
	cp 076h			;1c9e	fe 76		. v
	jr z,l1c28h		;1ca0	28 86		( .
	cp 0cbh			;1ca2	fe cb		. .
	jr nz,l1caeh		;1ca4	20 08		  .
	set 3,(hl)		;1ca6	cb de		. .
	ld e,016h		;1ca8	1e 16		. .
	inc bc			;1caa	03		.
	inc d			;1cab	14		.
	jr l1cbdh		;1cac	18 0f		. .
l1caeh:
	cp 0edh			;1cae	fe ed		. .
	jr nz,l1cbdh		;1cb0	20 0b		  .
	set 2,(hl)		;1cb2	cb d6		. .
	bit 1,(hl)		;1cb4	cb 4e		. N
	jp nz,l1c30h		;1cb6	c2 30 1c	. 0 .
	inc bc			;1cb9	03		.
	ld e,018h		;1cba	1e 18		. .
	inc d			;1cbc	14		.
l1cbdh:
	inc d			;1cbd	14		.
	ld (iy+002h),d		;1cbe	fd 72 02	. r .
	push de			;1cc1	d5		.
	ld de,0ddb4h		;1cc2	11 b4 dd	. . .
	ld h,b			;1cc5	60		`
	ld l,c			;1cc6	69		i
	ld bc,l0003h		;1cc7	01 03 00	. . .
	ldir			;1cca	ed b0		. .
	pop de			;1ccc	d1		.
	ld a,(0dfe0h)		;1ccd	3a e0 df	: . .
	and 00ah		;1cd0	e6 0a		. .
	cp 00ah			;1cd2	fe 0a		. .
	jr nz,l1cdch		;1cd4	20 06		  .
	ld a,(0ddb5h)		;1cd6	3a b5 dd	: . .
	ld (0ddb4h),a		;1cd9	32 b4 dd	2 . .
l1cdch:
	xor a			;1cdc	af		.
	push af			;1cdd	f5		.
	push af			;1cde	f5		.
	ld a,e			;1cdf	7b		{
l1ce0h:
	add a,a			;1ce0	87		.
	add a,a			;1ce1	87		.
	ld b,000h		;1ce2	06 00		. .
	ld c,a			;1ce4	4f		O
	ld hl,OpcodeClassTable	;1ce5	21 d7 21	! . !
	add hl,bc		;1ce8	09		.
	ld c,(hl)		;1ce9	4e		N
	inc hl			;1cea	23		#
	ld d,(hl)		;1ceb	56		V
	inc hl			;1cec	23		#
	ld a,(hl)		;1ced	7e		~
	inc hl			;1cee	23		#
	ld h,(hl)		;1cef	66		f
	ld l,a			;1cf0	6f		o
	ld a,(0ddb4h)		;1cf1	3a b4 dd	: . .
	ld b,000h		;1cf4	06 00		. .
	and c			;1cf6	a1		.
l1cf7h:
	inc b			;1cf7	04		.
	srl c			;1cf8	cb 39		. 9
	jr nc,l1cf7h		;1cfa	30 fb		0 .
	or a			;1cfc	b7		.
l1cfdh:
	dec b			;1cfd	05		.
	jr z,l1d03h		;1cfe	28 03		( .
	rrca			;1d00	0f		.
	jr l1cfdh		;1d01	18 fa		. .
l1d03h:
	bit 0,d			;1d03	cb 42		. B
	jr nz,l1d08h		;1d05	20 01		  .
	add a,a			;1d07	87		.
l1d08h:
	ld b,000h		;1d08	06 00		. .
	ld c,a			;1d0a	4f		O
	add hl,bc		;1d0b	09		.
l1d0ch:
	ld a,(hl)		;1d0c	7e		~
	inc hl			;1d0d	23		#
	dec d			;1d0e	15		.
	or a			;1d0f	b7		.
	jr z,l1d35h		;1d10	28 23		( #
	cp 0eah			;1d12	fe ea		. .
	jp z,l1c30h		;1d14	ca 30 1c	. 0 .
	cp 0c7h			;1d17	fe c7		. .
	jr c,l1d67h		;1d19	38 4c		8 L
	cp 0eah			;1d1b	fe ea		. .
	jr nc,l1d67h		;1d1d	30 48		0 H
	sub 0c6h		;1d1f	d6 c6		. .
	cp 017h			;1d21	fe 17		. .
	jr nz,l1d29h		;1d23	20 04		  .
	set 6,(iy+004h)		;1d25	fd cb 04 f6	. . . .
l1d29h:
	bit 0,d			;1d29	cb 42		. B
	jr z,l1ce0h		;1d2b	28 b3		( .
	push de			;1d2d	d5		.
	push hl			;1d2e	e5		.
	jr l1ce0h		;1d2f	18 af		. .
l1d31h:
	ld a,d			;1d31	7a		z
	or a			;1d32	b7		.
	jr nz,l1d0ch		;1d33	20 d7		  .
l1d35h:
	pop hl			;1d35	e1		.
	pop de			;1d36	d1		.
	ld a,d			;1d37	7a		z
	or a			;1d38	b7		.
	jr nz,l1d0ch		;1d39	20 d1		  .
	ld hl,0dfe0h		;1d3b	21 e0 df	! . .
	bit 1,(hl)		;1d3e	cb 4e		. N
	jr z,l1d4bh		;1d40	28 09		( .
	bit 4,(hl)		;1d42	cb 66		. f
	jr nz,l1d4bh		;1d44	20 05		  .
	bit 7,(hl)		;1d46	cb 7e		. ~
	jp z,l1c30h		;1d48	ca 30 1c	. 0 .
l1d4bh:
	ld bc,(0dfe1h)		;1d4b	ed 4b e1 df	. K . .
	ld b,000h		;1d4f	06 00		. .
	ld sp,(0ddcbh)		;1d51	ed 7b cb dd	. { . .
	ret			;1d55	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CollectAsmText: capture assembler source from the editor
; into the work buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CollectAsmText:
	cp 020h			;1d56	fe 20		.  
	jr nc,EditorKeyLoop	;1d58	30 20		0  
	ld (ix+000h),a		;1d5a	dd 77 00	. w .
	inc ix			;1d5d	dd 23		. #
	set 5,(iy+001h)		;1d5f	fd cb 01 ee	. . . .
	ld a,022h		;1d63	3e 22		> "
	jr EditorKeyLoop	;1d65	18 13		. .
l1d67h:
	push de			;1d67	d5		.
	push hl			;1d68	e5		.
	bit 1,(iy+001h)		;1d69	fd cb 01 4e	. . . N
	jr z,EditorKeyLoop	;1d6d	28 0b		( .
	ld hl,0dfe3h		;1d6f	21 e3 df	! . .
	bit 7,(hl)		;1d72	cb 7e		. ~
	jr nz,CollectAsmText	;1d74	20 e0		  .
	bit 6,(hl)		;1d76	cb 76		. v
	jr nz,CollectAsmText	;1d78	20 dc		  .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EditorKeyLoop: the line editor dispatcher - EditorKeyTable
; ($23BB, CPIR-searched, 0x24 entries) maps keys to cursor
; moves, insert/delete and history actions.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EditorKeyLoop:
	push af			;1d7a	f5		.
	ld hl,EditorKeyTable	;1d7b	21 bb 23	! . #
	ld bc,l0024h		;1d7e	01 24 00	. $ .
	cpir			;1d81	ed b1		. .
	ld b,a			;1d83	47		G
	jp nz,EditorRegKeys	;1d84	c2 6a 1e	. j .
	ld a,c			;1d87	79		y
	cp 01eh			;1d88	fe 1e		. .
	jr nc,l1da8h		;1d8a	30 1c		0 .
	cp 01ch			;1d8c	fe 1c		. .
	jr nc,l1dddh		;1d8e	30 4d		0 M
	cp 00bh			;1d90	fe 0b		. .
	jp c,l1ee9h		;1d92	da e9 1e	. . .
	cp 014h			;1d95	fe 14		. .
	ld hl,0dfe2h		;1d97	21 e2 df	! . .
	jr nc,l1de3h		;1d9a	30 47		0 G
	cp 011h			;1d9c	fe 11		. .
	jr c,l1debh		;1d9e	38 4b		8 K
	ld (hl),022h		;1da0	36 22		6 "
	jp EditorRegKeys	;1da2	c3 6a 1e	. j .
l1da5h:
	dec b			;1da5	05		.
	jr l1dd4h		;1da6	18 2c		. ,
l1da8h:
	ld hl,0dfe0h		;1da8	21 e0 df	! . .
	cp 023h			;1dab	fe 23		. #
	jr z,l1da5h		;1dad	28 f6		( .
	cp 022h			;1daf	fe 22		. "
	jr z,l1dd4h		;1db1	28 21		( !
	bit 1,(hl)		;1db3	cb 4e		. N
	jr z,l1dd6h		;1db5	28 1f		( .
	bit 3,(hl)		;1db7	cb 5e		. ^
	jr nz,l1dd6h		;1db9	20 1b		  .
	sub 01dh		;1dbb	d6 1d		. .
	cp 003h			;1dbd	fe 03		. .
	jr nc,l1dd9h		;1dbf	30 18		0 .
	bit 7,(hl)		;1dc1	cb 7e		. ~
	jr nz,l1dd6h		;1dc3	20 11		  .
	bit 5,(iy+004h)		;1dc5	fd cb 04 6e	. . . n
	jr nz,l1dd6h		;1dc9	20 0b		  .
l1dcbh:
	bit 0,(hl)		;1dcb	cb 46		. F
	jr z,l1dd1h		;1dcd	28 02		( .
	add a,002h		;1dcf	c6 02		. .
l1dd1h:
	ld b,a			;1dd1	47		G
	set 4,(hl)		;1dd2	cb e6		. .
l1dd4h:
	set 5,(hl)		;1dd4	cb ee		. .
l1dd6h:
	jp EditorRegKeys	;1dd6	c3 6a 1e	. j .
l1dd9h:
	add a,07bh		;1dd9	c6 7b		. {
	jr l1dcbh		;1ddb	18 ee		. .
l1dddh:
	set 7,(iy+003h)		;1ddd	fd cb 03 fe	. . . .
	jr l1dd6h		;1de1	18 f3		. .
l1de3h:
	bit 1,(hl)		;1de3	cb 4e		. N
	jr z,l1dd6h		;1de5	28 ef		( .
	ld (hl),002h		;1de7	36 02		6 .
	jr l1dd6h		;1de9	18 eb		. .
l1debh:
	ld hl,(RegisterFile)	;1deb	2a 73 dd	* s .
	ld (0ddceh),hl		;1dee	22 ce dd	" . .
	cp 00eh			;1df1	fe 0e		. .
	jr nz,l1df9h		;1df3	20 04		  .
	set 4,(iy+003h)		;1df5	fd cb 03 e6	. . . .
l1df9h:
	ld hl,0dfe0h		;1df9	21 e0 df	! . .
	ld a,(hl)		;1dfc	7e		~
	bit 1,a			;1dfd	cb 4f		. O
	jr z,l1dd6h		;1dff	28 d5		( .
	set 7,(hl)		;1e01	cb fe		. .
	ld e,c			;1e03	59		Y
	ld hl,l23d4h		;1e04	21 d4 23	! . #
	bit 0,a			;1e07	cb 47		. G
	ld b,000h		;1e09	06 00		. .
	jr z,l1e11h		;1e0b	28 04		( .
	ld a,c			;1e0d	79		y
	add a,006h		;1e0e	c6 06		. .
	ld c,a			;1e10	4f		O
l1e11h:
	add hl,bc		;1e11	09		.
	ld b,(hl)		;1e12	46		F
	ld (ix+000h),b		;1e13	dd 70 00	. p .
	inc ix			;1e16	dd 23		. #
	ld a,e			;1e18	7b		{
	cp 00dh			;1e19	fe 0d		. .
	jr nc,l1e6fh		;1e1b	30 52		0 R
	cp 00ch			;1e1d	fe 0c		. .
	jr nz,l1e39h		;1e1f	20 18		  .
	bit 4,(iy+001h)		;1e21	fd cb 01 66	. . . f
	jr z,l1e39h		;1e25	28 12		( .
	ld a,001h		;1e27	3e 01		> .
	and (ix-002h)		;1e29	dd a6 fe	. . .
	or 0bah			;1e2c	f6 ba		. .
	ld (ix-002h),a		;1e2e	dd 77 fe	. w .
	res 4,(iy+001h)		;1e31	fd cb 01 a6	. . . .
	res 5,(iy+001h)		;1e35	fd cb 01 ae	. . . .
l1e39h:
	inc (iy+002h)		;1e39	fd 34 02	. 4 .
	ld a,(0ddaeh)		;1e3c	3a ae dd	: . .
	or a			;1e3f	b7		.
	jp p,l1e48h		;1e40	f2 48 1e	. H .
	inc (ix-001h)		;1e43	dd 34 ff	. 4 .
	neg			;1e46	ed 44		. D
l1e48h:
	ld (ix+000h),0fdh	;1e48	dd 36 00 fd	. 6 . .
	inc ix			;1e4c	dd 23		. #
	ld b,a			;1e4e	47		G
	ld hl,(0dd6fh)		;1e4f	2a 6f dd	* o .
	bit 0,(iy+001h)		;1e52	fd cb 01 46	. . . F
	jr z,l1e5bh		;1e56	28 03		( .
	ld hl,(0dd71h)		;1e58	2a 71 dd	* q .
l1e5bh:
	ld de,(0ddaeh)		;1e5b	ed 5b ae dd	. [ . .
	ld d,000h		;1e5f	16 00		. .
	bit 7,e			;1e61	cb 7b		. {
	jr z,l1e66h		;1e63	28 01		( .
	dec d			;1e65	15		.
l1e66h:
	add hl,de		;1e66	19		.
	ld (0ddceh),hl		;1e67	22 ce dd	" . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EditorRegKeys: register-edit keys (RegisterKeyTable $23EB)
; - hex entry into the highlighted register of the dump.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EditorRegKeys:
	ld (ix+000h),b		;1e6a	dd 70 00	. p .
	inc ix			;1e6d	dd 23		. #
l1e6fh:
	ld hl,RegisterKeyTable	;1e6f	21 eb 23	! . #
	ld bc,Rst08Vector+2	;1e72	01 0a 00	. . .
	cpir			;1e75	ed b1		. .
	jr nz,l1ecfh		;1e77	20 56		  V
	ld a,c			;1e79	79		y
	cp 008h			;1e7a	fe 08		. .
	jr nc,l1ed8h		;1e7c	30 5a		0 Z
	and 003h		;1e7e	e6 03		. .
	jr z,l1e97h		;1e80	28 15		( .
	dec a			;1e82	3d		=
	jr z,l1e8dh		;1e83	28 08		( .
	dec a			;1e85	3d		=
	jr z,l1e92h		;1e86	28 0a		( .
	ld hl,(StepPc)		;1e88	2a d0 dd	* . .
	jr l1e9ah		;1e8b	18 0d		. .
l1e8dh:
	ld hl,(0dd77h)		;1e8d	2a 77 dd	* w .
	jr l1e9ah		;1e90	18 08		. .
l1e92h:
	ld hl,(0dd75h)		;1e92	2a 75 dd	* u .
	jr l1e9ah		;1e95	18 03		. .
l1e97h:
	ld hl,(0ddceh)		;1e97	2a ce dd	* . .
l1e9ah:
	ld (0ddd2h),hl		;1e9a	22 d2 dd	" . .
	ld a,c			;1e9d	79		y
	cp 004h			;1e9e	fe 04		. .
	ld b,002h		;1ea0	06 02		. .
	jr nc,l1ecch		;1ea2	30 28		0 (
	dec b			;1ea4	05		.
	cp 003h			;1ea5	fe 03		. .
	jr nz,l1eb2h		;1ea7	20 09		  .
	ld a,(0ddcdh)		;1ea9	3a cd dd	: . .
	cp 0a1h			;1eac	fe a1		. .
	jr nz,l1ecch		;1eae	20 1c		  .
	jr l1ecfh		;1eb0	18 1d		. .
l1eb2h:
	or a			;1eb2	b7		.
	jr nz,l1ecch		;1eb3	20 17		  .
	ld a,(0ddcdh)		;1eb5	3a cd dd	: . .
	ld hl,l23f5h		;1eb8	21 f5 23	! . #
	ld bc,Rst08Vector+1	;1ebb	01 09 00	. . .
	cpir			;1ebe	ed b1		. .
	jr z,l1ecah		;1ec0	28 08		( .
	bit 7,(iy+004h)		;1ec2	fd cb 04 7e	. . . ~
	ld b,001h		;1ec6	06 01		. .
	jr z,l1ecch		;1ec8	28 02		( .
l1ecah:
	ld b,003h		;1eca	06 03		. .
l1ecch:
	ld (iy+004h),b		;1ecc	fd 70 04	. p .
l1ecfh:
	pop af			;1ecf	f1		.
	ld (0ddcdh),a		;1ed0	32 cd dd	2 . .
	pop hl			;1ed3	e1		.
	pop de			;1ed4	d1		.
	jp l1d31h		;1ed5	c3 31 1d	. 1 .
l1ed8h:
	ld b,003h		;1ed8	06 03		. .
	ld hl,(RegisterFile)	;1eda	2a 73 dd	* s .
	ld (0ddd2h),hl		;1edd	22 d2 dd	" . .
	jr l1ecch		;1ee0	18 ea		. .
l1ee2h:
	ld b,0ech		;1ee2	06 ec		. .
	call WordToHexBuf	;1ee4	cd ca 1f	. . .
	jr l1e6fh		;1ee7	18 86		. .
l1ee9h:
	cp 003h			;1ee9	fe 03		. .
	jr c,l1ef2h		;1eeb	38 05		8 .
	ld (ix+000h),b		;1eed	dd 70 00	. p .
	inc ix			;1ef0	dd 23		. #
l1ef2h:
	ld d,000h		;1ef2	16 00		. .
	ld hl,0dfe2h		;1ef4	21 e2 df	! . .
	cp 001h			;1ef7	fe 01		. .
	jr c,l1f78h		;1ef9	38 7d		8 }
	jr z,l1f1bh		;1efb	28 1e		( .
	cp 004h			;1efd	fe 04		. .
	jr c,l1f48h		;1eff	38 47		8 G
	cp 006h			;1f01	fe 06		. .
	jr c,l1f1bh		;1f03	38 16		8 .
	cp 008h			;1f05	fe 08		. .
	jr c,l1f63h		;1f07	38 5a		8 Z
	jr z,l1f67h		;1f09	28 5c		( \
	cp 00ah			;1f0b	fe 0a		. .
	jp z,l1fb7h		;1f0d	ca b7 1f	. . .
	ld a,(0ddb4h)		;1f10	3a b4 dd	: . .
	and 038h		;1f13	e6 38		. 8
	set 0,(hl)		;1f15	cb c6		. .
	ld e,a			;1f17	5f		_
	jp l1fa2h		;1f18	c3 a2 1f	. . .
l1f1bh:
	ld a,(0ddcdh)		;1f1b	3a cd dd	: . .
	cp 0a1h			;1f1e	fe a1		. .
	jr z,l1f48h		;1f20	28 26		( &
	cp 055h			;1f22	fe 55		. U
	jr z,l1f3ah		;1f24	28 14		( .
	cp 053h			;1f26	fe 53		. S
	jr z,l1f32h		;1f28	28 08		( .
	cp 091h			;1f2a	fe 91		. .
	jr nc,l1f36h		;1f2c	30 08		0 .
	cp 082h			;1f2e	fe 82		. .
	jr c,l1f3ch		;1f30	38 0a		8 .
l1f32h:
	set 3,(hl)		;1f32	cb de		. .
	jr l1f3ch		;1f34	18 06		. .
l1f36h:
	cp 099h			;1f36	fe 99		. .
	jr nc,l1f3ch		;1f38	30 02		0 .
l1f3ah:
	set 0,(hl)		;1f3a	cb c6		. .
l1f3ch:
	ld de,(0ddb5h)		;1f3c	ed 5b b5 dd	. [ . .
	inc (iy+002h)		;1f40	fd 34 02	. 4 .
	inc (iy+002h)		;1f43	fd 34 02	. 4 .
	jr l1f90h		;1f46	18 48		. H
l1f48h:
	inc (iy+002h)		;1f48	fd 34 02	. 4 .
	ld hl,0dfe0h		;1f4b	21 e0 df	! . .
	bit 1,(hl)		;1f4e	cb 4e		. N
	jr z,l1f5bh		;1f50	28 09		( .
	bit 4,(hl)		;1f52	cb 66		. f
	jr nz,l1f5bh		;1f54	20 05		  .
	ld a,(0ddb6h)		;1f56	3a b6 dd	: . .
	jr l1f5eh		;1f59	18 03		. .
l1f5bh:
	ld a,(0ddb5h)		;1f5b	3a b5 dd	: . .
l1f5eh:
	ld e,a			;1f5e	5f		_
	ld d,000h		;1f5f	16 00		. .
	jr l1f9ah		;1f61	18 37		. 7
l1f63h:
	set 7,(iy+004h)		;1f63	fd cb 04 fe	. . . .
l1f67h:
	ld a,(0ddb4h)		;1f67	3a b4 dd	: . .
	set 5,(iy+004h)		;1f6a	fd cb 04 ee	. . . .
	and 038h		;1f6e	e6 38		. 8
	rrca			;1f70	0f		.
	rrca			;1f71	0f		.
	rrca			;1f72	0f		.
	ld e,a			;1f73	5f		_
	ld b,0fch		;1f74	06 fc		. .
	jr l1fa4h		;1f76	18 2c		. ,
l1f78h:
	inc (iy+002h)		;1f78	fd 34 02	. 4 .
	set 2,(hl)		;1f7b	cb d6		. .
	ld a,(0ddb5h)		;1f7d	3a b5 dd	: . .
	ld d,000h		;1f80	16 00		. .
	or a			;1f82	b7		.
	jp p,l1f87h		;1f83	f2 87 1f	. . .
	dec d			;1f86	15		.
l1f87h:
	ld e,a			;1f87	5f		_
	ld hl,(0ddaah)		;1f88	2a aa dd	* . .
	inc hl			;1f8b	23		#
	inc hl			;1f8c	23		#
	add hl,de		;1f8d	19		.
	ld e,l			;1f8e	5d		]
	ld d,h			;1f8f	54		T
l1f90h:
	set 6,(iy+001h)		;1f90	fd cb 01 f6	. . . .
	call FindBreakpoint	;1f94	cd f5 1f	. . .
	jp c,l1ee2h		;1f97	da e2 1e	. . .
l1f9ah:
	ld b,0fch		;1f9a	06 fc		. .
	bit 0,(iy+005h)		;1f9c	fd cb 05 46	. . . F
	jr nz,l1fa4h		;1fa0	20 02		  .
l1fa2h:
	ld b,0feh		;1fa2	06 fe		. .
l1fa4h:
	ld (ix+000h),b		;1fa4	dd 70 00	. p .
	inc ix			;1fa7	dd 23		. #
	ld b,e			;1fa9	43		C
	ld (ix+000h),b		;1faa	dd 70 00	. p .
	inc ix			;1fad	dd 23		. #
	ld b,d			;1faf	42		B
	ld (StepPc),de		;1fb0	ed 53 d0 dd	. S . .
	jp EditorRegKeys	;1fb4	c3 6a 1e	. j .
l1fb7h:
	ld e,000h		;1fb7	1e 00		. .
	ld b,0fch		;1fb9	06 fc		. .
	ld a,(0ddb4h)		;1fbb	3a b4 dd	: . .
	cp 046h			;1fbe	fe 46		. F
	jr z,l1fa4h		;1fc0	28 e2		( .
	inc e			;1fc2	1c		.
	cp 056h			;1fc3	fe 56		. V
	jr z,l1fa4h		;1fc5	28 dd		( .
	inc e			;1fc7	1c		.
	jr l1fa4h		;1fc8	18 da		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WordToHexBuf: format HL as 4 hex characters (bit-7
; terminated) in the work buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WordToHexBuf:
	ld (ix+000h),b		;1fca	dd 70 00	. p .
	ld (ix+001h),04ch	;1fcd	dd 36 01 4c	. 6 . L
	inc ix			;1fd1	dd 23		. #
	inc ix			;1fd3	dd 23		. #
	ld c,004h		;1fd5	0e 04		. .
l1fd7h:
	ld l,004h		;1fd7	2e 04		. .
	xor a			;1fd9	af		.
l1fdah:
	sla e			;1fda	cb 23		. #
	rl d			;1fdc	cb 12		. .
	rla			;1fde	17		.
	dec l			;1fdf	2d		-
	jr nz,l1fdah		;1fe0	20 f8		  .
	add a,090h		;1fe2	c6 90		. .
	daa			;1fe4	27		'
	adc a,040h		;1fe5	ce 40		. @
	daa			;1fe7	27		'
	ld (ix+000h),a		;1fe8	dd 77 00	. w .
	inc ix			;1feb	dd 23		. #
	dec c			;1fed	0d		.
	jr nz,l1fd7h		;1fee	20 e7		  .
	set 7,(ix-001h)		;1ff0	dd cb ff fe	. . . .
	ret			;1ff4	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FindBreakpoint: search BreakTable (DDD6, count at
; BreakCount DDD4) for an address.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FindBreakpoint:
	or a			;1ff5	b7		.
	bit 4,(iy+000h)		;1ff6	fd cb 00 66	. . . f
	ret z			;1ffa	c8		.
	ld bc,(BreakCount)	;1ffb	ed 4b d4 dd	. K . .
	ld hl,(BreakTable)	;1fff	2a d6 dd	* . .
l2002h:
	ld a,b			;2002	78		x
	or c			;2003	b1		.
	ret z			;2004	c8		.
	ld a,(hl)		;2005	7e		~
	inc hl			;2006	23		#
	cp e			;2007	bb		.
	jr nz,l200eh		;2008	20 04		  .
	ld a,(hl)		;200a	7e		~
	cp d			;200b	ba		.
	scf			;200c	37		7
	ret z			;200d	c8		.
l200eh:
	inc hl			;200e	23		#
	dec bc			;200f	0b		.
	jr l2002h		;2010	18 f0		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MnemonicTable: packed mnemonic strings; placeholder
; characters carry bit 7 and mark operand splice points.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MnemonicTable:

; BLOCK 'MnemonicTable' (start 0x2012 end 0x2422)
	defb 05ch		;2012	5c		\
	defb 081h		;2013	81		.
	defb 05ch		;2014	5c		\
	defb 082h		;2015	82		.
	defb 05ch		;2016	5c		\
	defb 083h		;2017	83		.
	defb 05ch		;2018	5c		\
	defb 084h		;2019	84		.
	defb 053h		;201a	53		S
	defb 095h		;201b	95		.
	defb 053h		;201c	53		S
	defb 094h		;201d	94		.
	defb 053h		;201e	53		S
	defb 08dh		;201f	8d		.
l2020h:
	defb 053h		;2020	53		S
	defb 096h		;2021	96		.
	defb 053h		;2022	53		S
	defb 097h		;2023	97		.
	defb 053h		;2024	53		S
	defb 09ah		;2025	9a		.
	defb 053h		;2026	53		S
	defb 099h		;2027	99		.
	defb 053h		;2028	53		S
	defb 098h		;2029	98		.
	defb 053h		;202a	53		S
	defb 0a2h		;202b	a2		.
	defb 053h		;202c	53		S
	defb 0a3h		;202d	a3		.
	defb 053h		;202e	53		S
	defb 0a4h		;202f	a4		.
	defb 054h		;2030	54		T
	defb 095h		;2031	95		.
	defb 054h		;2032	54		T
	defb 094h		;2033	94		.
	defb 054h		;2034	54		T
	defb 08dh		;2035	8d		.
	defb 054h		;2036	54		T
	defb 096h		;2037	96		.
	defb 055h		;2038	55		U
	defb 095h		;2039	95		.
	defb 055h		;203a	55		U
	defb 094h		;203b	94		.
	defb 055h		;203c	55		U
	defb 08dh		;203d	8d		.
	defb 055h		;203e	55		U
	defb 096h		;203f	96		.
	defb 055h		;2040	55		U
	defb 097h		;2041	97		.
	defb 055h		;2042	55		U
	defb 09ah		;2043	9a		.
	defb 055h		;2044	55		U
	defb 099h		;2045	99		.
	defb 055h		;2046	55		U
	defb 098h		;2047	98		.
	defb 056h		;2048	56		V
	defb 08bh		;2049	8b		.
	defb 056h		;204a	56		V
	defb 087h		;204b	87		.
	defb 057h		;204c	57		W
	defb 08bh		;204d	8b		.
	defb 057h		;204e	57		W
	defb 087h		;204f	87		.
	defb 057h		;2050	57		W
	defb 092h		;2051	92		.
	defb 057h		;2052	57		W
	defb 093h		;2053	93		.
	defb 058h		;2054	58		X
	defb 08bh		;2055	8b		.
	defb 058h		;2056	58		X
	defb 087h		;2057	87		.
	defb 059h		;2058	59		Y
	defb 08bh		;2059	8b		.
	defb 059h		;205a	59		Y
	defb 08ch		;205b	8c		.
	defb 059h		;205c	59		Y
	defb 08dh		;205d	8d		.
	defb 059h		;205e	59		Y
	defb 08eh		;205f	8e		.
	defb 059h		;2060	59		Y
	defb 08fh		;2061	8f		.
	defb 059h		;2062	59		Y
	defb 090h		;2063	90		.
	defb 059h		;2064	59		Y
	defb 091h		;2065	91		.
	defb 05ah		;2066	5a		Z
	defb 0a6h		;2067	a6		.
	defb 05ah		;2068	5a		Z
	defb 0abh		;2069	ab		.
	defb 05bh		;206a	5b		[
	defb 086h		;206b	86		.
	defb 05bh		;206c	5b		[
	defb 08ah		;206d	8a		.
	defb 05bh		;206e	5b		[
	defb 0a5h		;206f	a5		.
	defb 05ch		;2070	5c		\
	defb 0a0h		;2071	a0		.
	defb 05ch		;2072	5c		\
	defb 0a1h		;2073	a1		.
	defb 05ch		;2074	5c		\
	defb 0a2h		;2075	a2		.
	defb 05ch		;2076	5c		\
	defb 0a9h		;2077	a9		.
	defb 05ch		;2078	5c		\
	defb 0aah		;2079	aa		.
	defb 05ch		;207a	5c		\
	defb 0a7h		;207b	a7		.
	defb 05ch		;207c	5c		\
	defb 0a8h		;207d	a8		.
	defb 05ch		;207e	5c		\
	defb 0abh		;207f	ab		.
	defb 05ch		;2080	5c		\
	defb 08bh		;2081	8b		.
	defb 05ch		;2082	5c		\
	defb 08ch		;2083	8c		.
	defb 05ch		;2084	5c		\
	defb 08dh		;2085	8d		.
	defb 05ch		;2086	5c		\
	defb 08eh		;2087	8e		.
	defb 05ch		;2088	5c		\
	defb 08fh		;2089	8f		.
	defb 05ch		;208a	5c		\
	defb 090h		;208b	90		.
	defb 05ch		;208c	5c		\
	defb 091h		;208d	91		.
	defb 05ch		;208e	5c		\
	defb 085h		;208f	85		.
	defb 05ch		;2090	5c		\
	defb 086h		;2091	86		.
	defb 05ch		;2092	5c		\
	defb 087h		;2093	87		.
	defb 05ch		;2094	5c		\
	defb 093h		;2095	93		.
	defb 05ch		;2096	5c		\
	defb 092h		;2097	92		.
	defb 05ch		;2098	5c		\
	defb 09bh		;2099	9b		.
	defb 05ch		;209a	5c		\
	defb 09ch		;209b	9c		.
	defb 05ch		;209c	5c		\
	defb 088h		;209d	88		.
	defb 000h		;209e	00		.
l209fh:
	defb 049h		;209f	49		I
	defb 058h		;20a0	58		X
	defb 0c8h		;20a1	c8		.
	defb 049h		;20a2	49		I
	defb 058h		;20a3	58		X
	defb 0cch		;20a4	cc		.
	defb 049h		;20a5	49		I
	defb 059h		;20a6	59		Y
	defb 0c8h		;20a7	c8		.
	defb 049h		;20a8	49		I
	defb 059h		;20a9	59		Y
	defb 0cch		;20aa	cc		.
	defb 042h		;20ab	42		B
	defb 0c3h		;20ac	c3		.
	defb 044h		;20ad	44		D
	defb 0c5h		;20ae	c5		.
	defb 048h		;20af	48		H
	defb 0cch		;20b0	cc		.
	defb 053h		;20b1	53		S
	defb 0d0h		;20b2	d0		.
	defb 041h		;20b3	41		A
	defb 046h		;20b4	46		F
	defb 0a7h		;20b5	a7		.
	defb 041h		;20b6	41		A
	defb 0c6h		;20b7	c6		.
	defb 0c1h		;20b8	c1		.
	defb 0c2h		;20b9	c2		.
	defb 0c3h		;20ba	c3		.
	defb 0c4h		;20bb	c4		.
	defb 0c5h		;20bc	c5		.
	defb 0c8h		;20bd	c8		.
	defb 0cch		;20be	cc		.
	defb 049h		;20bf	49		I
	defb 0d8h		;20c0	d8		.
	defb 049h		;20c1	49		I
	defb 0d9h		;20c2	d9		.
	defb 04eh		;20c3	4e		N
	defb 0dah		;20c4	da		.
	defb 0dah		;20c5	da		.
	defb 04eh		;20c6	4e		N
	defb 0c3h		;20c7	c3		.
	defb 0cdh		;20c8	cd		.
	defb 050h		;20c9	50		P
	defb 0cfh		;20ca	cf		.
	defb 050h		;20cb	50		P
	defb 0c5h		;20cc	c5		.
	defb 0d0h		;20cd	d0		.
	defb 0c9h		;20ce	c9		.
	defb 0d2h		;20cf	d2		.
	defb 0c6h		;20d0	c6		.
	defb 080h		;20d1	80		.
	defb 080h		;20d2	80		.
	defb 028h		;20d3	28		(
	defb 042h		;20d4	42		B
	defb 043h		;20d5	43		C
	defb 0a9h		;20d6	a9		.
	defb 028h		;20d7	28		(
	defb 044h		;20d8	44		D
	defb 045h		;20d9	45		E
	defb 0a9h		;20da	a9		.
	defb 028h		;20db	28		(
	defb 048h		;20dc	48		H
	defb 04ch		;20dd	4c		L
	defb 0a9h		;20de	a9		.
	defb 028h		;20df	28		(
	defb 049h		;20e0	49		I
	defb 058h		;20e1	58		X
	defb 0a9h		;20e2	a9		.
	defb 028h		;20e3	28		(
	defb 049h		;20e4	49		I
	defb 059h		;20e5	59		Y
	defb 0a9h		;20e6	a9		.
	defb 028h		;20e7	28		(
	defb 053h		;20e8	53		S
	defb 050h		;20e9	50		P
	defb 0a9h		;20ea	a9		.
	defb 028h		;20eb	28		(
	defb 043h		;20ec	43		C
	defb 0a9h		;20ed	a9		.
	defb 028h		;20ee	28		(
	defb 049h		;20ef	49		I
	defb 058h		;20f0	58		X
	defb 0abh		;20f1	ab		.
	defb 028h		;20f2	28		(
	defb 049h		;20f3	49		I
	defb 058h		;20f4	58		X
	defb 0adh		;20f5	ad		.
	defb 028h		;20f6	28		(
	defb 049h		;20f7	49		I
	defb 059h		;20f8	59		Y
	defb 0abh		;20f9	ab		.
	defb 028h		;20fa	28		(
	defb 049h		;20fb	49		I
	defb 059h		;20fc	59		Y
	defb 0adh		;20fd	ad		.
	defb 0a8h		;20fe	a8		.
	defb 000h		;20ff	00		.
l2100h:
	defb 043h		;2100	43		C
	defb 043h		;2101	43		C
	defb 0c6h		;2102	c6		.
	defb 043h		;2103	43		C
	defb 050h		;2104	50		P
	defb 0cch		;2105	cc		.
	defb 044h		;2106	44		D
	defb 041h		;2107	41		A
	defb 0c1h		;2108	c1		.
	defb 044h		;2109	44		D
	defb 0c9h		;210a	c9		.
	defb 045h		;210b	45		E
	defb 0c9h		;210c	c9		.
	defb 045h		;210d	45		E
	defb 058h		;210e	58		X
	defb 0d8h		;210f	d8		.
	defb 048h		;2110	48		H
	defb 041h		;2111	41		A
	defb 04ch		;2112	4c		L
	defb 0d4h		;2113	d4		.
	defb 04eh		;2114	4e		N
	defb 04fh		;2115	4f		O
	defb 0d0h		;2116	d0		.
	defb 052h		;2117	52		R
	defb 04ch		;2118	4c		L
	defb 0c1h		;2119	c1		.
	defb 052h		;211a	52		R
	defb 04ch		;211b	4c		L
	defb 043h		;211c	43		C
	defb 0c1h		;211d	c1		.
	defb 052h		;211e	52		R
	defb 052h		;211f	52		R
l2120h:
	defb 0c1h		;2120	c1		.
	defb 052h		;2121	52		R
	defb 052h		;2122	52		R
	defb 043h		;2123	43		C
	defb 0c1h		;2124	c1		.
	defb 053h		;2125	53		S
	defb 043h		;2126	43		C
	defb 0c6h		;2127	c6		.
	defb 04ch		;2128	4c		L
	defb 044h		;2129	44		D
	defb 049h		;212a	49		I
	defb 0d2h		;212b	d2		.
	defb 04ch		;212c	4c		L
	defb 044h		;212d	44		D
	defb 044h		;212e	44		D
	defb 0d2h		;212f	d2		.
	defb 043h		;2130	43		C
	defb 050h		;2131	50		P
	defb 049h		;2132	49		I
	defb 0d2h		;2133	d2		.
	defb 043h		;2134	43		C
	defb 050h		;2135	50		P
	defb 044h		;2136	44		D
	defb 0d2h		;2137	d2		.
	defb 049h		;2138	49		I
	defb 04eh		;2139	4e		N
	defb 049h		;213a	49		I
	defb 0d2h		;213b	d2		.
	defb 049h		;213c	49		I
	defb 04eh		;213d	4e		N
	defb 044h		;213e	44		D
	defb 0d2h		;213f	d2		.
	defb 04fh		;2140	4f		O
	defb 054h		;2141	54		T
	defb 049h		;2142	49		I
	defb 0d2h		;2143	d2		.
	defb 04fh		;2144	4f		O
	defb 054h		;2145	54		T
	defb 044h		;2146	44		D
	defb 0d2h		;2147	d2		.
	defb 04ch		;2148	4c		L
	defb 044h		;2149	44		D
	defb 0c9h		;214a	c9		.
	defb 04ch		;214b	4c		L
	defb 044h		;214c	44		D
	defb 0c4h		;214d	c4		.
	defb 043h		;214e	43		C
	defb 050h		;214f	50		P
	defb 0c9h		;2150	c9		.
	defb 043h		;2151	43		C
	defb 050h		;2152	50		P
	defb 0c4h		;2153	c4		.
	defb 049h		;2154	49		I
	defb 04eh		;2155	4e		N
	defb 0c9h		;2156	c9		.
	defb 049h		;2157	49		I
	defb 04eh		;2158	4e		N
	defb 0c4h		;2159	c4		.
	defb 04fh		;215a	4f		O
	defb 055h		;215b	55		U
	defb 054h		;215c	54		T
	defb 0c9h		;215d	c9		.
	defb 04fh		;215e	4f		O
	defb 055h		;215f	55		U
	defb 054h		;2160	54		T
	defb 0c4h		;2161	c4		.
	defb 052h		;2162	52		R
	defb 04ch		;2163	4c		L
	defb 0c4h		;2164	c4		.
	defb 052h		;2165	52		R
	defb 052h		;2166	52		R
	defb 0c4h		;2167	c4		.
	defb 052h		;2168	52		R
	defb 045h		;2169	45		E
	defb 054h		;216a	54		T
	defb 0c9h		;216b	c9		.
	defb 052h		;216c	52		R
	defb 045h		;216d	45		E
	defb 054h		;216e	54		T
	defb 0ceh		;216f	ce		.
	defb 04eh		;2170	4e		N
	defb 045h		;2171	45		E
	defb 0c7h		;2172	c7		.
	defb 080h		;2173	80		.
	defb 080h		;2174	80		.
	defb 080h		;2175	80		.
	defb 044h		;2176	44		D
	defb 045h		;2177	45		E
	defb 0c3h		;2178	c3		.
	defb 049h		;2179	49		I
	defb 04eh		;217a	4e		N
	defb 0c3h		;217b	c3		.
	defb 04ah		;217c	4a		J
	defb 0d0h		;217d	d0		.
	defb 04ah		;217e	4a		J
	defb 0d2h		;217f	d2		.
	defb 043h		;2180	43		C
	defb 041h		;2181	41		A
	defb 04ch		;2182	4c		L
	defb 0cch		;2183	cc		.
	defb 041h		;2184	41		A
	defb 044h		;2185	44		D
	defb 0c3h		;2186	c3		.
	defb 041h		;2187	41		A
	defb 044h		;2188	44		D
	defb 0c4h		;2189	c4		.
	defb 053h		;218a	53		S
	defb 042h		;218b	42		B
	defb 0c3h		;218c	c3		.
	defb 049h		;218d	49		I
	defb 0ceh		;218e	ce		.
	defb 04fh		;218f	4f		O
	defb 055h		;2190	55		U
	defb 0d4h		;2191	d4		.
	defb 045h		;2192	45		E
	defb 0d8h		;2193	d8		.
	defb 04ch		;2194	4c		L
	defb 0c4h		;2195	c4		.
	defb 04fh		;2196	4f		O
	defb 052h		;2197	52		R
	defb 0c7h		;2198	c7		.
	defb 041h		;2199	41		A
	defb 04eh		;219a	4e		N
	defb 0c4h		;219b	c4		.
	defb 04fh		;219c	4f		O
	defb 0d2h		;219d	d2		.
	defb 058h		;219e	58		X
	defb 04fh		;219f	4f		O
	defb 0d2h		;21a0	d2		.
	defb 053h		;21a1	53		S
	defb 055h		;21a2	55		U
	defb 0c2h		;21a3	c2		.
	defb 043h		;21a4	43		C
	defb 0d0h		;21a5	d0		.
	defb 050h		;21a6	50		P
	defb 055h		;21a7	55		U
	defb 053h		;21a8	53		S
	defb 0c8h		;21a9	c8		.
	defb 050h		;21aa	50		P
	defb 04fh		;21ab	4f		O
	defb 0d0h		;21ac	d0		.
	defb 044h		;21ad	44		D
	defb 04ah		;21ae	4a		J
	defb 04eh		;21af	4e		N
	defb 0dah		;21b0	da		.
	defb 052h		;21b1	52		R
	defb 045h		;21b2	45		E
	defb 0d4h		;21b3	d4		.
	defb 042h		;21b4	42		B
	defb 049h		;21b5	49		I
	defb 0d4h		;21b6	d4		.
	defb 053h		;21b7	53		S
	defb 045h		;21b8	45		E
	defb 0d4h		;21b9	d4		.
	defb 052h		;21ba	52		R
	defb 045h		;21bb	45		E
	defb 0d3h		;21bc	d3		.
	defb 052h		;21bd	52		R
	defb 04ch		;21be	4c		L
	defb 0c3h		;21bf	c3		.
	defb 052h		;21c0	52		R
	defb 0cch		;21c1	cc		.
	defb 052h		;21c2	52		R
	defb 052h		;21c3	52		R
	defb 0c3h		;21c4	c3		.
	defb 052h		;21c5	52		R
	defb 0d2h		;21c6	d2		.
	defb 053h		;21c7	53		S
	defb 04ch		;21c8	4c		L
	defb 0c1h		;21c9	c1		.
	defb 053h		;21ca	53		S
	defb 052h		;21cb	52		R
	defb 0c1h		;21cc	c1		.
	defb 053h		;21cd	53		S
	defb 052h		;21ce	52		R
	defb 0cch		;21cf	cc		.
	defb 049h		;21d0	49		I
	defb 0cdh		;21d1	cd		.
	defb 052h		;21d2	52		R
	defb 053h		;21d3	53		S
	defb 0d4h		;21d4	d4		.
	defb 044h		;21d5	44		D
	defb 0c2h		;21d6	c2		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; OpcodeClassTable: class/length/control-flow bytes for every
; opcode (including the xxCB pages) - input to
; AnalyzeInstruction.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
OpcodeClassTable:
	defb 053h		;21d7	53		S
	defb 04ch		;21d8	4c		L
	defb 0c9h		;21d9	c9		.
	defb 000h		;21da	00		.
	defb 007h		;21db	07		.
	defb 001h		;21dc	01		.
	defb 067h		;21dd	67		g
	defb 022h		;21de	22		"
	defb 038h		;21df	38		8
	defb 001h		;21e0	01		.
	defb 067h		;21e1	67		g
	defb 022h		;21e2	22		"
	defb 038h		;21e3	38		8
	defb 001h		;21e4	01		.
	defb 06fh		;21e5	6f		o
	defb 022h		;21e6	22		"
	defb 030h		;21e7	30		0
	defb 001h		;21e8	01		.
	defb 077h		;21e9	77		w
	defb 022h		;21ea	22		"
	defb 030h		;21eb	30		0
	defb 001h		;21ec	01		.
	defb 07bh		;21ed	7b		{
	defb 022h		;21ee	22		"
	defb 038h		;21ef	38		8
	defb 001h		;21f0	01		.
	defb 07fh		;21f1	7f		.
	defb 022h		;21f2	22		"
	defb 038h		;21f3	38		8
	defb 001h		;21f4	01		.
	defb 087h		;21f5	87		.
	defb 022h		;21f6	22		"
	defb 038h		;21f7	38		8
	defb 001h		;21f8	01		.
	defb 08fh		;21f9	8f		.
	defb 022h		;21fa	22		"
	defb 0c0h		;21fb	c0		.
	defb 002h		;21fc	02		.
	defb 097h		;21fd	97		.
	defb 022h		;21fe	22		"
	defb 038h		;21ff	38		8
	defb 001h		;2200	01		.
	defb 09fh		;2201	9f		.
	defb 022h		;2202	22		"
	defb 038h		;2203	38		8
	defb 001h		;2204	01		.
	defb 0a7h		;2205	a7		.
	defb 022h		;2206	22		"
	defb 00fh		;2207	0f		.
	defb 002h		;2208	02		.
	defb 0afh		;2209	af		.
	defb 022h		;220a	22		"
	defb 038h		;220b	38		8
	defb 002h		;220c	02		.
	defb 0cfh		;220d	cf		.
	defb 022h		;220e	22		"
	defb 038h		;220f	38		8
	defb 001h		;2210	01		.
	defb 0dfh		;2211	df		.
	defb 022h		;2212	22		"
	defb 030h		;2213	30		0
	defb 001h		;2214	01		.
	defb 0e7h		;2215	e7		.
	defb 022h		;2216	22		"
	defb 030h		;2217	30		0
	defb 002h		;2218	02		.
	defb 0ebh		;2219	eb		.
	defb 022h		;221a	22		"
	defb 030h		;221b	30		0
	defb 002h		;221c	02		.
	defb 0f3h		;221d	f3		.
	defb 022h		;221e	22		"
	defb 00fh		;221f	0f		.
	defb 002h		;2220	02		.
	defb 0fbh		;2221	fb		.
	defb 022h		;2222	22		"
	defb 038h		;2223	38		8
	defb 002h		;2224	02		.
	defb 01bh		;2225	1b		.
	defb 023h		;2226	23		#
	defb 030h		;2227	30		0
	defb 002h		;2228	02		.
	defb 02bh		;2229	2b		+
	defb 023h		;222a	23		#
	defb 030h		;222b	30		0
	defb 002h		;222c	02		.
	defb 033h		;222d	33		3
	defb 023h		;222e	23		#
	defb 0c0h		;222f	c0		.
	defb 002h		;2230	02		.
	defb 03bh		;2231	3b		;
	defb 023h		;2232	23		#
	defb 038h		;2233	38		8
	defb 001h		;2234	01		.
	defb 043h		;2235	43		C
	defb 023h		;2236	23		#
	defb 0e0h		;2237	e0		.
	defb 001h		;2238	01		.
	defb 04bh		;2239	4b		K
	defb 023h		;223a	23		#
	defb 00fh		;223b	0f		.
	defb 002h		;223c	02		.
	defb 053h		;223d	53		S
	defb 023h		;223e	23		#
	defb 007h		;223f	07		.
	defb 001h		;2240	01		.
	defb 073h		;2241	73		s
	defb 023h		;2242	23		#
	defb 038h		;2243	38		8
	defb 001h		;2244	01		.
	defb 07bh		;2245	7b		{
	defb 023h		;2246	23		#
	defb 038h		;2247	38		8
	defb 001h		;2248	01		.
	defb 083h		;2249	83		.
	defb 023h		;224a	23		#
	defb 038h		;224b	38		8
	defb 001h		;224c	01		.
	defb 08bh		;224d	8b		.
	defb 023h		;224e	23		#
	defb 038h		;224f	38		8
	defb 001h		;2250	01		.
	defb 093h		;2251	93		.
	defb 023h		;2252	23		#
	defb 038h		;2253	38		8
	defb 002h		;2254	02		.
	defb 09bh		;2255	9b		.
	defb 023h		;2256	23		#
	defb 018h		;2257	18		.
	defb 001h		;2258	01		.
	defb 0abh		;2259	ab		.
	defb 023h		;225a	23		#
	defb 018h		;225b	18		.
	defb 001h		;225c	01		.
	defb 0afh		;225d	af		.
	defb 023h		;225e	23		#
	defb 018h		;225f	18		.
	defb 001h		;2260	01		.
	defb 0b3h		;2261	b3		.
	defb 023h		;2262	23		#
	defb 018h		;2263	18		.
	defb 001h		;2264	01		.
	defb 0b7h		;2265	b7		.
	defb 023h		;2266	23		#
	defb 00ch		;2267	0c		.
	defb 00dh		;2268	0d		.
	defb 00eh		;2269	0e		.
	defb 00fh		;226a	0f		.
	defb 010h		;226b	10		.
	defb 011h		;226c	11		.
	defb 022h		;226d	22		"
	defb 00bh		;226e	0b		.
	defb 00ch		;226f	0c		.
	defb 00dh		;2270	0d		.
	defb 00eh		;2271	0e		.
	defb 00fh		;2272	0f		.
	defb 010h		;2273	10		.
	defb 011h		;2274	11		.
	defb 0eah		;2275	ea		.
	defb 00bh		;2276	0b		.
	defb 005h		;2277	05		.
	defb 006h		;2278	06		.
	defb 007h		;2279	07		.
	defb 008h		;227a	08		.
	defb 005h		;227b	05		.
	defb 006h		;227c	06		.
	defb 007h		;227d	07		.
	defb 00ah		;227e	0a		.
	defb 014h		;227f	14		.
	defb 015h		;2280	15		.
	defb 016h		;2281	16		.
	defb 00dh		;2282	0d		.
	defb 018h		;2283	18		.
	defb 019h		;2284	19		.
	defb 01ah		;2285	1a		.
	defb 017h		;2286	17		.
	defb 083h		;2287	83		.
	defb 082h		;2288	82		.
	defb 085h		;2289	85		.
	defb 084h		;228a	84		.
	defb 089h		;228b	89		.
	defb 088h		;228c	88		.
	defb 087h		;228d	87		.
	defb 086h		;228e	86		.
	defb 092h		;228f	92		.
	defb 091h		;2290	91		.
	defb 094h		;2291	94		.
	defb 093h		;2292	93		.
	defb 098h		;2293	98		.
	defb 097h		;2294	97		.
	defb 096h		;2295	96		.
	defb 095h		;2296	95		.
	defb 0d2h		;2297	d2		.
	defb 000h		;2298	00		.
	defb 0d0h		;2299	d0		.
	defb 0c7h		;229a	c7		.
	defb 0d1h		;229b	d1		.
	defb 0c7h		;229c	c7		.
	defb 0d8h		;229d	d8		.
	defb 000h		;229e	00		.
	defb 0b6h		;229f	b6		.
	defb 0b7h		;22a0	b7		.
	defb 0b8h		;22a1	b8		.
	defb 0b9h		;22a2	b9		.
	defb 0bah		;22a3	ba		.
	defb 0bbh		;22a4	bb		.
	defb 0afh		;22a5	af		.
	defb 0b5h		;22a6	b5		.
	defb 09bh		;22a7	9b		.
	defb 099h		;22a8	99		.
	defb 061h		;22a9	61		a
	defb 09fh		;22aa	9f		.
	defb 05eh		;22ab	5e		^
	defb 060h		;22ac	60		`
	defb 05fh		;22ad	5f		_
	defb 062h		;22ae	62		b
	defb 0d3h		;22af	d3		.
	defb 000h		;22b0	00		.
	defb 0d5h		;22b1	d5		.
	defb 0ebh		;22b2	eb		.
	defb 0d6h		;22b3	d6		.
	defb 000h		;22b4	00		.
	defb 052h		;22b5	52		R
	defb 0cah		;22b6	ca		.
	defb 052h		;22b7	52		R
	defb 0c8h		;22b8	c8		.
	defb 051h		;22b9	51		Q
	defb 0c8h		;22ba	c8		.
	defb 0d0h		;22bb	d0		.
	defb 0ech		;22bc	ec		.
	defb 0d4h		;22bd	d4		.
	defb 000h		;22be	00		.
	defb 0d3h		;22bf	d3		.
	defb 000h		;22c0	00		.
	defb 09ch		;22c1	9c		.
	defb 0cah		;22c2	ca		.
	defb 0d7h		;22c3	d7		.
	defb 000h		;22c4	00		.
	defb 051h		;22c5	51		Q
	defb 0cah		;22c6	ca		.
	defb 052h		;22c7	52		R
	defb 0c8h		;22c8	c8		.
	defb 051h		;22c9	51		Q
	defb 0c8h		;22ca	c8		.
	defb 0d0h		;22cb	d0		.
	defb 0ech		;22cc	ec		.
	defb 0d4h		;22cd	d4		.
	defb 000h		;22ce	00		.
	defb 033h		;22cf	33		3
	defb 000h		;22d0	00		.
	defb 0abh		;22d1	ab		.
	defb 009h		;22d2	09		.
	defb 065h		;22d3	65		e
	defb 0edh		;22d4	ed		.
	defb 054h		;22d5	54		T
	defb 0edh		;22d6	ed		.
	defb 08eh		;22d7	8e		.
	defb 0edh		;22d8	ed		.
	defb 08dh		;22d9	8d		.
	defb 0edh		;22da	ed		.
	defb 090h		;22db	90		.
	defb 0edh		;22dc	ed		.
	defb 08fh		;22dd	8f		.
	defb 0edh		;22de	ed		.
	defb 035h		;22df	35		5
	defb 037h		;22e0	37		7
	defb 034h		;22e1	34		4
	defb 036h		;22e2	36		6
	defb 02eh		;22e3	2e		.
	defb 02dh		;22e4	2d		-
	defb 038h		;22e5	38		8
	defb 02ch		;22e6	2c		,
	defb 0bch		;22e7	bc		.
	defb 0bdh		;22e8	bd		.
	defb 0beh		;22e9	be		.
	defb 0c3h		;22ea	c3		.
	defb 0adh		;22eb	ad		.
	defb 00bh		;22ec	0b		.
	defb 0aeh		;22ed	ae		.
	defb 00bh		;22ee	0b		.
	defb 0b4h		;22ef	b4		.
	defb 007h		;22f0	07		.
	defb 0b4h		;22f1	b4		.
	defb 00bh		;22f2	0b		.
	defb 0b5h		;22f3	b5		.
	defb 020h		;22f4	20		 
	defb 0b5h		;22f5	b5		.
	defb 021h		;22f6	21		!
	defb 0beh		;22f7	be		.
	defb 02bh		;22f8	2b		+
	defb 0b5h		;22f9	b5		.
	defb 02bh		;22fa	2b		+
	defb 066h		;22fb	66		f
	defb 0cch		;22fc	cc		.
	defb 064h		;22fd	64		d
	defb 0cbh		;22fe	cb		.
	defb 0cdh		;22ff	cd		.
	defb 0ebh		;2300	eb		.
	defb 0d9h		;2301	d9		.
l2302h:
	defb 000h		;2302	00		.
	defb 0ceh		;2303	ce		.
	defb 0ebh		;2304	eb		.
	defb 063h		;2305	63		c
	defb 0cbh		;2306	cb		.
	defb 0d1h		;2307	d1		.
	defb 0ech		;2308	ec		.
	defb 072h		;2309	72		r
	defb 000h		;230a	00		.
	defb 066h		;230b	66		f
	defb 0cch		;230c	cc		.
	defb 0dah		;230d	da		.
	defb 000h		;230e	00		.
	defb 0cdh		;230f	cd		.
	defb 0ebh		;2310	eb		.
	defb 0d9h		;2311	d9		.
	defb 000h		;2312	00		.
	defb 0ceh		;2313	ce		.
	defb 0ebh		;2314	eb		.
	defb 0dbh		;2315	db		.
	defb 000h		;2316	00		.
	defb 0d1h		;2317	d1		.
	defb 0ech		;2318	ec		.
	defb 072h		;2319	72		r
	defb 000h		;231a	00		.
	defb 053h		;231b	53		S
	defb 0ebh		;231c	eb		.
	defb 0eah		;231d	ea		.
	defb 000h		;231e	00		.
	defb 0a9h		;231f	a9		.
	defb 00bh		;2320	0b		.
	defb 0a1h		;2321	a1		.
	defb 02bh		;2322	2b		+
	defb 0ach		;2323	ac		.
	defb 007h		;2324	07		.
	defb 0eah		;2325	ea		.
	defb 000h		;2326	00		.
	defb 02fh		;2327	2f		/
	defb 000h		;2328	00		.
	defb 030h		;2329	30		0
	defb 000h		;232a	00		.
	defb 066h		;232b	66		f
	defb 000h		;232c	00		.
	defb 031h		;232d	31		1
	defb 000h		;232e	00		.
	defb 08ah		;232f	8a		.
	defb 000h		;2330	00		.
	defb 0c3h		;2331	c3		.
	defb 007h		;2332	07		.
	defb 055h		;2333	55		U
	defb 0ebh		;2334	eb		.
	defb 0eah		;2335	ea		.
	defb 000h		;2336	00		.
	defb 0eah		;2337	ea		.
	defb 000h		;2338	00		.
	defb 0eah		;2339	ea		.
	defb 000h		;233a	00		.
	defb 0ddh		;233b	dd		.
	defb 0c7h		;233c	c7		.
	defb 067h		;233d	67		g
	defb 0c7h		;233e	c7		.
	defb 069h		;233f	69		i
	defb 0c7h		;2340	c7		.
	defb 068h		;2341	68		h
	defb 0c7h		;2342	c7		.
	defb 06ah		;2343	6a		j
	defb 06ch		;2344	6c		l
	defb 06bh		;2345	6b		k
	defb 06dh		;2346	6d		m
	defb 06eh		;2347	6e		n
	defb 06fh		;2348	6f		o
	defb 074h		;2349	74		t
	defb 070h		;234a	70		p
	defb 0eah		;234b	ea		.
	defb 0eah		;234c	ea		.
	defb 0dfh		;234d	df		.
	defb 0dfh		;234e	df		.
	defb 0eah		;234f	ea		.
	defb 0e0h		;2350	e0		.
	defb 0eah		;2351	ea		.
	defb 0eah		;2352	ea		.
	defb 0e1h		;2353	e1		.
	defb 026h		;2354	26		&
	defb 0a8h		;2355	a8		.
	defb 0c9h		;2356	c9		.
	defb 0a0h		;2357	a0		.
	defb 0cah		;2358	ca		.
	defb 0b4h		;2359	b4		.
	defb 0cah		;235a	ca		.
	defb 0e2h		;235b	e2		.
	defb 000h		;235c	00		.
	defb 0e3h		;235d	e3		.
	defb 000h		;235e	00		.
	defb 0e4h		;235f	e4		.
	defb 000h		;2360	00		.
	defb 0e5h		;2361	e5		.
	defb 000h		;2362	00		.
	defb 0e1h		;2363	e1		.
	defb 026h		;2364	26		&
	defb 0a8h		;2365	a8		.
	defb 0c9h		;2366	c9		.
	defb 09ah		;2367	9a		.
	defb 0cah		;2368	ca		.
	defb 0d5h		;2369	d5		.
	defb 02bh		;236a	2b		+
	defb 0e2h		;236b	e2		.
	defb 000h		;236c	00		.
	defb 0e3h		;236d	e3		.
	defb 000h		;236e	00		.
	defb 0e4h		;236f	e4		.
	defb 000h		;2370	00		.
	defb 0e5h		;2371	e5		.
	defb 000h		;2372	00		.
	defb 0e6h		;2373	e6		.
	defb 0e7h		;2374	e7		.
	defb 0e8h		;2375	e8		.
	defb 0e9h		;2376	e9		.
	defb 0eah		;2377	ea		.
	defb 0eah		;2378	ea		.
	defb 0eah		;2379	ea		.
	defb 0eah		;237a	ea		.
	defb 0a2h		;237b	a2		.
	defb 0a3h		;237c	a3		.
	defb 0a4h		;237d	a4		.
	defb 0a5h		;237e	a5		.
	defb 0a6h		;237f	a6		.
	defb 0a7h		;2380	a7		.
	defb 0eah		;2381	ea		.
	defb 0a1h		;2382	a1		.
	defb 04dh		;2383	4d		M
	defb 04eh		;2384	4e		N
	defb 04eh		;2385	4e		N
	defb 04eh		;2386	4e		N
	defb 04eh		;2387	4e		N
	defb 04eh		;2388	4e		N
	defb 04eh		;2389	4e		N
	defb 04eh		;238a	4e		N
	defb 04ch		;238b	4c		L
	defb 04bh		;238c	4b		K
	defb 0eah		;238d	ea		.
	defb 0eah		;238e	ea		.
	defb 0eah		;238f	ea		.
	defb 0eah		;2390	ea		.
	defb 0eah		;2391	ea		.
	defb 0eah		;2392	ea		.
	defb 071h		;2393	71		q
	defb 0eah		;2394	ea		.
	defb 071h		;2395	71		q
	defb 071h		;2396	71		q
	defb 0eah		;2397	ea		.
	defb 0eah		;2398	ea		.
	defb 0eah		;2399	ea		.
	defb 0eah		;239a	ea		.
	defb 0c1h		;239b	c1		.
	defb 00bh		;239c	0b		.
	defb 0c2h		;239d	c2		.
	defb 00bh		;239e	0b		.
	defb 0b5h		;239f	b5		.
	defb 01bh		;23a0	1b		.
	defb 0b5h		;23a1	b5		.
	defb 01ch		;23a2	1c		.
	defb 04ah		;23a3	4a		J
	defb 000h		;23a4	00		.
	defb 049h		;23a5	49		I
	defb 000h		;23a6	00		.
	defb 0eah		;23a7	ea		.
	defb 000h		;23a8	00		.
	defb 0eah		;23a9	ea		.
	defb 000h		;23aa	00		.
	defb 041h		;23ab	41		A
	defb 042h		;23ac	42		B
	defb 039h		;23ad	39		9
	defb 03ah		;23ae	3a		:
	defb 043h		;23af	43		C
	defb 02ch		;23b0	2c		,
	defb 03bh		;23b1	3b		;
	defb 03ch		;23b2	3c		<
	defb 045h		;23b3	45		E
	defb 046h		;23b4	46		F
	defb 03dh		;23b5	3d		=
	defb 03eh		;23b6	3e		>
	defb 047h		;23b7	47		G
	defb 048h		;23b8	48		H
	defb 03fh		;23b9	3f		?
	defb 040h		;23ba	40		@
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EditorKeyTable: key code -> editor action (see
; EditorKeyLoop $1D7A).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EditorKeyTable:
	defb 04eh		;23bb	4e		N
	defb 074h		;23bc	74		t
	defb 0bbh		;23bd	bb		.
	defb 0bah		;23be	ba		.
	defb 011h		;23bf	11		.
	defb 010h		;23c0	10		.
	defb 031h		;23c1	31		1
	defb 0abh		;23c2	ab		.
	defb 014h		;23c3	14		.
	defb 015h		;23c4	15		.
	defb 00dh		;23c5	0d		.
	defb 016h		;23c6	16		.
	defb 01ah		;23c7	1a		.
	defb 017h		;23c8	17		.
	defb 018h		;23c9	18		.
	defb 019h		;23ca	19		.
	defb 066h		;23cb	66		f
	defb 04ch		;23cc	4c		L
	defb 04bh		;23cd	4b		K
	defb 007h		;23ce	07		.
	defb 0beh		;23cf	be		.
	defb 08ah		;23d0	8a		.
	defb 09ch		;23d1	9c		.
	defb 022h		;23d2	22		"
	defb 0afh		;23d3	af		.
l23d4h:
	defb 071h		;23d4	71		q
	defb 072h		;23d5	72		r
	defb 067h		;23d6	67		g
	defb 068h		;23d7	68		h
	defb 069h		;23d8	69		i
	defb 02bh		;23d9	2b		+
	defb 0b4h		;23da	b4		.
	defb 0a9h		;23db	a9		.
	defb 0ech		;23dc	ec		.
	defb 0ebh		;23dd	eb		.
	defb 0edh		;23de	ed		.
	defb 0b2h		;23df	b2		.
	defb 027h		;23e0	27		'
	defb 09dh		;23e1	9d		.
	defb 08bh		;23e2	8b		.
	defb 0c0h		;23e3	c0		.
	defb 012h		;23e4	12		.
	defb 0b0h		;23e5	b0		.
	defb 029h		;23e6	29		)
	defb 09eh		;23e7	9e		.
	defb 08ch		;23e8	8c		.
	defb 0bfh		;23e9	bf		.
	defb 013h		;23ea	13		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegisterKeyTable: register-edit keys (see EditorRegKeys
; $1E6A).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RegisterKeyTable:
	defb 049h		;23eb	49		I
	defb 04ah		;23ec	4a		J
	defb 0b4h		;23ed	b4		.
	defb 0aeh		;23ee	ae		.
	defb 0adh		;23ef	ad		.
	defb 0afh		;23f0	af		.
	defb 02bh		;23f1	2b		+
	defb 021h		;23f2	21		!
	defb 020h		;23f3	20		 
	defb 022h		;23f4	22		"
l23f5h:
	defb 052h		;23f5	52		R
	defb 051h		;23f6	51		Q
	defb 06ah		;23f7	6a		j
	defb 06ch		;23f8	6c		l
	defb 06bh		;23f9	6b		k
	defb 06dh		;23fa	6d		m
	defb 06eh		;23fb	6e		n
	defb 06fh		;23fc	6f		o
	defb 070h		;23fd	70		p
	defb 001h		;23fe	01		.
	defb 002h		;23ff	02		.
	defb 007h		;2400	07		.
	defb 00ah		;2401	0a		.
	defb 00ch		;2402	0c		.
	defb 00dh		;2403	0d		.
	defb 01bh		;2404	1b		.
l2405h:
	defb 01eh		;2405	1e		.
	defb 0a1h		;2406	a1		.
	defb 027h		;2407	27		'
	defb 0c1h		;2408	c1		.
	defb 027h		;2409	27		'
	defb 051h		;240a	51		Q
	defb 027h		;240b	27		'
	defb 026h		;240c	26		&
	defb 027h		;240d	27		'
	defb 05bh		;240e	5b		[
	defb 026h		;240f	26		&
	defb 017h		;2410	17		.
	defb 027h		;2411	27		'
	defb 073h		;2412	73		s
	defb 027h		;2413	27		'
	defb 018h		;2414	18		.
	defb 026h		;2415	26		&
	defb 0e5h		;2416	e5		.
	defb 0d5h		;2417	d5		.
	defb 0c5h		;2418	c5		.
	defb 021h		;2419	21		!
	defb 0b3h		;241a	b3		.
	defb 026h		;241b	26		&
	defb 0e5h		;241c	e5		.
	defb 018h		;241d	18		.
	defb 003h		;241e	03		.
sub_241fh:
	defb 0cdh		;241f	cd		.
	defb 0b1h		;2420	b1		.
	defb 024h		;2421	24		$
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; GlyphAddr: address of the 6x8 glyph for A in Font6x8 ($FCA0),
; 8 bytes per character.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
GlyphAddr:
	sub 020h		;2422	d6 20		.  
	ld l,a			;2424	6f		o
	ld c,a			;2425	4f		O
	ld h,000h		;2426	26 00		& .
	add hl,hl		;2428	29		)
	add hl,hl		;2429	29		)
	add hl,hl		;242a	29		)
	ld de,Font6x8		;242b	11 a0 fc	. . .
	add hl,de		;242e	19		.
	ld a,(ix+00ah)		;242f	dd 7e 0a	. ~ .
	cp 008h			;2432	fe 08		. .
	jr nz,l246ah		;2434	20 34		  4
	push hl			;2436	e5		.
	call BitmapAddr		;2437	cd 79 25	. y %
	pop de			;243a	d1		.
	ld b,008h		;243b	06 08		. .
l243dh:
	ld a,(de)		;243d	1a		.
	ld (hl),a		;243e	77		w
	inc de			;243f	13		.
	inc h			;2440	24		$
	djnz l243dh		;2441	10 fa		. .
l2443h:
	bit 1,(ix+007h)		;2443	dd cb 07 4e	. . . N
	jr z,l2457h		;2447	28 0e		( .
	dec h			;2449	25		%
	ld a,h			;244a	7c		|
	rrca			;244b	0f		.
	rrca			;244c	0f		.
	rrca			;244d	0f		.
	and 003h		;244e	e6 03		. .
	xor 058h		;2450	ee 58		. X
	ld h,a			;2452	67		g
	ld a,(ix+006h)		;2453	dd 7e 06	. ~ .
	ld (hl),a		;2456	77		w
l2457h:
	bit 1,(iy+009h)		;2457	fd cb 09 4e	. . . N
	ret nz			;245b	c0		.
	inc (ix+001h)		;245c	dd 34 01	. 4 .
	ld a,(ix+00bh)		;245f	dd 7e 0b	. ~ .
	cp (ix+001h)		;2462	dd be 01	. . .
	ret nc			;2465	d0		.
	dec (ix+001h)		;2466	dd 35 01	. 5 .
	ret			;2469	c9		.
l246ah:
	push hl			;246a	e5		.
	call WindowAddr		;246b	cd 28 25	. ( %
	inc h			;246e	24		$
	ld de,l03ffh		;246f	11 ff 03	. . .
	ld a,(0e2d2h)		;2472	3a d2 e2	: . .
	or a			;2475	b7		.
	jr z,l2480h		;2476	28 08		( .
	ld b,a			;2478	47		G
l2479h:
	scf			;2479	37		7
	rr d			;247a	cb 1a		. .
	rr e			;247c	cb 1b		. .
	djnz l2479h		;247e	10 f9		. .
l2480h:
	ld (0e2bfh),de		;2480	ed 53 bf e2	. S . .
	ld a,007h		;2484	3e 07		> .
l2486h:
	ex af,af'		;2486	08		.
	ex (sp),hl		;2487	e3		.
	inc hl			;2488	23		#
	ld d,(hl)		;2489	56		V
	ex (sp),hl		;248a	e3		.
	ld e,000h		;248b	1e 00		. .
	ld a,(0e2d2h)		;248d	3a d2 e2	: . .
	or a			;2490	b7		.
	jr z,l249ah		;2491	28 07		( .
	ld b,a			;2493	47		G
l2494h:
	srl d			;2494	cb 3a		. :
	rr e			;2496	cb 1b		. .
	djnz l2494h		;2498	10 fa		. .
l249ah:
	ld bc,(0e2bfh)		;249a	ed 4b bf e2	. K . .
	ld a,(hl)		;249e	7e		~
	and b			;249f	a0		.
	or d			;24a0	b2		.
	ld (hl),a		;24a1	77		w
	inc hl			;24a2	23		#
	ld a,(hl)		;24a3	7e		~
	and c			;24a4	a1		.
	or e			;24a5	b3		.
	ld (hl),a		;24a6	77		w
	dec hl			;24a7	2b		+
	inc h			;24a8	24		$
	ex af,af'		;24a9	08		.
	dec a			;24aa	3d		=
	jr nz,l2486h		;24ab	20 d9		  .
	pop de			;24ad	d1		.
	jp l2443h		;24ae	c3 43 24	. C $
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SaveCursorCell (+restore): the cursor spans 2 glyphs of a
; 6-pixel cell; the 16 covered bytes are buffered at
; CursorCellSave (E2C1) and restored when the cursor moves.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SaveCursorCell:
	set 2,(iy+009h)		;24b1	fd cb 09 d6	. . . .
	bit 0,(iy+009h)		;24b5	fd cb 09 46	. . . F
	ret z			;24b9	c8		.
	res 0,(iy+009h)		;24ba	fd cb 09 86	. . . .
l24beh:
	push af			;24be	f5		.
	call WindowAddr		;24bf	cd 28 25	. ( %
	ld de,CursorCellSave	;24c2	11 c1 e2	. . .
	ld b,008h		;24c5	06 08		. .
l24c7h:
	ld c,002h		;24c7	0e 02		. .
l24c9h:
	bit 0,(iy+009h)		;24c9	fd cb 09 46	. . . F
	jr z,l24d3h		;24cd	28 04		( .
	ld a,(hl)		;24cf	7e		~
	ld (de),a		;24d0	12		.
	jr l24d5h		;24d1	18 02		. .
l24d3h:
	ld a,(de)		;24d3	1a		.
	ld (hl),a		;24d4	77		w
l24d5h:
	inc l			;24d5	2c		,
	inc de			;24d6	13		.
	dec c			;24d7	0d		.
	jr nz,l24c9h		;24d8	20 ef		  .
	dec l			;24da	2d		-
	dec l			;24db	2d		-
	inc h			;24dc	24		$
	djnz l24c7h		;24dd	10 e8		. .
	pop af			;24df	f1		.
	ret			;24e0	c9		.
RestartCursorBlink:
	ld a,001h		;24e1	3e 01		> .
	res 2,(iy+009h)		;24e3	fd cb 09 96	. . . .
	bit 0,(iy+009h)		;24e7	fd cb 09 46	. . . F
	ret nz			;24eb	c0		.
	ld (0e2d3h),a		;24ec	32 d3 e2	2 . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BlinkCursor: toggle cursor visibility every $7D frames
; (counter at E2D3); draw/erase goes through the cell buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BlinkCursor:
	ld ix,(WorkBufferPtr)	;24ef	dd 2a d4 e2	. * . .
	bit 0,(ix+007h)		;24f3	dd cb 07 46	. . . F
	ret nz			;24f7	c0		.
	ld hl,0dfe8h		;24f8	21 e8 df	! . .
	bit 2,(hl)		;24fb	cb 56		. V
	res 2,(hl)		;24fd	cb 96		. .
	ld hl,0e2d3h		;24ff	21 d3 e2	! . .
	jr nz,l2525h		;2502	20 21		  !
	dec (hl)		;2504	35		5
	ret nz			;2505	c0		.
	ld (hl),07dh		;2506	36 7d		6 }
	ld hl,0dfe8h		;2508	21 e8 df	! . .
	bit 0,(hl)		;250b	cb 46		. F
	res 0,(hl)		;250d	cb 86		. .
	jr nz,l24beh		;250f	20 ad		  .
	set 0,(hl)		;2511	cb c6		. .
	push hl			;2513	e5		.
	call l24beh		;2514	cd be 24	. . $
	pop hl			;2517	e1		.
	ld a,(ix+00ch)		;2518	dd 7e 0c	. ~ .
	set 1,(hl)		;251b	cb ce		. .
	push hl			;251d	e5		.
	call GlyphAddr	;251e	cd 22 24	. " $
	pop hl			;2521	e1		.
	res 1,(hl)		;2522	cb 8e		. .
	ret			;2524	c9		.
l2525h:
	ld (hl),00ah		;2525	36 0a		6 .
	ret			;2527	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WindowAddr: window cursor (IX+0 row, IX+1 column) ->
; absolute screen address.  The 6-pixel font packs two glyphs
; per 8-pixel byte cell, so columns address 6-pixel units.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WindowAddr:
	ld a,(ix+002h)		;2528	dd 7e 02	. ~ .
	add a,(ix+000h)		;252b	dd 86 00	. . .
	ld h,a			;252e	67		g
	rrca			;252f	0f		.
	rrca			;2530	0f		.
	rrca			;2531	0f		.
	and 0e0h		;2532	e6 e0		. .
	ld c,a			;2534	4f		O
	ld l,(ix+001h)		;2535	dd 6e 01	. n .
	ld a,l			;2538	7d		}
	add a,l			;2539	85		.
	add a,l			;253a	85		.
	add a,l			;253b	85		.
	add a,l			;253c	85		.
	add a,l			;253d	85		.
	ld l,a			;253e	6f		o
	and 007h		;253f	e6 07		. .
	ld (0e2d2h),a		;2541	32 d2 e2	2 . .
	ld a,l			;2544	7d		}
	rrca			;2545	0f		.
	rrca			;2546	0f		.
	rrca			;2547	0f		.
	add a,(ix+003h)		;2548	dd 86 03	. . .
	and 01fh		;254b	e6 1f		. .
	or c			;254d	b1		.
	ld l,a			;254e	6f		o
	ld a,h			;254f	7c		|
	and 018h		;2550	e6 18		. .
	xor 040h		;2552	ee 40		. @
	ld h,a			;2554	67		g
	ret			;2555	c9		.
sub_2556h:
	ld l,(ix+000h)		;2556	dd 6e 00	. n .
	ld h,(ix+001h)		;2559	dd 66 01	. f .
	ld c,(ix+002h)		;255c	dd 4e 02	. N .
	ld b,(ix+003h)		;255f	dd 46 03	. F .
	add hl,bc		;2562	09		.
	ld c,l			;2563	4d		M
	ld b,h			;2564	44		D
	ret			;2565	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AttrAddr: attribute address for the cursor position.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AttrAddr:
	call sub_2556h		;2566	cd 56 25	. V %
	ld a,c			;2569	79		y
	rrca			;256a	0f		.
	rrca			;256b	0f		.
	rrca			;256c	0f		.
	ld c,a			;256d	4f		O
	and 0e0h		;256e	e6 e0		. .
	xor b			;2570	a8		.
	ld l,a			;2571	6f		o
	ld a,c			;2572	79		y
	and 003h		;2573	e6 03		. .
	xor 058h		;2575	ee 58		. X
	ld h,a			;2577	67		g
	ret			;2578	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BitmapAddr: bitmap address for the cursor position.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BitmapAddr:
	call sub_2556h		;2579	cd 56 25	. V %
	ld a,c			;257c	79		y
	rrca			;257d	0f		.
	rrca			;257e	0f		.
	rrca			;257f	0f		.
	and 0e0h		;2580	e6 e0		. .
	xor b			;2582	a8		.
	ld l,a			;2583	6f		o
	ld a,c			;2584	79		y
	and 018h		;2585	e6 18		. .
	xor 040h		;2587	ee 40		. @
	ld h,a			;2589	67		g
	ret			;258a	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScrollUp: shift the window bitmap (and attributes, when the
; descriptor flag bit 1 is set) one row toward row 0, then blank
; the vacated bottom row via FillWholeRow.  Called by WindowNewline
; when the cursor sits on the last row - the classic text-scroll.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScrollUp:
	ld c,(ix+000h)		;258b	dd 4e 00	. N .
	ld b,(ix+001h)		;258e	dd 46 01	. F .
	push bc			;2591	c5		.
	ld (ix+000h),001h	;2592	dd 36 00 01	. 6 . .
	ld (ix+001h),000h	;2596	dd 36 01 00	. 6 . .
	ld b,(ix+004h)		;259a	dd 46 04	. F .
l259dh:
	dec b			;259d	05		.
	jr z,l25bbh		;259e	28 1b		( .
	push bc			;25a0	c5		.
	call BitmapAddr		;25a1	cd 79 25	. y %
	or a			;25a4	b7		.
	call CopyCharRow	;25a5	cd cd 25	. . %
	bit 1,(ix+007h)		;25a8	dd cb 07 4e	. . . N
	jr z,l25b5h		;25ac	28 07		( .
	call AttrAddr		;25ae	cd 66 25	. f %
	or a			;25b1	b7		.
	call CopyAttrRow	;25b2	cd f8 25	. . %
l25b5h:
	inc (ix+000h)		;25b5	dd 34 00	. 4 .
	pop bc			;25b8	c1		.
	jr l259dh		;25b9	18 e2		. .
l25bbh:
	ld a,(ix+004h)		;25bb	dd 7e 04	. ~ .
	dec a			;25be	3d		=
	ld (ix+000h),a		;25bf	dd 77 00	. w .
	call sub_267eh		;25c2	cd 7e 26	. ~ &
	pop bc			;25c5	c1		.
	ld (ix+000h),c		;25c6	dd 71 00	. q .
	ld (ix+001h),b		;25c9	dd 70 01	. p .
	ret			;25cc	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CopyCharRow: copy one 8-line character row between adjacent
; window rows (LDIR per line, H/D advanced within the third);
; carry selects the source row: +1 (into ScrollDown) or -1.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CopyCharRow:
	push hl			;25cd	e5		.
	ld a,(ix+000h)		;25ce	dd 7e 00	. ~ .
	push af			;25d1	f5		.
	jr c,l25d7h		;25d2	38 03		8 .
	dec a			;25d4	3d		=
	jr l25d8h		;25d5	18 01		. .
l25d7h:
	inc a			;25d7	3c		<
l25d8h:
	ld (ix+000h),a		;25d8	dd 77 00	. w .
	call BitmapAddr		;25db	cd 79 25	. y %
	pop af			;25de	f1		.
	ld (ix+000h),a		;25df	dd 77 00	. w .
	ld e,l			;25e2	5d		]
	ld d,h			;25e3	54		T
	pop hl			;25e4	e1		.
	ld a,008h		;25e5	3e 08		> .
l25e7h:
	ld b,000h		;25e7	06 00		. .
	ld c,(ix+005h)		;25e9	dd 4e 05	. N .
	push hl			;25ec	e5		.
	push de			;25ed	d5		.
	ldir			;25ee	ed b0		. .
	pop de			;25f0	d1		.
	pop hl			;25f1	e1		.
	inc h			;25f2	24		$
	inc d			;25f3	14		.
	dec a			;25f4	3d		=
	jr nz,l25e7h		;25f5	20 f0		  .
	ret			;25f7	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CopyAttrRow: the attribute-row twin of CopyCharRow - one LDIR
; of the window width, source row selected by carry.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CopyAttrRow:
	push hl			;25f8	e5		.
	ld a,(ix+000h)		;25f9	dd 7e 00	. ~ .
	push af			;25fc	f5		.
	jr c,l2602h		;25fd	38 03		8 .
	dec a			;25ff	3d		=
	jr l2603h		;2600	18 01		. .
l2602h:
	inc a			;2602	3c		<
l2603h:
	ld (ix+000h),a		;2603	dd 77 00	. w .
	call AttrAddr		;2606	cd 66 25	. f %
	pop af			;2609	f1		.
	ld (ix+000h),a		;260a	dd 77 00	. w .
	ld e,l			;260d	5d		]
	ld d,h			;260e	54		T
	pop hl			;260f	e1		.
	ld b,000h		;2610	06 00		. .
	ld c,(ix+005h)		;2612	dd 4e 05	. N .
	ldir			;2615	ed b0		. .
	ret			;2617	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScrollDown: reverse scroll - rows shift away from row 0 (the
; helpers are called with carry set), the vacated top row is
; blanked from column 0.  Used by the disassembly scroll-back
; (the caller sits just before ArmWatchpoint).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScrollDown:
	ld c,(ix+000h)		;2618	dd 4e 00	. N .
	ld b,(ix+001h)		;261b	dd 46 01	. F .
	push bc			;261e	c5		.
	ld (ix+001h),000h	;261f	dd 36 01 00	. 6 . .
	ld b,(ix+004h)		;2623	dd 46 04	. F .
	dec b			;2626	05		.
	jr z,l2649h		;2627	28 20		(  
	dec b			;2629	05		.
	ld (ix+000h),b		;262a	dd 70 00	. p .
	inc b			;262d	04		.
l262eh:
	push bc			;262e	c5		.
	call BitmapAddr		;262f	cd 79 25	. y %
	scf			;2632	37		7
	call CopyCharRow	;2633	cd cd 25	. . %
	bit 1,(ix+007h)		;2636	dd cb 07 4e	. . . N
	jr z,l2643h		;263a	28 07		( .
	call AttrAddr		;263c	cd 66 25	. f %
	scf			;263f	37		7
	call CopyAttrRow	;2640	cd f8 25	. . %
l2643h:
	dec (ix+000h)		;2643	dd 35 00	. 5 .
	pop bc			;2646	c1		.
	djnz l262eh		;2647	10 e5		. .
l2649h:
	ld (ix+000h),000h	;2649	dd 36 00 00	. 6 . .
	call sub_267eh		;264d	cd 7e 26	. ~ &
	pop bc			;2650	c1		.
	ld (ix+000h),c		;2651	dd 71 00	. q .
	ld (ix+001h),b		;2654	dd 70 01	. p .
	ret			;2657	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FillWindow: blank every row of the window (FillRowToEnd per
; row, attributes from the descriptor), home the cursor to row 0
; and restart the blink timer ($24E1).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FillWindow:
	call sub_02afh		;2658	cd af 02	. . .
sub_265bh:
	xor a			;265b	af		.
	ld (ix+000h),a		;265c	dd 77 00	. w .
	ld (ix+001h),a		;265f	dd 77 01	. w .
	ld b,(ix+004h)		;2662	dd 46 04	. F .
l2665h:
	push bc			;2665	c5		.
	call FillRowToEnd	;2666	cd 82 26	. . &
	pop bc			;2669	c1		.
	inc (ix+000h)		;266a	dd 34 00	. 4 .
	djnz l2665h		;266d	10 f6		. .
	ld (ix+000h),000h	;266f	dd 36 00 00	. 6 . .
	call RestartCursorBlink	;2673	cd e1 24	. . $
	ret			;2676	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FillWholeRow: save the cursor cell, park X at 0 and fall into
; FillRowToEnd - i.e. blank the entire current row.  The scroll
; routines enter at $267E (one instruction further) to keep the
; cursor flag untouched.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FillWholeRow:
	call SaveCursorCell	;2677	cd b1 24	. . $
	ld (ix+001h),000h	;267a	dd 36 01 00	. 6 . .
sub_267eh:
	res 3,(iy+009h)		;267e	fd cb 09 9e	. . . .
FillRowToEnd:
	call SaveCursorCell	;2682	cd b1 24	. . $
	call BitmapAddr		;2685	cd 79 25	. y %
	ld a,(ix+005h)		;2688	dd 7e 05	. ~ .
	sub (ix+001h)		;268b	dd 96 01	. . .
	ld c,008h		;268e	0e 08		. .
	ld d,a			;2690	57		W
	ld e,l			;2691	5d		]
l2692h:
	ld b,d			;2692	42		B
	ld l,e			;2693	6b		k
l2694h:
	ld (hl),000h		;2694	36 00		6 .
	inc l			;2696	2c		,
	djnz l2694h		;2697	10 fb		. .
	inc h			;2699	24		$
	dec c			;269a	0d		.
	jr nz,l2692h		;269b	20 f5		  .
	push de			;269d	d5		.
	call AttrAddr		;269e	cd 66 25	. f %
	pop bc			;26a1	c1		.
	ld d,(ix+006h)		;26a2	dd 56 06	. V .
l26a5h:
	ld (hl),d		;26a5	72		r
	inc hl			;26a6	23		#
	djnz l26a5h		;26a7	10 fc		. .
	ret			;26a9	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrinterPutChar: buffer a character for the printer/parallel
; port (buffer pointer at E2D9).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrinterPutChar:
	ld hl,(0e2d9h)		;26aa	2a d9 e2	* . .
	ld (hl),a		;26ad	77		w
	inc hl			;26ae	23		#
	ld (0e2d9h),hl		;26af	22 d9 e2	" . .
	ret			;26b2	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PopAllRet: pop every register and return - the standard exit
; of window handlers.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PopAllRet:
	pop bc			;26b3	c1		.
	pop de			;26b4	d1		.
	pop hl			;26b5	e1		.
	ret			;26b6	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintCharBit7: print A with bit 7 stripped (terminator-safe).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintCharBit7:
	push af			;26b7	f5		.
	and 07fh		;26b8	e6 7f		. .
	jr l26bfh		;26ba	18 03		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintSpace.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintSpace:
	ld a,020h		;26bc	3e 20		>  
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintChar - the RST 10h backend and the single character
; sink of the monitor; everything funnels into
; DispatchOutput next.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintChar:
	push af			;26be	f5		.
l26bfh:
	call DispatchOutput	;26bf	cd c4 26	. . &
	pop af			;26c2	f1		.
	ret			;26c3	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DispatchOutput: the output switchboard - DFE8 bits 4/5/6
; route each character to tape ($084D), printer or the screen
; window; installed print hooks watch the stream as well.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DispatchOutput:
	push hl			;26c4	e5		.
	push de			;26c5	d5		.
	push bc			;26c6	c5		.
	ld hl,PopAllRet		;26c7	21 b3 26	! . &
	push hl			;26ca	e5		.
	ld hl,0dfe8h		;26cb	21 e8 df	! . .
	bit 6,(hl)		;26ce	cb 76		. v
	jr nz,PrinterPutChar	;26d0	20 d8		  .
	bit 5,(ix+007h)		;26d2	dd cb 07 6e	. . . n
	ret nz			;26d6	c0		.
	bit 4,(hl)		;26d7	cb 66		. f
	jp nz,FeedTapeOutput	;26d9	c2 4d 08	. M .
	bit 5,(hl)		;26dc	cb 6e		. n
	jr nz,l2707h		;26de	20 27		  '
	cp 020h			;26e0	fe 20		.  
	jr c,l26f2h		;26e2	38 0e		8 .
	call sub_241fh		;26e4	cd 1f 24	. . $
	ret nz			;26e7	c0		.
	bit 4,(ix+007h)		;26e8	dd cb 07 66	. . . f
	ret z			;26ec	c8		.
	call sub_271eh		;26ed	cd 1e 27	. . '
	jr l272dh		;26f0	18 3b		. ;
l26f2h:
	ld hl,l2405h		;26f2	21 05 24	! . $
	ld bc,Rst08Vector	;26f5	01 08 00	. . .
	ld e,l			;26f8	5d		]
	ld d,h			;26f9	54		T
	cpdr			;26fa	ed b9		. .
	ret nz			;26fc	c0		.
	inc de			;26fd	13		.
	ex de,hl		;26fe	eb		.
	sla c			;26ff	cb 21		. !
	add hl,bc		;2701	09		.
	ld e,(hl)		;2702	5e		^
	inc hl			;2703	23		#
	ld d,(hl)		;2704	56		V
	ex de,hl		;2705	eb		.
	jp (hl)			;2706	e9		.
l2707h:
	res 5,(iy+009h)		;2707	fd cb 09 ae	. . . .
	ld hl,(0e2d6h)		;270b	2a d6 e2	* . .
	jp (hl)			;270e	e9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; InstallPrintHook: park a user routine at the E2D6 vector;
; every printed character passes through it.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
InstallPrintHook:
	ld (0e2d6h),hl		;270f	22 d6 e2	" . .
	set 5,(iy+009h)		;2712	fd cb 09 ee	. . . .
	ret			;2716	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WindowNewline: advance to the next row (scroll at the bottom
; edge).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WindowNewline:
	bit 2,(ix+007h)		;2717	dd cb 07 56	. . . V
	call nz,l272dh		;271b	c4 2d 27	. - '
sub_271eh:
	call SaveCursorCell	;271e	cd b1 24	. . $
	xor a			;2721	af		.
	ld (ix+001h),a		;2722	dd 77 01	. w .
	ret			;2725	c9		.
	bit 3,(ix+007h)		;2726	dd cb 07 5e	. . . ^
	call nz,sub_271eh	;272a	c4 1e 27	. . '
l272dh:
	call sub_2737h		;272d	cd 37 27	. 7 '
	ret nz			;2730	c0		.
	call SaveCursorCell	;2731	cd b1 24	. . $
	jp ScrollUp		;2734	c3 8b 25	. . %
sub_2737h:
	ld a,(ix+004h)		;2737	dd 7e 04	. ~ .
	dec a			;273a	3d		=
	cp (ix+000h)		;273b	dd be 00	. . .
	ret z			;273e	c8		.
	call SaveCursorCell	;273f	cd b1 24	. . $
	inc (ix+000h)		;2742	dd 34 00	. 4 .
	ret			;2745	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WindowHome: cursor to row/column 0 of the current window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WindowHome:
	call SaveCursorCell	;2746	cd b1 24	. . $
	xor a			;2749	af		.
	ld (ix+001h),a		;274a	dd 77 01	. w .
	ld (ix+000h),a		;274d	dd 77 00	. w .
	ret			;2750	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyClick: brief speaker toggle (OUT $FE, bit 4 XOR) per
; keypress.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
KeyClick:
	push de			;2751	d5		.
	ld de,07060h		;2752	11 60 70	. ` p
	push bc			;2755	c5		.
	push af			;2756	f5		.
	ld a,(0e005h)		;2757	3a 05 e0	: . .
	push af			;275a	f5		.
l275bh:
	out (0feh),a		;275b	d3 fe		. .
	xor 010h		;275d	ee 10		. .
	push af			;275f	f5		.
	ld a,r			;2760	ed 5f		. _
	and 001h		;2762	e6 01		. .
	add a,e			;2764	83		.
	ld b,a			;2765	47		G
l2766h:
	djnz l2766h		;2766	10 fe		. .
	pop af			;2768	f1		.
	dec d			;2769	15		.
	jr nz,l275bh		;276a	20 ef		  .
	pop af			;276c	f1		.
	out (0feh),a		;276d	d3 fe		. .
	pop af			;276f	f1		.
	pop bc			;2770	c1		.
	pop de			;2771	d1		.
	ret			;2772	c9		.
	ld hl,l2779h		;2773	21 79 27	! y '
	jp InstallPrintHook	;2776	c3 0f 27	. . '
l2779h:
	ld c,(ix+000h)		;2779	dd 4e 00	. N .
	cp (ix+004h)		;277c	dd be 04	. . .
	jr nc,l2782h		;277f	30 01		0 .
	ld c,a			;2781	4f		O
l2782h:
	ld a,c			;2782	79		y
	ld (0e2d8h),a		;2783	32 d8 e2	2 . .
	ld hl,l278ch		;2786	21 8c 27	! . '
	jp InstallPrintHook	;2789	c3 0f 27	. . '
l278ch:
	call SaveCursorCell	;278c	cd b1 24	. . $
	ld c,(ix+00bh)		;278f	dd 4e 0b	. N .
	dec c			;2792	0d		.
	cp c			;2793	b9		.
	jr nc,l2797h		;2794	30 01		0 .
	ld c,a			;2796	4f		O
l2797h:
	ld (ix+001h),c		;2797	dd 71 01	. q .
	ld a,(0e2d8h)		;279a	3a d8 e2	: . .
	ld (ix+000h),a		;279d	dd 77 00	. w .
	ret			;27a0	c9		.
	ld a,(ix+006h)		;27a1	dd 7e 06	. ~ .
	ld (0e2d1h),a		;27a4	32 d1 e2	2 . .
	ld hl,l27adh		;27a7	21 ad 27	! . '
	jp InstallPrintHook	;27aa	c3 0f 27	. . '
l27adh:
	or (ix+006h)		;27ad	dd b6 06	. . .
	ld (ix+006h),a		;27b0	dd 77 06	. w .
	ld hl,l27b9h		;27b3	21 b9 27	! . '
	jp InstallPrintHook	;27b6	c3 0f 27	. . '
l27b9h:
	cpl			;27b9	2f		/
	and (ix+006h)		;27ba	dd a6 06	. . .
	ld (ix+006h),a		;27bd	dd 77 06	. w .
	ret			;27c0	c9		.
	ld a,(0e2d1h)		;27c1	3a d1 e2	: . .
	ld (ix+006h),a		;27c4	dd 77 06	. w .
	ret			;27c7	c9		.
sub_27c8h:
	call l0e2ch		;27c8	cd 2c 0e	. , .
	ld (iy+00ah),000h	;27cb	fd 36 0a 00	. 6 . .
	ld de,0e6f7h		;27cf	11 f7 e6	. . .
	ld ix,0e5f7h		;27d2	dd 21 f7 e5	. ! . .
	ret			;27d6	c9		.
	call sub_27c8h		;27d7	cd c8 27	. . '
	rst 18h			;27da	df		.
	ld d,c			;27db	51		Q
	inc a			;27dc	3c		<
	jp nc,l0a49h		;27dd	d2 49 0a	. I .
l27e0h:
	call ReadNmiPort	;27e0	cd 74 34	. t 4
l27e3h:
	jp ExitToError		;27e3	c3 54 0a	. T .
	ld b,002h		;27e6	06 02		. .
	call sub_27ffh		;27e8	cd ff 27	. . '
	call FillMemoryRange	;27eb	cd 46 28	. F (
l27eeh:
	jr c,l27e0h		;27ee	38 f0		8 .
	rst 18h			;27f0	df		.
	and h			;27f1	a4		.
	ld a,(0e818h)		;27f2	3a 18 e8	: . .
	ld b,001h		;27f5	06 01		. .
	call sub_27ffh		;27f7	cd ff 27	. . '
	call sub_2836h		;27fa	cd 36 28	. 6 (
	jr l27eeh		;27fd	18 ef		. .
sub_27ffh:
	call sub_27c8h		;27ff	cd c8 27	. . '
	rst 18h			;2802	df		.
	add a,b			;2803	80		.
	ld a,(0d938h)		;2804	3a 38 d9	: 8 .
	call sub_0e00h		;2807	cd 00 0e	. . .
	cp 002h			;280a	fe 02		. .
	jr z,l281ah		;280c	28 0c		( .
	jr nc,l2832h		;280e	30 22		0 "
	bit 0,(ix+017h)		;2810	dd cb 17 46	. . . F
	jr nz,l281ah		;2814	20 04		  .
	ld a,00ah		;2816	3e 0a		> .
	jr l27e3h		;2818	18 c9		. .
l281ah:
	push hl			;281a	e5		.
	push de			;281b	d5		.
	push af			;281c	f5		.
	rst 18h			;281d	df		.
	rra			;281e	1f		.
	ld a,(0be38h)		;281f	3a 38 be	: 8 .
	ld e,c			;2822	59		Y
	ld d,b			;2823	50		P
	pop af			;2824	f1		.
	pop hl			;2825	e1		.
	pop bc			;2826	c1		.
l2827h:
	cp 001h			;2827	fe 01		. .
	jr z,l282fh		;2829	28 04		( .
	ret nc			;282b	d0		.
	ld hl,(0e600h)		;282c	2a 00 e6	* . .
l282fh:
	ld b,d			;282f	42		B
	ld c,e			;2830	4b		K
	ret			;2831	c9		.
l2832h:
	ld a,00bh		;2832	3e 0b		> .
	jr l27e3h		;2834	18 ad		. .
sub_2836h:
	push hl			;2836	e5		.
	push bc			;2837	c5		.
	rst 18h			;2838	df		.
	dec c			;2839	0d		.
	dec sp			;283a	3b		;
	pop bc			;283b	c1		.
	pop hl			;283c	e1		.
	ret c			;283d	d8		.
	rst 0			;283e	c7		.
	inc hl			;283f	23		#
	dec bc			;2840	0b		.
	ld a,b			;2841	78		x
	or c			;2842	b1		.
	jr nz,sub_2836h		;2843	20 f1		  .
	ret			;2845	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FillMemoryRange: fill through the E600 pointer with a
; pattern.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FillMemoryRange:
	ld (0e600h),hl		;2846	22 00 e6	" . .
l2849h:
	push hl			;2849	e5		.
	push bc			;284a	c5		.
	rst 28h			;284b	ef		.
	rst 18h			;284c	df		.
	ld e,c			;284d	59		Y
	dec sp			;284e	3b		;
	pop bc			;284f	c1		.
	pop hl			;2850	e1		.
	ret c			;2851	d8		.
	inc hl			;2852	23		#
	dec bc			;2853	0b		.
	ld a,b			;2854	78		x
	or c			;2855	b1		.
	jr nz,l2849h		;2856	20 f1		  .
	ret			;2858	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ChecksumLoop: running sum over a range - also used to
; calibrate AY timing.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ChecksumLoop:
	xor a			;2859	af		.
	ld l,a			;285a	6f		o
	ld h,a			;285b	67		g
	ld c,a			;285c	4f		O
	ld b,a			;285d	47		G
	ex af,af'		;285e	08		.
	ld de,l00f6h		;285f	11 f6 00	. . .
	set 6,b			;2862	cb f0		. .
	dec bc			;2864	0b		.
l2865h:
	ex af,af'		;2865	08		.
	add a,(hl)		;2866	86		.
	ex af,af'		;2867	08		.
	inc hl			;2868	23		#
	or a			;2869	b7		.
	sbc hl,de		;286a	ed 52		. R
	add hl,de		;286c	19		.
	jr nz,l2870h		;286d	20 01		  .
	inc hl			;286f	23		#
l2870h:
	dec bc			;2870	0b		.
	ld a,c			;2871	79		y
	or b			;2872	b0		.
	jr nz,l2865h		;2873	20 f0		  .
	ex af,af'		;2875	08		.
	ex de,hl		;2876	eb		.
	cp (hl)			;2877	be		.
	ret z			;2878	c8		.
	ld bc,l00feh		;2879	01 fe 00	. . .
	ld hl,0c000h		;287c	21 00 c0	! . .
	inc (hl)		;287f	34		4
	ld l,(hl)		;2880	6e		n
	out (c),l		;2881	ed 69		. i
	jr ChecksumLoop		;2883	18 d4		. .
	ld c,b			;2885	48		H
	ld h,l			;2886	65		e
	ld l,h			;2887	6c		l
	ret p			;2888	f0		.
	ld c,c			;2889	49		I
	call sub_00bfh		;288a	cd bf 00	. . .
	call m,sub_0035h	;288d	fc 35 00	. 5 .
	ld e,l			;2890	5d		]
	ld (hl),001h		;2891	36 01		6 .
	dec l			;2893	2d		-
	dec hl			;2894	2b		+
	ld bc,07a00h		;2895	01 00 7a	. . z
	or a			;2898	b7		.
	ld a,02ch		;2899	3e 2c		> ,
	ret nz			;289b	c0		.
	ld (iy+016h),e		;289c	fd 73 16	. s .
	ld hl,(UserPc)		;289f	2a 6b dd	* k .
	call l1b9ah		;28a2	cd 9a 1b	. . .
l28a5h:
	call sub_13cch		;28a5	cd cc 13	. . .
	call sub_2f3eh		;28a8	cd 3e 2f	. > /
	jr l28a5h		;28ab	18 f8		. .
	ld hl,WatchTable	;28ad	21 94 e3	! . .
	ld de,l2827h		;28b0	11 27 28	. ' (
	ld bc,l28e4h		;28b3	01 e4 28	. . (
	ld a,009h		;28b6	3e 09		> .
	push bc			;28b8	c5		.
	push af			;28b9	f5		.
	push hl			;28ba	e5		.
	push de			;28bb	d5		.
	call InitErrorWindow	;28bc	cd cb 35	. . 5
	ld b,e			;28bf	43		C
	rst 8			;28c0	cf		.
	rst 20h			;28c1	e7		.
	dec c			;28c2	0d		.
	adc a,l			;28c3	8d		.
	pop bc			;28c4	c1		.
	rst 8			;28c5	cf		.
	rst 20h			;28c6	e7		.
	dec c			;28c7	0d		.
	adc a,l			;28c8	8d		.
	pop hl			;28c9	e1		.
	pop de			;28ca	d1		.
	ld e,001h		;28cb	1e 01		. .
	pop bc			;28cd	c1		.
l28ceh:
	push de			;28ce	d5		.
	push bc			;28cf	c5		.
	call sub_35c9h		;28d0	cd c9 35	. . 5
	jr c,l28e1h		;28d3	38 0c		8 .
	call sub_2f3eh		;28d5	cd 3e 2f	. > /
	rst 20h			;28d8	e7		.
	adc a,l			;28d9	8d		.
	pop bc			;28da	c1		.
	pop de			;28db	d1		.
	inc e			;28dc	1c		.
	ld a,d			;28dd	7a		z
	cp e			;28de	bb		.
	jr nz,l28ceh		;28df	20 ed		  .
l28e1h:
	ld a,081h		;28e1	3e 81		> .
	ret			;28e3	c9		.
l28e4h:
	ld c,e			;28e4	4b		K
	ld b,000h		;28e5	06 00		. .
	push de			;28e7	d5		.
	ld (iy+008h),020h	;28e8	fd 36 08 20	. 6 .  
	ld (iy+007h),002h	;28ec	fd 36 07 02	. 6 . .
	call sub_192eh		;28f0	cd 2e 19	. . .
	pop de			;28f3	d1		.
	rst 20h			;28f4	e7		.
	and b			;28f5	a0		.
	push hl			;28f6	e5		.
	bit 6,(hl)		;28f7	cb 76		. v
	jr nz,l2900h		;28f9	20 05		  .
	ld b,029h		;28fb	06 29		. )
	rst 8			;28fd	cf		.
	jr l2957h		;28fe	18 57		. W
l2900h:
	push de			;2900	d5		.
	ld b,002h		;2901	06 02		. .
	call l18b4h		;2903	cd b4 18	. . .
	rst 20h			;2906	e7		.
	and e			;2907	a3		.
	push hl			;2908	e5		.
	inc hl			;2909	23		#
	ld e,(hl)		;290a	5e		^
	push de			;290b	d5		.
	inc hl			;290c	23		#
	ld e,(hl)		;290d	5e		^
	inc hl			;290e	23		#
	ld d,(hl)		;290f	56		V
	ex de,hl		;2910	eb		.
	call PrintHexWord	;2911	cd 9e 19	. . .
	pop de			;2914	d1		.
	pop hl			;2915	e1		.
	ld b,004h		;2916	06 04		. .
	call l18b4h		;2918	cd b4 18	. . .
	ld a,e			;291b	7b		{
	call sub_19ach		;291c	cd ac 19	. . .
	ld b,004h		;291f	06 04		. .
	call l18b4h		;2921	cd b4 18	. . .
	ld a,(hl)		;2924	7e		~
	and 003h		;2925	e6 03		. .
	call sub_19ach		;2927	cd ac 19	. . .
	ld b,003h		;292a	06 03		. .
	call l18b4h		;292c	cd b4 18	. . .
	bit 7,(hl)		;292f	cb 7e		. ~
	call PrintOkError	;2931	cd 5e 29	. ^ )
	ld de,l0003h+2		;2934	11 05 00	. . .
	add hl,de		;2937	19		.
	ld c,(hl)		;2938	4e		N
	inc hl			;2939	23		#
	ld b,(hl)		;293a	46		F
	inc hl			;293b	23		#
	call sub_1929h		;293c	cd 29 19	. ) .
	ld c,(hl)		;293f	4e		N
	inc hl			;2940	23		#
	ld b,(hl)		;2941	46		F
	inc hl			;2942	23		#
	call sub_1921h		;2943	cd 21 19	. ! .
	ld b,002h		;2946	06 02		. .
	call l18b4h		;2948	cd b4 18	. . .
	pop de			;294b	d1		.
	ld a,e			;294c	7b		{
	call LookupCommand	;294d	cd 6d 29	. m )
	ld a,02dh		;2950	3e 2d		> -
	jr z,l2956h		;2952	28 02		( .
	ld a,02bh		;2954	3e 2b		> +
l2956h:
	rst 10h			;2956	d7		.
l2957h:
	pop hl			;2957	e1		.
	ld de,l000bh		;2958	11 0b 00	. . .
	add hl,de		;295b	19		.
	or a			;295c	b7		.
	ret			;295d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintOkError: "Ok" or the pending error after a command.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintOkError:
	push hl			;295e	e5		.
	jr nz,l2967h		;295f	20 06		  .
	rst 20h			;2961	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgOff: inline RST 20h string OF+$C6 which prints
; OFF - the terminating byte is emitted with bit 7
; stripped; used by the watch/breakpoint listing
; routine above ($295E).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgOff' (start 0x2962 end 0x2965)
MsgOff_start:
	defb 04fh		;2962	4f		O
	defb 046h		;2963	46		F
	defb 0c6h		;2964	c6		.
MsgOff_end:
	pop hl			;2965	e1		.
	ret			;2966	c9		.
l2967h:
	rst 20h			;2967	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgOn: ON+$A0, printing ON - companion of MsgOff
; ($2962).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgOn' (start 0x2968 end 0x296b)
MsgOn_start:
	defb 04fh		;2968	4f		O
	defb 04eh		;2969	4e		N
	defb 0a0h		;296a	a0		.
MsgOn_end:
	pop hl			;296b	e1		.
	ret			;296c	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; LookupCommand: walk CommandTable (E80B, 2-byte entries)
; matching the entered verb.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
LookupCommand:
	ld de,CommandTable	;296d	11 0b e8	. . .
	ld l,a			;2970	6f		o
	ld h,000h		;2971	26 00		& .
	add hl,hl		;2973	29		)
	add hl,de		;2974	19		.
	ld e,(hl)		;2975	5e		^
	push hl			;2976	e5		.
	inc hl			;2977	23		#
	ld d,(hl)		;2978	56		V
	ld l,a			;2979	6f		o
	ld a,d			;297a	7a		z
	or e			;297b	b3		.
	ld a,l			;297c	7d		}
	pop hl			;297d	e1		.
	ret			;297e	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckWatchpoints: after every step compare the 8 entries of
; WatchTable (E394, 11 bytes each: address, length, value...)
; against the visible banks; a hit re-enters the monitor.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckWatchpoints:
	res 7,(iy+00bh)		;297f	fd cb 0b be	. . . .
	jr l2989h		;2983	18 04		. .
sub_2985h:
	set 7,(iy+00bh)		;2985	fd cb 0b fe	. . . .
l2989h:
	call InitWatchTable	;2989	cd c9 29	. . )
l298ch:
	call sub_29d3h		;298c	cd d3 29	. . )
	jr nc,l29c4h		;298f	30 33		0 3
	call SavePagingShadow	;2991	cd 80 2d	. . -
	exx			;2994	d9		.
	ld a,h			;2995	7c		|
	exx			;2996	d9		.
	and 0c0h		;2997	e6 c0		. .
	jr z,l29adh		;2999	28 12		( .
	bit 5,(iy-002h)		;299b	fd cb fe 6e	. . . n
	jr z,l29a8h		;299f	28 07		( .
	call sub_2e8ch		;29a1	cd 8c 2e	. . .
	exx			;29a4	d9		.
	jr nz,l29c1h		;29a5	20 1a		  .
	exx			;29a7	d9		.
l29a8h:
	push bc			;29a8	c5		.
	call sub_2eb7h		;29a9	cd b7 2e	. . .
	pop bc			;29ac	c1		.
l29adh:
	exx			;29ad	d9		.
	bit 7,(iy+00bh)		;29ae	fd cb 0b 7e	. . . ~
	jr z,l29bdh		;29b2	28 09		( .
	rst 28h			;29b4	ef		.
	ld (ix+004h),a		;29b5	dd 77 04	. w .
	ld a,0cfh		;29b8	3e cf		> .
	rst 0			;29ba	c7		.
	jr l29c1h		;29bb	18 04		. .
l29bdh:
	ld a,(ix+004h)		;29bd	dd 7e 04	. ~ .
	rst 0			;29c0	c7		.
l29c1h:
	call RestorePaging	;29c1	cd 84 35	. . 5
l29c4h:
	add ix,de		;29c4	dd 19		. .
	djnz l298ch		;29c6	10 c4		. .
	ret			;29c8	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; InitWatchTable: clear the watchpoint table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
InitWatchTable:
	ld ix,WatchTable	;29c9	dd 21 94 e3	. ! . .
	ld b,008h		;29cd	06 08		. .
	ld de,l000bh		;29cf	11 0b 00	. . .
	ret			;29d2	c9		.
sub_29d3h:
	ld a,(ix+000h)		;29d3	dd 7e 00	. ~ .
	rlca			;29d6	07		.
	ret nc			;29d7	d0		.
	ccf			;29d8	3f		?
	bit 1,a			;29d9	cb 4f		. O
	ret nz			;29db	c0		.
	ld l,(ix+002h)		;29dc	dd 6e 02	. n .
	ld h,(ix+003h)		;29df	dd 66 03	. f .
	exx			;29e2	d9		.
	ld l,(ix+001h)		;29e3	dd 6e 01	. n .
	ccf			;29e6	3f		?
	ret			;29e7	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FindWatchpoint: locate a free/owning slot in WatchTable.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FindWatchpoint:
	call InitWatchTable	;29e8	cd c9 29	. . )
l29ebh:
	ld a,(ix+000h)		;29eb	dd 7e 00	. ~ .
	rlca			;29ee	07		.
	jr nc,l2a06h		;29ef	30 15		0 .
	exx			;29f1	d9		.
	ld l,(ix+001h)		;29f2	dd 6e 01	. n .
	call sub_2e8ch		;29f5	cd 8c 2e	. . .
	exx			;29f8	d9		.
	jr nz,l2a06h		;29f9	20 0b		  .
	ld a,(ix+002h)		;29fb	dd 7e 02	. ~ .
	cp l			;29fe	bd		.
	jr nz,l2a06h		;29ff	20 05		  .
	ld a,(ix+003h)		;2a01	dd 7e 03	. ~ .
	cp h			;2a04	bc		.
	ret z			;2a05	c8		.
l2a06h:
	add ix,de		;2a06	dd 19		. .
	djnz l29ebh		;2a08	10 e1		. .
	scf			;2a0a	37		7
	ret			;2a0b	c9		.
	call sub_2a1fh		;2a0c	cd 1f 2a	. . *
	ld (ix+000h),000h	;2a0f	dd 36 00 00	. 6 . .
	ld a,(RegisterBackup)	;2a13	3a 99 dd	: . .
	call LookupCommand	;2a16	cd 6d 29	. m )
	xor a			;2a19	af		.
	ld (hl),a		;2a1a	77		w
	inc hl			;2a1b	23		#
	ld (hl),a		;2a1c	77		w
	jr l2a58h		;2a1d	18 39		. 9
sub_2a1fh:
	push af			;2a1f	f5		.
	dec e			;2a20	1d		.
	ld a,e			;2a21	7b		{
	and 0f8h		;2a22	e6 f8		. .
	or d			;2a24	b2		.
	ld a,022h		;2a25	3e 22		> "
	jr nz,l2a5dh		;2a27	20 34		  4
	push hl			;2a29	e5		.
	ld l,e			;2a2a	6b		k
	ld h,d			;2a2b	62		b
	add hl,hl		;2a2c	29		)
	add hl,hl		;2a2d	29		)
	add hl,de		;2a2e	19		.
	add hl,hl		;2a2f	29		)
	add hl,de		;2a30	19		.
	ld de,WatchTable	;2a31	11 94 e3	. . .
	add hl,de		;2a34	19		.
	push hl			;2a35	e5		.
	pop ix			;2a36	dd e1		. .
	pop hl			;2a38	e1		.
	pop af			;2a39	f1		.
	bit 6,(ix+000h)		;2a3a	dd cb 00 76	. . . v
	ret			;2a3e	c9		.
	call sub_2a1fh		;2a3f	cd 1f 2a	. . *
	ld a,026h		;2a42	3e 26		> &
	jr z,l2a5dh		;2a44	28 17		( .
	ld a,l			;2a46	7d		}
	and 0feh		;2a47	e6 fe		. .
	or h			;2a49	b4		.
	ld a,023h		;2a4a	3e 23		> #
	jr nz,l2a5dh		;2a4c	20 0f		  .
sub_2a4eh:
	sla (ix+000h)		;2a4e	dd cb 00 26	. . . &
	srl l			;2a52	cb 3d		. =
	rr (ix+000h)		;2a54	dd cb 00 1e	. . . .
l2a58h:
	ld a,081h		;2a58	3e 81		> .
	ret			;2a5a	c9		.
l2a5bh:
	ld a,00ah		;2a5b	3e 0a		> .
l2a5dh:
	jp ExitToError		;2a5d	c3 54 0a	. T .
sub_2a60h:
	call sub_2a1fh		;2a60	cd 1f 2a	. . *
	push ix			;2a63	dd e5		. .
	ld de,0e3edh		;2a65	11 ed e3	. . .
	ex de,hl		;2a68	eb		.
	push af			;2a69	f5		.
	ld a,e			;2a6a	7b		{
	and 0fch		;2a6b	e6 fc		. .
	or d			;2a6d	b2		.
	ld a,024h		;2a6e	3e 24		> $
	jr nz,l2a5dh		;2a70	20 eb		  .
	pop af			;2a72	f1		.
	cp 004h			;2a73	fe 04		. .
	jr z,l2a92h		;2a75	28 1b		( .
	jr c,l2a5bh		;2a77	38 e2		8 .
	cp 006h			;2a79	fe 06		. .
	ld a,00bh		;2a7b	3e 0b		> .
	jr nc,l2a5dh		;2a7d	30 de		0 .
	push hl			;2a7f	e5		.
	ld hl,(0dda1h)		;2a80	2a a1 dd	* . .
	ld a,l			;2a83	7d		}
	and 0f0h		;2a84	e6 f0		. .
	or h			;2a86	b4		.
	ld a,018h		;2a87	3e 18		> .
	jr nz,l2a5dh		;2a89	20 d2		  .
	ld a,l			;2a8b	7d		}
	pop hl			;2a8c	e1		.
	ld (hl),a		;2a8d	77		w
	set 5,e			;2a8e	cb eb		. .
	jr l2a96h		;2a90	18 04		. .
l2a92h:
	call ScreenBankCheck	;2a92	cd 7e 2e	. ~ .
	ld (hl),a		;2a95	77		w
l2a96h:
	bit 0,e			;2a96	cb 43		. C
	inc hl			;2a98	23		#
	jr nz,l2aa2h		;2a99	20 07		  .
	ld a,b			;2a9b	78		x
	and 0c0h		;2a9c	e6 c0		. .
	ld a,025h		;2a9e	3e 25		> %
	jr z,l2a5dh		;2aa0	28 bb		( .
l2aa2h:
	ld (hl),c		;2aa2	71		q
	inc hl			;2aa3	23		#
	ld (hl),b		;2aa4	70		p
	inc hl			;2aa5	23		#
	inc hl			;2aa6	23		#
	ld bc,(0dd9fh)		;2aa7	ed 4b 9f dd	. K . .
	ld (hl),c		;2aab	71		q
	inc hl			;2aac	23		#
	ld (hl),b		;2aad	70		p
	inc hl			;2aae	23		#
	ld (hl),c		;2aaf	71		q
	inc hl			;2ab0	23		#
	ld (hl),b		;2ab1	70		p
	inc hl			;2ab2	23		#
	xor a			;2ab3	af		.
	ld (hl),a		;2ab4	77		w
	inc hl			;2ab5	23		#
	ld (hl),a		;2ab6	77		w
	ld hl,0e3ech		;2ab7	21 ec e3	! . .
	ld a,e			;2aba	7b		{
	or 0c0h			;2abb	f6 c0		. .
	ld (hl),a		;2abd	77		w
	pop de			;2abe	d1		.
	ld bc,l000bh		;2abf	01 0b 00	. . .
	ldir			;2ac2	ed b0		. .
	jr l2a58h		;2ac4	18 92		. .
	ld de,Rst08Vector	;2ac6	11 08 00	. . .
	ld bc,(0dd8bh)		;2ac9	ed 4b 8b dd	. K . .
	ld a,b			;2acd	78		x
	and 0c0h		;2ace	e6 c0		. .
	ld hl,l0001h		;2ad0	21 01 00	! . .
	ld (0dd9fh),hl		;2ad3	22 9f dd	" . .
	jr z,l2ad9h		;2ad6	28 01		( .
	dec hl			;2ad8	2b		+
l2ad9h:
	ld a,004h		;2ad9	3e 04		> .
	call sub_2a60h		;2adb	cd 60 2a	. ` *
l2adeh:
	jp l30bdh		;2ade	c3 bd 30	. . 0
	ld de,Rst08Vector	;2ae1	11 08 00	. . .
	call sub_2a1fh		;2ae4	cd 1f 2a	. . *
	ld l,000h		;2ae7	2e 00		. .
	call sub_2a4eh		;2ae9	cd 4e 2a	. N *
	jr l2adeh		;2aec	18 f0		. .
	ld a,e			;2aee	7b		{
	and 0f8h		;2aef	e6 f8		. .
	or d			;2af1	b2		.
	ld a,00dh		;2af2	3e 0d		> .
	ret nz			;2af4	c0		.
	ld a,l			;2af5	7d		}
	and 0feh		;2af6	e6 fe		. .
	or h			;2af8	b4		.
	ld a,017h		;2af9	3e 17		> .
	ret nz			;2afb	c0		.
	inc h			;2afc	24		$
	ld a,e			;2afd	7b		{
l2afeh:
	or a			;2afe	b7		.
	jr z,l2b06h		;2aff	28 05		( .
	dec a			;2b01	3d		=
	sla h			;2b02	cb 24		. $
	jr l2afeh		;2b04	18 f8		. .
l2b06h:
	ld a,h			;2b06	7c		|
	bit 0,l			;2b07	cb 45		. E
	jr nz,l2b16h		;2b09	20 0b		  .
	xor 0ffh		;2b0b	ee ff		. .
	and (iy+005h)		;2b0d	fd a6 05	. . .
l2b10h:
	ld (iy+005h),a		;2b10	fd 77 05	. w .
	ld a,081h		;2b13	3e 81		> .
	ret			;2b15	c9		.
l2b16h:
	or (iy+005h)		;2b16	fd b6 05	. . .
	jr l2b10h		;2b19	18 f5		. .
	ld a,h			;2b1b	7c		|
	or a			;2b1c	b7		.
	ld a,011h		;2b1d	3e 11		> .
	ret nz			;2b1f	c0		.
	ld c,e			;2b20	4b		K
	ld b,d			;2b21	42		B
	out (c),l		;2b22	ed 69		. i
	ld a,081h		;2b24	3e 81		> .
	ret			;2b26	c9		.
	ld c,e			;2b27	4b		K
	ld b,d			;2b28	42		B
	in e,(c)		;2b29	ed 58		. X
	ld d,000h		;2b2b	16 00		. .
sub_2b2dh:
	call PopupTabStops_end	;2b2d	cd 5f 2b	. _ +
	ld c,e			;2b30	4b		K
	ld b,d			;2b31	42		B
	ld a,b			;2b32	78		x
	or a			;2b33	b7		.
	jr nz,l2b47h		;2b34	20 11		  .
	ld a,c			;2b36	79		y
	rlca			;2b37	07		.
	jr c,l2b47h		;2b38	38 0d		8 .
	rrca			;2b3a	0f		.
	cp 020h			;2b3b	fe 20		.  
	jr c,l2b47h		;2b3d	38 08		8 .
	push af			;2b3f	f5		.
	rst 20h			;2b40	e7		.
	dec c			;2b41	0d		.
	and d			;2b42	a2		.
	pop af			;2b43	f1		.
	rst 10h			;2b44	d7		.
	rst 20h			;2b45	e7		.
	and d			;2b46	a2		.
l2b47h:
	ld hl,PopupTabStops_start	;2b47	21 5a 2b	! Z +
l2b4ah:
	ld a,(hl)		;2b4a	7e		~
	cp 081h			;2b4b	fe 81		. .
	ret z			;2b4d	c8		.
	push bc			;2b4e	c5		.
	push af			;2b4f	f5		.
	rst 20h			;2b50	e7		.
	adc a,l			;2b51	8d		.
	pop af			;2b52	f1		.
	call sub_18e9h		;2b53	cd e9 18	. . .
	pop bc			;2b56	c1		.
	inc hl			;2b57	23		#
	jr l2b4ah		;2b58	18 f0		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PopupTabStops: four tab columns (0,2,4,6) closed
; by the $81 sentinel; the loop at $2B4A feeds each
; value to the print helper at $18E9 to lay out the
; popup menu line right before OpenPopup ($2B5F).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'PopupTabStops' (start 0x2b5a end 0x2b5f)
PopupTabStops_start:
	defb 000h		;2b5a	00		.
	defb 002h		;2b5b	02		.
	defb 004h		;2b5c	04		.
	defb 006h		;2b5d	06		.
	defb 081h		;2b5e	81		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; OpenPopup: pop up a window from the descriptor at
; PopupWindowDef (E06D).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PopupTabStops_end:
OpenPopup:
	push de			;2b5f	d5		.
	ld hl,PopupWindowDef	;2b60	21 6d e0	! m .
	rst 30h			;2b63	f7		.
	call sub_265bh		;2b64	cd 5b 26	. [ &
	rst 20h			;2b67	e7		.
	adc a,l			;2b68	8d		.
	pop de			;2b69	d1		.
	ret			;2b6a	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; TapeMenu: the tape command cluster - save/verify/leader
; state machine; control flow is dense here (cf. SaveToTape
; $358D and the key waits at $2F3E+).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
TapeMenu:
	bit 5,(iy+005h)		;2b6b	fd cb 05 6e	. . . n
	ret nz			;2b6f	c0		.
	call PopupTabStops_end	;2b70	cd 5f 2b	. _ +
	call sub_09e2h		;2b73	cd e2 09	. . .
	ld hl,l3b42h		;2b76	21 42 3b	! B ;
	call PrintJustifiedHeader	;2b79	cd 9c 18	. . .
	rst 20h			;2b7c	e7		.
	and b			;2b7d	a0		.
	bit 4,(iy+014h)		;2b7e	fd cb 14 66	. . . f
	call PrintOkError	;2b82	cd 5e 29	. ^ )
	rst 20h			;2b85	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgBase: popup-menu item base led by a carriage
; return byte; the $A0 terminator prints as a
; trailing space.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgBase' (start 0x2b86 end 0x2b8c)
MsgBase_start:
	defb 00dh		;2b86	0d		.
	defb 062h		;2b87	62		b
	defb 061h		;2b88	61		a
	defb 073h		;2b89	73		s
	defb 065h		;2b8a	65		e
	defb 0a0h		;2b8b	a0		.
MsgBase_end:
	ld de,Rst08Vector+2	;2b8c	11 0a 00	. . .
	ld c,(iy+017h)		;2b8f	fd 4e 17	. N .
	ld b,d			;2b92	42		B
	call l1937h		;2b93	cd 37 19	. 7 .
	rst 20h			;2b96	e7		.
	adc a,l			;2b97	8d		.
	ld b,03ch		;2b98	06 3c		. <
	rst 8			;2b9a	cf		.
	ld bc,(StepContext)	;2b9b	ed 4b aa e0	. K . .
	call sub_1921h		;2b9f	cd 21 19	. ! .
	rst 20h			;2ba2	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgOption: popup-menu item option (CR-prefixed,
; $A0-terminated).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgOption' (start 0x2ba3 end 0x2bab)
MsgOption_start:
	defb 00dh		;2ba3	0d		.
	defb 06fh		;2ba4	6f		o
	defb 070h		;2ba5	70		p
	defb 074h		;2ba6	74		t
	defb 069h		;2ba7	69		i
	defb 06fh		;2ba8	6f		o
	defb 06eh		;2ba9	6e		n
	defb 0a0h		;2baa	a0		.
MsgOption_end:
	ld l,(iy+005h)		;2bab	fd 6e 05	. n .
	call sub_1560h		;2bae	cd 60 15	. ` .
	rst 20h			;2bb1	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgIntMode: popup-menu item Int mode (CR-prefixed);
; the choice drives the im0/im1/im2 dispatcher at
; $3671.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgIntMode' (start 0x2bb2 end 0x2bbd)
MsgIntMode_start:
	defb 00dh		;2bb2	0d		.
	defb 049h		;2bb3	49		I
	defb 06eh		;2bb4	6e		n
	defb 074h		;2bb5	74		t
	defb 020h		;2bb6	20		 
	defb 06dh		;2bb7	6d		m
	defb 06fh		;2bb8	6f		o
	defb 064h		;2bb9	64		d
	defb 065h		;2bba	65		e
	defb 0a0h		;2bbb	a0		.
	defb 0afh		;2bbc	af		.
MsgIntMode_end:
	ld d,a			;2bbd	57		W
	ei			;2bbe	fb		.
	halt			;2bbf	76		v
	di			;2bc0	f3		.
	ld a,031h		;2bc1	3e 31		> 1
	dec d			;2bc3	15		.
	jr z,l2bc7h		;2bc4	28 01		( .
	inc a			;2bc6	3c		<
l2bc7h:
	rst 10h			;2bc7	d7		.
	bit 6,(iy+019h)		;2bc8	fd cb 19 76	. . . v
	jr z,l2be9h		;2bcc	28 1b		( .
	rst 20h			;2bce	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgTapeTurbo: (t+$A9, printing (t) - appended to
; the tape line when turbo mode is selected.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgTapeTurbo' (start 0x2bcf end 0x2bd2)
MsgTapeTurbo_start:
	defb 028h		;2bcf	28		(
	defb 074h		;2bd0	74		t
	defb 0a9h		;2bd1	a9		.
MsgTapeTurbo_end:
	jr l2be9h		;2bd2	18 15		. .
	ld a,d			;2bd4	7a		z
	or a			;2bd5	b7		.
	jr nz,l2be3h		;2bd6	20 0b		  .
	ld a,e			;2bd8	7b		{
	ld bc,l0003h+2		;2bd9	01 05 00	. . .
	ld hl,l2bech		;2bdc	21 ec 2b	! . +
	cpir			;2bdf	ed b1		. .
	jr z,l2be6h		;2be1	28 03		( .
l2be3h:
	ld a,00dh		;2be3	3e 0d		> .
	ret			;2be5	c9		.
l2be6h:
	ld (iy+017h),e		;2be6	fd 73 17	. s .
l2be9h:
	ld a,081h		;2be9	3e 81		> .
	ret			;2beb	c9		.
l2bech:
	nop			;2bec	00		.
	ld (bc),a		;2bed	02		.
	ex af,af'		;2bee	08		.
	ld a,(bc)		;2bef	0a		.
	djnz SearchPattern	;2bf0	10 7a		. z
	cp 05bh			;2bf2	fe 5b		. [
	ld a,03bh		;2bf4	3e 3b		> ;
	ret c			;2bf6	d8		.
	ld a,d			;2bf7	7a		z
	cp 0c0h			;2bf8	fe c0		. .
	ld a,03bh		;2bfa	3e 3b		> ;
	ret nc			;2bfc	d0		.
	ld (StepContext),de	;2bfd	ed 53 aa e0	. S . .
	jr l2be9h		;2c01	18 e6		. .
	ld de,l0318h		;2c03	11 18 03	. . .
	set 0,(iy+013h)		;2c06	fd cb 13 c6	. . . .
	jr l2c13h		;2c0a	18 07		. .
	ld de,l0217h		;2c0c	11 17 02	. . .
	res 0,(iy+013h)		;2c0f	fd cb 13 86	. . . .
l2c13h:
	ld a,b			;2c13	78		x
	cp d			;2c14	ba		.
	ld a,00ah		;2c15	3e 0a		> .
	ret c			;2c17	d8		.
	ret z			;2c18	c8		.
	ld a,b			;2c19	78		x
	cp e			;2c1a	bb		.
	ld a,01fh		;2c1b	3e 1f		> .
	ret nc			;2c1d	d0		.
	ld a,b			;2c1e	78		x
	sub d			;2c1f	92		.
	push af			;2c20	f5		.
	xor a			;2c21	af		.
	ld (0dde0h),a		;2c22	32 e0 dd	2 . .
	call sub_0be2h		;2c25	cd e2 0b	. . .
	push de			;2c28	d5		.
	call sub_0be2h		;2c29	cd e2 0b	. . .
	ex (sp),hl		;2c2c	e3		.
	ex de,hl		;2c2d	eb		.
	call sub_2ee1h		;2c2e	cd e1 2e	. . .
	ld (0dddch),de		;2c31	ed 53 dc dd	. S . .
	ld (0dddeh),hl		;2c35	22 de dd	" . .
	ld hl,(PagingState)	;2c38	2a dd df	* . .
	ld (0dde1h),hl		;2c3b	22 e1 dd	" . .
	ld (PagingBackup),hl	;2c3e	22 db df	" . .
	pop hl			;2c41	e1		.
	bit 0,(iy+013h)		;2c42	fd cb 13 46	. . . F
	jr z,l2c59h		;2c46	28 11		( .
	call sub_0be2h		;2c48	cd e2 0b	. . .
	ex de,hl		;2c4b	eb		.
	call sub_2f57h		;2c4c	cd 57 2f	. W /
	call RestorePagingShadow	;2c4f	cd 8c 2d	. . -
	ld hl,(PagingState)	;2c52	2a dd df	* . .
	ld (0dde1h),hl		;2c55	22 e1 dd	" . .
	ex de,hl		;2c58	eb		.
l2c59h:
	ld bc,0dde3h		;2c59	01 e3 dd	. . .
	pop af			;2c5c	f1		.
	ld (0dde0h),a		;2c5d	32 e0 dd	2 . .
l2c60h:
	push bc			;2c60	c5		.
	call sub_0c9fh		;2c61	cd 9f 0c	. . .
	pop bc			;2c64	c1		.
	jr c,SearchPattern	;2c65	38 05		8 .
	ld a,e			;2c67	7b		{
	ld (bc),a		;2c68	02		.
	inc bc			;2c69	03		.
	jr l2c60h		;2c6a	18 f4		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SearchPattern: wildcard search across banks through the
; bank window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SearchPattern:
	ld hl,(0dddch)		;2c6c	2a dc dd	* . .
l2c6fh:
	ld bc,(0dddfh)		;2c6f	ed 4b df dd	. K . .
	push hl			;2c73	e5		.
	ld de,0dde3h		;2c74	11 e3 dd	. . .
l2c77h:
	rst 28h			;2c77	ef		.
	ld c,a			;2c78	4f		O
	ld a,(de)		;2c79	1a		.
	cp c			;2c7a	b9		.
	jr nz,l2ca4h		;2c7b	20 27		  '
	inc hl			;2c7d	23		#
	inc de			;2c7e	13		.
	djnz l2c77h		;2c7f	10 f6		. .
	ld (0dddch),hl		;2c81	22 dc dd	" . .
	pop hl			;2c84	e1		.
	bit 0,(iy+013h)		;2c85	fd cb 13 46	. . . F
	jr nz,l2c8eh		;2c89	20 03		  .
	ld (0dd69h),hl		;2c8b	22 69 dd	" i .
l2c8eh:
	ld (0dda7h),hl		;2c8e	22 a7 dd	" . .
	ld a,084h		;2c91	3e 84		> .
l2c93h:
	push af			;2c93	f5		.
	call RestorePaging	;2c94	cd 84 35	. . 5
	ld a,(iy+00ah)		;2c97	fd 7e 0a	. ~ .
	and 0cfh		;2c9a	e6 cf		. .
	ld (iy+00ah),a		;2c9c	fd 77 0a	. w .
	call CommandLoop	;2c9f	cd e2 13	. . .
	pop af			;2ca2	f1		.
	ret			;2ca3	c9		.
l2ca4h:
	pop hl			;2ca4	e1		.
	push hl			;2ca5	e5		.
	or a			;2ca6	b7		.
	ld de,(0dddeh)		;2ca7	ed 5b de dd	. [ . .
	sbc hl,de		;2cab	ed 52		. R
	pop hl			;2cad	e1		.
	inc hl			;2cae	23		#
	jr c,l2c6fh		;2caf	38 be		8 .
	xor a			;2cb1	af		.
	ld (0dde0h),a		;2cb2	32 e0 dd	2 . .
	ld a,0a0h		;2cb5	3e a0		> .
	jr l2c93h		;2cb7	18 da		. .
sub_2cb9h:
	ld a,(0dde0h)		;2cb9	3a e0 dd	: . .
	or a			;2cbc	b7		.
	ld a,0a1h		;2cbd	3e a1		> .
	ret z			;2cbf	c8		.
	ld a,00ch		;2cc0	3e 0c		> .
	call sub_0a00h		;2cc2	cd 00 0a	. . .
	call SavePagingShadow	;2cc5	cd 80 2d	. . -
	ld hl,(0dde1h)		;2cc8	2a e1 dd	* . .
	ld (PagingState),hl	;2ccb	22 dd df	" . .
	jr SearchPattern	;2cce	18 9c		. .
	call sub_2eebh		;2cd0	cd eb 2e	. . .
	inc hl			;2cd3	23		#
	or a			;2cd4	b7		.
	sbc hl,de		;2cd5	ed 52		. R
	push bc			;2cd7	c5		.
	push hl			;2cd8	e5		.
	pop bc			;2cd9	c1		.
	pop hl			;2cda	e1		.
	ex de,hl		;2cdb	eb		.
	ld a,c			;2cdc	79		y
	or b			;2cdd	b0		.
	ld a,042h		;2cde	3e 42		> B
	ret z			;2ce0	c8		.
	push hl			;2ce1	e5		.
	sbc hl,de		;2ce2	ed 52		. R
	pop hl			;2ce4	e1		.
	jr c,l2cf2h		;2ce5	38 0b		8 .
l2ce7h:
	call sub_2d04h		;2ce7	cd 04 2d	. . -
	inc hl			;2cea	23		#
	inc de			;2ceb	13		.
	ld a,c			;2cec	79		y
	or b			;2ced	b0		.
	jr nz,l2ce7h		;2cee	20 f7		  .
	jr l2d01h		;2cf0	18 0f		. .
l2cf2h:
	dec bc			;2cf2	0b		.
	add hl,bc		;2cf3	09		.
	ex de,hl		;2cf4	eb		.
l2cf5h:
	add hl,bc		;2cf5	09		.
	ex de,hl		;2cf6	eb		.
	inc bc			;2cf7	03		.
l2cf8h:
	call sub_2d04h		;2cf8	cd 04 2d	. . -
	dec hl			;2cfb	2b		+
	dec de			;2cfc	1b		.
	ld a,b			;2cfd	78		x
	or c			;2cfe	b1		.
	jr nz,l2cf8h		;2cff	20 f7		  .
l2d01h:
	jp l2e19h		;2d01	c3 19 2e	. . .
sub_2d04h:
	call RestorePaging	;2d04	cd 84 35	. . 5
	rst 28h			;2d07	ef		.
l2d08h:
	call RestorePagingShadow	;2d08	cd 8c 2d	. . -
	ex de,hl		;2d0b	eb		.
	rst 0			;2d0c	c7		.
	ex de,hl		;2d0d	eb		.
	dec bc			;2d0e	0b		.
	ret			;2d0f	c9		.
	call sub_2eebh		;2d10	cd eb 2e	. . .
	push bc			;2d13	c5		.
	push hl			;2d14	e5		.
	pop bc			;2d15	c1		.
	pop hl			;2d16	e1		.
	ex de,hl		;2d17	eb		.
l2d18h:
	ex de,hl		;2d18	eb		.
	call RestorePagingShadow	;2d19	cd 8c 2d	. . -
	rst 28h			;2d1c	ef		.
	ex de,hl		;2d1d	eb		.
	call RestorePaging	;2d1e	cd 84 35	. . 5
	push bc			;2d21	c5		.
	ld c,a			;2d22	4f		O
	rst 28h			;2d23	ef		.
	cp c			;2d24	b9		.
	jr nz,l2d39h		;2d25	20 12		  .
l2d27h:
	pop bc			;2d27	c1		.
	push hl			;2d28	e5		.
	or a			;2d29	b7		.
	sbc hl,bc		;2d2a	ed 42		. B
	pop hl			;2d2c	e1		.
	inc de			;2d2d	13		.
	inc hl			;2d2e	23		#
	jr nz,l2d18h		;2d2f	20 e7		  .
	call sub_2d73h		;2d31	cd 73 2d	. s -
	ld a,082h		;2d34	3e 82		> .
	ret nz			;2d36	c0		.
	dec a			;2d37	3d		=
	ret			;2d38	c9		.
l2d39h:
	ld b,a			;2d39	47		G
	push hl			;2d3a	e5		.
	push de			;2d3b	d5		.
	push bc			;2d3c	c5		.
	call sub_2d73h		;2d3d	cd 73 2d	. s -
	call nz,InitErrorWindow	;2d40	c4 cb 35	. . 5
	ld b,041h		;2d43	06 41		. A
	rst 8			;2d45	cf		.
	pop bc			;2d46	c1		.
	pop de			;2d47	d1		.
	pop hl			;2d48	e1		.
	push hl			;2d49	e5		.
	push de			;2d4a	d5		.
	push bc			;2d4b	c5		.
	call PrintMarkerChar	;2d4c	cd d2 18	. . .
	rst 20h			;2d4f	e7		.
	jr nz,l2cf5h		;2d50	20 a3		  .
	pop af			;2d52	f1		.
	push af			;2d53	f5		.
	call PrintHexByte	;2d54	cd a3 19	. . .
	rst 20h			;2d57	e7		.
	jr nz,l2d94h		;2d58	20 3a		  :
	and b			;2d5a	a0		.
	pop bc			;2d5b	c1		.
	pop hl			;2d5c	e1		.
	push hl			;2d5d	e5		.
	push bc			;2d5e	c5		.
	call PrintMarkerChar	;2d5f	cd d2 18	. . .
	rst 20h			;2d62	e7		.
	jr nz,l2d08h		;2d63	20 a3		  .
	pop hl			;2d65	e1		.
	ld a,l			;2d66	7d		}
	call PrintHexByte	;2d67	cd a3 19	. . .
	rst 20h			;2d6a	e7		.
	adc a,l			;2d6b	8d		.
	call sub_2f3eh		;2d6c	cd 3e 2f	. > /
	pop de			;2d6f	d1		.
	pop hl			;2d70	e1		.
	jr l2d27h		;2d71	18 b4		. .
sub_2d73h:
	push de			;2d73	d5		.
	push ix			;2d74	dd e5		. .
	ex (sp),hl		;2d76	e3		.
	ld de,PromptWindowDef	;2d77	11 35 e0	. 5 .
	or a			;2d7a	b7		.
	sbc hl,de		;2d7b	ed 52		. R
	pop hl			;2d7d	e1		.
	pop de			;2d7e	d1		.
	ret			;2d7f	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SavePagingShadow / RestorePagingShadow ($2D8C): stash the
; real paging (DFDD) into the DFD9/DFDB shadows while the
; monitor reprograms banks.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SavePagingShadow:
	push hl			;2d80	e5		.
	ld hl,(PagingState)	;2d81	2a dd df	* . .
	ld (PagingBackup),hl	;2d84	22 db df	" . .
	ld (0dfd9h),hl		;2d87	22 d9 df	" . .
	pop hl			;2d8a	e1		.
	ret			;2d8b	c9		.
RestorePagingShadow:
	push hl			;2d8c	e5		.
	ld hl,(0dfd9h)		;2d8d	2a d9 df	* . .
	ld (PagingState),hl	;2d90	22 dd df	" . .
	pop hl			;2d93	e1		.
l2d94h:
	ret			;2d94	c9		.
	call sub_2eebh		;2d95	cd eb 2e	. . .
	ld a,b			;2d98	78		x
	or a			;2d99	b7		.
	ld a,00ch		;2d9a	3e 0c		> .
	ret nz			;2d9c	c0		.
	call RestorePagingShadow	;2d9d	cd 8c 2d	. . -
	ex de,hl		;2da0	eb		.
l2da1h:
	ld a,c			;2da1	79		y
	rst 0			;2da2	c7		.
	push hl			;2da3	e5		.
	or a			;2da4	b7		.
	sbc hl,de		;2da5	ed 52		. R
	pop hl			;2da7	e1		.
	inc hl			;2da8	23		#
	jr nz,l2da1h		;2da9	20 f6		  .
	jr l2e19h		;2dab	18 6c		. l
	call sub_0be2h		;2dad	cd e2 0b	. . .
	ld c,e			;2db0	4b		K
	ld b,d			;2db1	42		B
	jr l2db8h		;2db2	18 04		. .
	ld bc,(UserPc)		;2db4	ed 4b 6b dd	. K k .
l2db8h:
	set 1,(iy+013h)		;2db8	fd cb 13 ce	. . . .
	jr l2dc6h		;2dbc	18 08		. .
	ld bc,(0dd69h)		;2dbe	ed 4b 69 dd	. K i .
	res 1,(iy+013h)		;2dc2	fd cb 13 8e	. . . .
l2dc6h:
	push bc			;2dc6	c5		.
	call sub_0c9fh		;2dc7	cd 9f 0c	. . .
	pop bc			;2dca	c1		.
	jr c,l2e1ch		;2dcb	38 4f		8 O
	push hl			;2dcd	e5		.
	ld l,c			;2dce	69		i
	ld h,b			;2dcf	60		`
	ld a,e			;2dd0	7b		{
	rst 0			;2dd1	c7		.
	inc bc			;2dd2	03		.
	bit 1,(iy+013h)		;2dd3	fd cb 13 4e	. . . N
	jr nz,l2dddh		;2dd7	20 04		  .
	ld (0dd69h),bc		;2dd9	ed 43 69 dd	. C i .
l2dddh:
	pop hl			;2ddd	e1		.
	jr l2dc6h		;2dde	18 e6		. .
sub_2de0h:
	ld hl,(UserSp)		;2de0	2a 6d dd	* m .
	rst 28h			;2de3	ef		.
	ld e,a			;2de4	5f		_
	inc hl			;2de5	23		#
	rst 28h			;2de6	ef		.
	ld d,a			;2de7	57		W
	inc hl			;2de8	23		#
	ld (UserSp),hl		;2de9	22 6d dd	" m .
	ret			;2dec	c9		.
	call sub_2de0h		;2ded	cd e0 2d	. . -
	call sub_2b2dh		;2df0	cd 2d 2b	. - +
	jr l2e0eh		;2df3	18 19		. .
	ld hl,(0dd69h)		;2df5	2a 69 dd	* i .
	call sub_2fe5h		;2df8	cd e5 2f	. . /
	ex de,hl		;2dfb	eb		.
	ld (0dd69h),de		;2dfc	ed 53 69 dd	. S i .
	jr l2e1ch		;2e00	18 1a		. .
	ld a,001h		;2e02	3e 01		> .
	jr l2e08h		;2e04	18 02		. .
	ld a,002h		;2e06	3e 02		> .
l2e08h:
	xor (iy+00bh)		;2e08	fd ae 0b	. . .
	ld (iy+00bh),a		;2e0b	fd 77 0b	. w .
l2e0eh:
	ld a,(iy+00ah)		;2e0e	fd 7e 0a	. ~ .
l2e11h:
	and 0f1h		;2e11	e6 f1		. .
	ld (iy+00ah),a		;2e13	fd 77 0a	. w .
l2e16h:
	jp l0a49h		;2e16	c3 49 0a	. I .
l2e19h:
	call RestorePaging	;2e19	cd 84 35	. . 5
l2e1ch:
	ld a,(iy+00ah)		;2e1c	fd 7e 0a	. ~ .
	and 08fh		;2e1f	e6 8f		. .
	jr l2e11h		;2e21	18 ee		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CmdInterruptState: the EI/DI command - toggles the saved
; user IFF (DD83 bit 2) that the step trampoline applies as
; its DI/EI prefix.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CmdInterruptState:
	ld a,e			;2e23	7b		{
	and 0feh		;2e24	e6 fe		. .
	or d			;2e26	b2		.
	ld a,017h		;2e27	3e 17		> .
	ret nz			;2e29	c0		.
	ld hl,UserIff		;2e2a	21 83 dd	! . .
	ld a,e			;2e2d	7b		{
	or e			;2e2e	b3		.
	jr z,l2e35h		;2e2f	28 04		( .
	set 2,(hl)		;2e31	cb d6		. .
	jr l2e16h		;2e33	18 e1		. .
l2e35h:
	res 2,(hl)		;2e35	cb 96		. .
	jr l2e16h		;2e37	18 dd		. .
	call sub_2e98h		;2e39	cd 98 2e	. . .
	set 4,(iy+012h)		;2e3c	fd cb 12 e6	. . . .
	push af			;2e40	f5		.
	push de			;2e41	d5		.
	ld de,(UserPc)		;2e42	ed 5b 6b dd	. [ k .
	inc de			;2e46	13		.
	call sub_354ah		;2e47	cd 4a 35	. J 5
	ld de,Rst08Vector	;2e4a	11 08 00	. . .
	call sub_354ah		;2e4d	cd 4a 35	. J 5
	pop de			;2e50	d1		.
	pop af			;2e51	f1		.
	jr l2e57h		;2e52	18 03		. .
	call sub_2e98h		;2e54	cd 98 2e	. . .
l2e57h:
	jr c,l2e5dh		;2e57	38 04		8 .
	ld (UserPc),de		;2e59	ed 53 6b dd	. S k .
l2e5dh:
	jp l011ch		;2e5d	c3 1c 01	. . .
	ld a,e			;2e60	7b		{
	and 0feh		;2e61	e6 fe		. .
	or d			;2e63	b2		.
	ld a,019h		;2e64	3e 19		> .
	ret nz			;2e66	c0		.
	ld a,01bh		;2e67	3e 1b		> .
	bit 5,(iy-002h)		;2e69	fd cb fe 6e	. . . n
	ret nz			;2e6d	c0		.
	ld hl,l2e1ch		;2e6e	21 1c 2e	! . .
	ex (sp),hl		;2e71	e3		.
	res 4,(iy-002h)		;2e72	fd cb fe a6	. . . .
	bit 0,e			;2e76	cb 43		. C
	ret z			;2e78	c8		.
	set 4,(iy-002h)		;2e79	fd cb fe e6	. . . .
	ret			;2e7d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScreenBankCheck: is the visible screen the bank being
; edited? (shadow-screen test).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScreenBankCheck:
	push hl			;2e7e	e5		.
	ld hl,(PagingState)	;2e7f	2a dd df	* . .
	ld a,007h		;2e82	3e 07		> .
	and l			;2e84	a5		.
	bit 4,h			;2e85	cb 64		. d
	pop hl			;2e87	e1		.
	ret z			;2e88	c8		.
	or 008h			;2e89	f6 08		. .
	ret			;2e8b	c9		.
sub_2e8ch:
	call ScreenBankCheck	;2e8c	cd 7e 2e	. ~ .
	cp l			;2e8f	bd		.
	ret			;2e90	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CalcPagingPorts: bank number in HL -> the #7FFD/#1FFD byte
; pair (bank bits, RAM-at-$C000 bit, TR-DOS session bits),
; respecting the paging mirror.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CalcPagingPorts:
	ld bc,l2e1ch		;2e91	01 1c 2e	. . .
	push bc			;2e94	c5		.
	ex de,hl		;2e95	eb		.
	jr l2ea2h		;2e96	18 0a		. .
sub_2e98h:
	cp 001h			;2e98	fe 01		. .
	ret c			;2e9a	d8		.
	ret z			;2e9b	c8		.
	cp 002h			;2e9c	fe 02		. .
l2e9eh:
	ld a,00bh		;2e9e	3e 0b		> .
	jr nz,l2ee8h		;2ea0	20 46		  F
l2ea2h:
	ld a,l			;2ea2	7d		}
	and 0f0h		;2ea3	e6 f0		. .
	or h			;2ea5	b4		.
	ld a,018h		;2ea6	3e 18		> .
	jr nz,l2ee8h		;2ea8	20 3e		  >
	ld a,l			;2eaa	7d		}
	and 007h		;2eab	e6 07		. .
	jr z,sub_2eb7h		;2ead	28 08		( .
	ld a,01ch		;2eaf	3e 1c		> .
	bit 5,(iy-002h)		;2eb1	fd cb fe 6e	. . . n
	jr nz,l2ee8h		;2eb5	20 31		  1
sub_2eb7h:
	ld bc,(PagingState)	;2eb7	ed 4b dd df	. K . .
	res 4,b			;2ebb	cb a0		. .
	bit 3,l			;2ebd	cb 5d		. ]
	jr z,l2ec3h		;2ebf	28 02		( .
	set 4,b			;2ec1	cb e0		. .
l2ec3h:
	res 3,l			;2ec3	cb 9d		. .
	ld a,c			;2ec5	79		y
	and 0f8h		;2ec6	e6 f8		. .
	or l			;2ec8	b5		.
	ld c,a			;2ec9	4f		O
	ld (PagingState),bc	;2eca	ed 43 dd df	. C . .
	xor a			;2ece	af		.
	ret			;2ecf	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckAddressRange: validate a command address range (carry
; on error, message via ReportError).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckAddressRange:
	cp 002h			;2ed0	fe 02		. .
	jr z,sub_2ee1h		;2ed2	28 0d		( .
	jr nc,l2e9eh		;2ed4	30 c8		0 .
	ld hl,0ffffh		;2ed6	21 ff ff	! . .
	cp 001h			;2ed9	fe 01		. .
	jr z,sub_2ee1h		;2edb	28 04		( .
	ld de,(0dd69h)		;2edd	ed 5b 69 dd	. [ i .
sub_2ee1h:
	or a			;2ee1	b7		.
	sbc hl,de		;2ee2	ed 52		. R
	add hl,de		;2ee4	19		.
	ret nc			;2ee5	d0		.
	ld a,01dh		;2ee6	3e 1d		> .
l2ee8h:
	jp ExitToError		;2ee8	c3 54 0a	. T .
sub_2eebh:
	call SavePagingShadow	;2eeb	cd 80 2d	. . -
	cp 004h			;2eee	fe 04		. .
	call z,sub_2f5ch	;2ef0	cc 5c 2f	. \ /
	jr z,sub_2ee1h		;2ef3	28 ec		( .
	cp 003h			;2ef5	fe 03		. .
	jr z,sub_2ee1h		;2ef7	28 e8		( .
	ld a,00ah		;2ef9	3e 0a		> .
	jr l2ee8h		;2efb	18 eb		. .
	cp 001h			;2efd	fe 01		. .
	jr z,l2f0ch		;2eff	28 0b		( .
	jr nc,l2e9eh		;2f01	30 9b		0 .
	call CallTrdos		;2f03	cd 57 35	. W 5
	call sub_2f85h		;2f06	cd 85 2f	. . /
	jp l0a40h		;2f09	c3 40 0a	. @ .
l2f0ch:
	ld a,e			;2f0c	7b		{
	and 0feh		;2f0d	e6 fe		. .
	or d			;2f0f	b2		.
	jr nz,l2f1fh		;2f10	20 0d		  .
	ld a,081h		;2f12	3e 81		> .
	ld hl,StepFlags		;2f14	21 f1 df	! . .
	set 1,(hl)		;2f17	cb ce		. .
	bit 0,e			;2f19	cb 43		. C
	ret z			;2f1b	c8		.
	res 1,(hl)		;2f1c	cb 8e		. .
	ret			;2f1e	c9		.
l2f1fh:
	ld a,d			;2f1f	7a		z
	or a			;2f20	b7		.
	ld a,017h		;2f21	3e 17		> .
	ret nz			;2f23	c0		.
	ld hl,PagingState	;2f24	21 dd df	! . .
	ld a,e			;2f27	7b		{
	cp 005h			;2f28	fe 05		. .
	jr z,l2f33h		;2f2a	28 07		( .
	cp 007h			;2f2c	fe 07		. .
	jr z,l2f3ah		;2f2e	28 0a		( .
	ld a,017h		;2f30	3e 17		> .
	ret			;2f32	c9		.
l2f33h:
	res 3,(hl)		;2f33	cb 9e		. .
l2f35h:
	ld a,081h		;2f35	3e 81		> .
	jp l0a49h		;2f37	c3 49 0a	. I .
l2f3ah:
	set 3,(hl)		;2f3a	cb de		. .
	jr l2f35h		;2f3c	18 f7		. .
sub_2f3eh:
	call sub_2f7fh		;2f3e	cd 7f 2f	. . /
	ld a,0feh		;2f41	3e fe		> .
	in a,(0feh)		;2f43	db fe		. .
	rrca			;2f45	0f		.
	ret c			;2f46	d8		.
	ld a,0f7h		;2f47	3e f7		> .
	in a,(0feh)		;2f49	db fe		. .
	rrca			;2f4b	0f		.
	ret c			;2f4c	d8		.
	call sub_2f90h		;2f4d	cd 90 2f	. . /
l2f50h:
	call sub_0839h		;2f50	cd 39 08	. 9 .
	ld a,09ah		;2f53	3e 9a		> .
	jr l2ee8h		;2f55	18 91		. .
sub_2f57h:
	push af			;2f57	f5		.
	push hl			;2f58	e5		.
	push bc			;2f59	c5		.
	jr l2f62h		;2f5a	18 06		. .
sub_2f5ch:
	push af			;2f5c	f5		.
	push hl			;2f5d	e5		.
	push bc			;2f5e	c5		.
	ld hl,(0dd9fh)		;2f5f	2a 9f dd	* . .
l2f62h:
	call l2ea2h		;2f62	cd a2 2e	. . .
	ld hl,(PagingState)	;2f65	2a dd df	* . .
	ld (0dfd9h),hl		;2f68	22 d9 df	" . .
	call RestorePaging	;2f6b	cd 84 35	. . 5
	pop bc			;2f6e	c1		.
	pop hl			;2f6f	e1		.
	pop af			;2f70	f1		.
	scf			;2f71	37		7
	ret			;2f72	c9		.
sub_2f73h:
	ld a,0c0h		;2f73	3e c0		> .
l2f75h:
	push bc			;2f75	c5		.
	ld b,000h		;2f76	06 00		. .
l2f78h:
	djnz l2f78h		;2f78	10 fe		. .
	pop bc			;2f7a	c1		.
	dec a			;2f7b	3d		=
	jr nz,l2f75h		;2f7c	20 f7		  .
	ret			;2f7e	c9		.
sub_2f7fh:
	ld a,07fh		;2f7f	3e 7f		> .
	in a,(0feh)		;2f81	db fe		. .
	rrca			;2f83	0f		.
	ret c			;2f84	d8		.
sub_2f85h:
	call sub_2f73h		;2f85	cd 73 2f	. s /
l2f88h:
	in a,(0feh)		;2f88	db fe		. .
	cpl			;2f8a	2f		/
	and 01fh		;2f8b	e6 1f		. .
	ret nz			;2f8d	c0		.
	jr l2f88h		;2f8e	18 f8		. .
sub_2f90h:
	xor a			;2f90	af		.
	in a,(0feh)		;2f91	db fe		. .
	cpl			;2f93	2f		/
	and 01fh		;2f94	e6 1f		. .
	ret z			;2f96	c8		.
	jr sub_2f90h		;2f97	18 f7		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DecodeXorTable: XOR-unmask a ROM table into RAM using the
; keys at XorDecodeKeys ($00FC).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DecodeXorTable:
	ex (sp),hl		;2f99	e3		.
	inc hl			;2f9a	23		#
	inc hl			;2f9b	23		#
	ld b,(hl)		;2f9c	46		F
	inc hl			;2f9d	23		#
	push de			;2f9e	d5		.
	ld e,(hl)		;2f9f	5e		^
	inc hl			;2fa0	23		#
	ld d,000h		;2fa1	16 00		. .
	ld a,(de)		;2fa3	1a		.
	pop de			;2fa4	d1		.
	ex (sp),hl		;2fa5	e3		.
	ld c,a			;2fa6	4f		O
	ld a,(l00feh)		;2fa7	3a fe 00	: . .
	xor c			;2faa	a9		.
	ld c,a			;2fab	4f		O
	ld a,(l00fdh)		;2fac	3a fd 00	: . .
	xor c			;2faf	a9		.
	ld c,a			;2fb0	4f		O
	ld a,(Filler00F1_end)	;2fb1	3a fc 00	: . .
	xor c			;2fb4	a9		.
	ld c,a			;2fb5	4f		O
l2fb6h:
	ld a,c			;2fb6	79		y
	xor b			;2fb7	a8		.
	xor (hl)		;2fb8	ae		.
	ld (de),a		;2fb9	12		.
	inc hl			;2fba	23		#
	inc de			;2fbb	13		.
	djnz l2fb6h		;2fbc	10 f8		. .
	ret			;2fbe	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RunCommand: walk the command table, execute the matching
; entry (jp (HL) per entry), wait for a key, return to the
; main loop.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RunCommand:
	scf			;2fbf	37		7
	jr l2fc3h		;2fc0	18 01		. .
	or a			;2fc2	b7		.
l2fc3h:
	pop hl			;2fc3	e1		.
	ld e,(hl)		;2fc4	5e		^
	inc hl			;2fc5	23		#
	ld d,(hl)		;2fc6	56		V
	inc hl			;2fc7	23		#
	push hl			;2fc8	e5		.
	ex de,hl		;2fc9	eb		.
	jr c,l2fe1h		;2fca	38 15		8 .
	ld a,(hl)		;2fcc	7e		~
	or a			;2fcd	b7		.
	jr nz,l2fe4h		;2fce	20 14		  .
	ld a,03dh		;2fd0	3e 3d		> =
l2fd2h:
	sla a			;2fd2	cb 27		. '
	call sub_09deh		;2fd4	cd de 09	. . .
	scf			;2fd7	37		7
	call sub_0a00h		;2fd8	cd 00 0a	. . .
	call sub_2f85h		;2fdb	cd 85 2f	. . /
	jp l0108h		;2fde	c3 08 01	. . .
l2fe1h:
	ld a,(hl)		;2fe1	7e		~
	or a			;2fe2	b7		.
	ret z			;2fe3	c8		.
l2fe4h:
	jp (hl)			;2fe4	e9		.
sub_2fe5h:
	push hl			;2fe5	e5		.
	call FetchPrefixBytes	;2fe6	cd b1 16	. . .
	pop hl			;2fe9	e1		.
	bit 6,(iy+001h)		;2fea	fd cb 01 76	. . . v
	ret z			;2fee	c8		.
	ld hl,(StepPc)		;2fef	2a d0 dd	* . .
	ret			;2ff2	c9		.
	xor a			;2ff3	af		.
	call AddBreakpoint	;2ff4	cd 25 30	. % 0
	ex de,hl		;2ff7	eb		.
	call sub_2fe5h		;2ff8	cd e5 2f	. . /
	jr l3022h		;2ffb	18 25		. %
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BreakpointSlot: address of breakpoint slot A (DD8D-based
; table).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BreakpointSlot:
	ld l,a			;2ffd	6f		o
	ld h,000h		;2ffe	26 00		& .
	ld de,0dd8dh		;3000	11 8d dd	. . .
	add hl,hl		;3003	29		)
	add hl,de		;3004	19		.
	ret			;3005	c9		.
	ld a,0c3h		;3006	3e c3		> .
l3008h:
	call sub_1177h		;3008	cd 77 11	. w .
	jr c,l3030h		;300b	38 23		8 #
	bit 4,(iy+005h)		;300d	fd cb 05 66	. . . f
	push af			;3011	f5		.
	call z,sub_0c9fh	;3012	cc 9f 0c	. . .
	pop af			;3015	f1		.
	call nz,00d3eh		;3016	c4 3e 0d	. > .
	jr nz,l3008h		;3019	20 ed		  .
	push de			;301b	d5		.
	ld a,00ch		;301c	3e 0c		> .
	call sub_0a00h		;301e	cd 00 0a	. . .
	pop hl			;3021	e1		.
l3022h:
	jp l30ach		;3022	c3 ac 30	. . 0
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AddBreakpoint: program a slot - address plus the original
; bytes saved for clean removal.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AddBreakpoint:
	call BreakpointSlot	;3025	cd fd 2f	. . /
	ld de,(0dd8bh)		;3028	ed 5b 8b dd	. [ . .
	ld (hl),e		;302c	73		s
	inc hl			;302d	23		#
	ld (hl),d		;302e	72		r
	ret			;302f	c9		.
l3030h:
	res 1,(ix+007h)		;3030	dd cb 07 8e	. . . .
	jp l2f50h		;3034	c3 50 2f	. P /
sub_3037h:
	xor a			;3037	af		.
	ld hl,(UserPc)		;3038	2a 6b dd	* k .
	bit 6,(iy+005h)		;303b	fd cb 05 76	. . . v
	jr nz,l3044h		;303f	20 03		  .
	ld hl,(0dd69h)		;3041	2a 69 dd	* i .
l3044h:
	or a			;3044	b7		.
	jr z,l304dh		;3045	28 06		( .
	cp 002h			;3047	fe 02		. .
	ld a,00bh		;3049	3e 0b		> .
	ret nc			;304b	d0		.
	ex de,hl		;304c	eb		.
l304dh:
	ld (0dd8bh),hl		;304d	22 8b dd	" . .
	ld a,00ch		;3050	3e 0c		> .
	call sub_0a00h		;3052	cd 00 0a	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ListBreakpoints: the breakpoint list window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ListBreakpoints:
	call InitErrorWindow	;3055	cd cb 35	. . 5
	ld (ix+000h),000h	;3058	dd 36 00 00	. 6 . .
	set 1,(ix+007h)		;305c	dd cb 07 ce	. . . .
	ld hl,(0dd8bh)		;3060	2a 8b dd	* . .
	ld b,016h		;3063	06 16		. .
	jr l306ah		;3065	18 03		. .
l3067h:
	inc (ix+000h)		;3067	dd 34 00	. 4 .
l306ah:
	push bc			;306a	c5		.
	call ArmWatchpoint	;306b	cd 05 31	. . 1
	pop bc			;306e	c1		.
	djnz l3067h		;306f	10 f6		. .
l3071h:
	ld hl,l3071h		;3071	21 71 30	! q 0
	push hl			;3074	e5		.
l3075h:
	call AutoRepeatKey	;3075	cd 16 17	. . .
	call ScanTokens		;3078	cd b5 13	. . .
	call l3030h		;307b	cd 30 30	. 0 0
	rlca			;307e	07		.
	jr nc,l30b1h		;307f	30 30		0 0
	ex af,af'		;3081	08		.
	xor (hl)		;3082	ae		.
	jr nc,l308eh		;3083	30 09		0 .
	or e			;3085	b3		.
	jr nc,l3092h		;3086	30 0a		0 .
	call pe,00b30h		;3088	ec 30 0b	. 0 .
	ret nz			;308b	c0		.
	jr nc,l309bh		;308c	30 0d		0 .
l308eh:
	ld b,030h		;308e	06 30		. 0
	set 6,e			;3090	cb f3		. .
l3092h:
	cpl			;3092	2f		/
	ld h,d			;3093	62		b
	add a,02ah		;3094	c6 2a		. *
	ld l,(hl)		;3096	6e		n
	pop hl			;3097	e1		.
	ld hl,(0d6ffh)		;3098	2a ff d6	* . .
l309bh:
	jr nc,l3075h		;309b	30 d8		0 .
	ld c,005h		;309d	0e 05		. .
	cp c			;309f	b9		.
	jr c,AddBreakpoint	;30a0	38 83		8 .
	sub c			;30a2	91		.
	cp c			;30a3	b9		.
	ret nc			;30a4	d0		.
	call BreakpointSlot	;30a5	cd fd 2f	. . /
	ld e,(hl)		;30a8	5e		^
	inc hl			;30a9	23		#
	ld d,(hl)		;30aa	56		V
	ex de,hl		;30ab	eb		.
l30ach:
	jr l30bah		;30ac	18 0c		. .
	ld bc,0ffffh		;30ae	01 ff ff	. . .
l30b1h:
	jr l30b6h		;30b1	18 03		. .
	ld bc,l0001h		;30b3	01 01 00	. . .
l30b6h:
	ld hl,(0dd8bh)		;30b6	2a 8b dd	* . .
	add hl,bc		;30b9	09		.
l30bah:
	ld (0dd8bh),hl		;30ba	22 8b dd	" . .
l30bdh:
	pop hl			;30bd	e1		.
	jr ListBreakpoints	;30be	18 95		. .
	call ScrollDown		;30c0	cd 18 26	. . &
	ld (ix+000h),000h	;30c3	dd 36 00 00	. 6 . .
	ld bc,0fffbh		;30c7	01 fb ff	. . .
l30cah:
	inc bc			;30ca	03		.
	ld hl,(0dd8bh)		;30cb	2a 8b dd	* . .
	add hl,bc		;30ce	09		.
	push bc			;30cf	c5		.
	call FetchPrefixBytes	;30d0	cd b1 16	. . .
	pop bc			;30d3	c1		.
	ld de,(0dd8bh)		;30d4	ed 5b 8b dd	. [ . .
	or a			;30d8	b7		.
	sbc hl,de		;30d9	ed 52		. R
	jr z,l30dfh		;30db	28 02		( .
	jr nc,l30e8h		;30dd	30 09		0 .
l30dfh:
	add hl,de		;30df	19		.
	jr nz,l30cah		;30e0	20 e8		  .
	add hl,bc		;30e2	09		.
	ld (0dd8bh),hl		;30e3	22 8b dd	" . .
	jr ArmWatchpoint	;30e6	18 1d		. .
l30e8h:
	add hl,de		;30e8	19		.
	add hl,bc		;30e9	09		.
	jr l30bah		;30ea	18 ce		. .
	call ScrollUp		;30ec	cd 8b 25	. . %
	ld (ix+000h),015h	;30ef	dd 36 00 15	. 6 . .
	ld hl,(0dd8bh)		;30f3	2a 8b dd	* . .
	call FetchPrefixBytes	;30f6	cd b1 16	. . .
	ld (0dd8bh),hl		;30f9	22 8b dd	" . .
	ld b,015h		;30fc	06 15		. .
l30feh:
	push bc			;30fe	c5		.
	call FetchPrefixBytes	;30ff	cd b1 16	. . .
	pop bc			;3102	c1		.
	djnz l30feh		;3103	10 f9		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ArmWatchpoint: activate a watch entry (address / length /
; value).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ArmWatchpoint:
	ld a,(ix+006h)		;3105	dd 7e 06	. ~ .
	push af			;3108	f5		.
	ld (ix+001h),000h	;3109	dd 36 01 00	. 6 . .
	push ix			;310d	dd e5		. .
	call InitWatchTable	;310f	cd c9 29	. . )
l3112h:
	bit 7,(ix+000h)		;3112	dd cb 00 7e	. . . ~
	jr z,l313ah		;3116	28 22		( "
	push hl			;3118	e5		.
	ld l,(ix+001h)		;3119	dd 6e 01	. n .
	call sub_2e8ch		;311c	cd 8c 2e	. . .
	pop hl			;311f	e1		.
	jr nz,l313ah		;3120	20 18		  .
	push de			;3122	d5		.
	ld e,(ix+002h)		;3123	dd 5e 02	. ^ .
	ld d,(ix+003h)		;3126	dd 56 03	. V .
	or a			;3129	b7		.
	sbc hl,de		;312a	ed 52		. R
	add hl,de		;312c	19		.
	pop de			;312d	d1		.
	jr nz,l313ah		;312e	20 0a		  .
	ld a,(0e0a9h)		;3130	3a a9 e0	: . .
	ex (sp),ix		;3133	dd e3		. .
	ld (ix+006h),a		;3135	dd 77 06	. w .
	ex (sp),ix		;3138	dd e3		. .
l313ah:
	add ix,de		;313a	dd 19		. .
	djnz l3112h		;313c	10 d4		. .
	pop ix			;313e	dd e1		. .
	call sub_16c7h		;3140	cd c7 16	. . .
	pop af			;3143	f1		.
	ld (ix+006h),a		;3144	dd 77 06	. w .
	ret			;3147	c9		.
	set 4,(iy+009h)		;3148	fd cb 09 e6	. . . .
	or a			;314c	b7		.
	jr z,l3161h		;314d	28 12		( .
	cp 002h			;314f	fe 02		. .
	ld a,00bh		;3151	3e 0b		> .
	jr nc,l3164h		;3153	30 0f		0 .
	ld a,e			;3155	7b		{
	and 0feh		;3156	e6 fe		. .
	or d			;3158	b2		.
	ld a,03eh		;3159	3e 3e		> >
	jr nz,l3164h		;315b	20 07		  .
	ld a,e			;315d	7b		{
	ld (0ffcah),a		;315e	32 ca ff	2 . .
l3161h:
	call l316ch		;3161	cd 6c 31	. l 1
l3164h:
	call sub_0839h		;3164	cd 39 08	. 9 .
	ld a,081h		;3167	3e 81		> .
	jp l0a2eh		;3169	c3 2e 0a	. . .
l316ch:
	call InitErrorWindow	;316c	cd cb 35	. . 5
	ld (iy+008h),020h	;316f	fd 36 08 20	. 6 .  
	ld hl,(0ffd0h)		;3173	2a d0 ff	* . .
	push hl			;3176	e5		.
	rst 18h			;3177	df		.
	call z,0da38h		;3178	cc 38 da	. 8 .
	ld (hl),d		;317b	72		r
	ld (006c5h),a		;317c	32 c5 06	2 . .
	inc c			;317f	0c		.
	rst 8			;3180	cf		.
	rst 20h			;3181	e7		.
	and d			;3182	a2		.
	ld hl,0e4ech		;3183	21 ec e4	! . .
	ld b,008h		;3186	06 08		. .
	call l1885h		;3188	cd 85 18	. . .
	rst 20h			;318b	e7		.
	ld (0f18dh),hl		;318c	22 8d f1	" . .
	push af			;318f	f5		.
	bit 5,a			;3190	cb 6f		. o
	ld b,039h		;3192	06 39		. 9
	jr z,l3197h		;3194	28 01		( .
	inc b			;3196	04		.
l3197h:
	rst 8			;3197	cf		.
	ld b,038h		;3198	06 38		. 8
	rst 8			;319a	cf		.
	pop af			;319b	f1		.
	rlca			;319c	07		.
	ld b,03ch		;319d	06 3c		. <
	jr nc,l31a2h		;319f	30 01		0 .
	inc b			;31a1	04		.
l31a2h:
	rst 8			;31a2	cf		.
	ld b,03bh		;31a3	06 3b		. ;
	rst 8			;31a5	cf		.
	rst 20h			;31a6	e7		.
	xor b			;31a7	a8		.
	ld a,(0e4deh)		;31a8	3a de e4	: . .
	call sub_3281h		;31ab	cd 81 32	. . 2
	rst 20h			;31ae	e7		.
	xor a			;31af	af		.
	ld a,(0e4dah)		;31b0	3a da e4	: . .
	call sub_3281h		;31b3	cd 81 32	. . 2
	rst 20h			;31b6	e7		.
	add hl,hl		;31b7	29		)
	dec c			;31b8	0d		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg31B9: disk-catalog message File.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg31B9' (start 0x31b9 end 0x31bd)
Msg31B9_start:
	defb 046h		;31b9	46		F
	defb 069h		;31ba	69		i
	defb 06ch		;31bb	6c		l
	defb 0e5h		;31bc	e5		.
Msg31B9_end:
	ld a,(0e4dbh)		;31bd	3a db e4	: . .
	call sub_328ah		;31c0	cd 8a 32	. . 2
	rst 20h			;31c3	e7		.
	xor a			;31c4	af		.
	ld a,(0e4ebh)		;31c5	3a eb e4	: . .
	call sub_328ah		;31c8	cd 8a 32	. . 2
	rst 20h			;31cb	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg31CC: disk-catalog message , free.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg31CC' (start 0x31cc end 0x31d2)
Msg31CC_start:
	defb 02ch		;31cc	2c		,
	defb 020h		;31cd	20		 
	defb 066h		;31ce	66		f
	defb 072h		;31cf	72		r
	defb 065h		;31d0	65		e
	defb 0e5h		;31d1	e5		.
Msg31CC_end:
	ld bc,(0e4dch)		;31d2	ed 4b dc e4	. K . .
	call sub_191eh		;31d6	cd 1e 19	. . .
	rst 20h			;31d9	e7		.
	jr nz,$-86		;31da	20 a8		  .
	ld a,(0e4d8h)		;31dc	3a d8 e4	: . .
	call sub_3281h		;31df	cd 81 32	. . 2
	rst 20h			;31e2	e7		.
	xor a			;31e3	af		.
	ld a,(0e4d9h)		;31e4	3a d9 e4	: . .
	call sub_3281h		;31e7	cd 81 32	. . 2
	rst 20h			;31ea	e7		.
	add hl,hl		;31eb	29		)
	dec c			;31ec	0d		.
	adc a,l			;31ed	8d		.
l31eeh:
	ld b,010h		;31ee	06 10		. .
l31f0h:
	push bc			;31f0	c5		.
	rst 18h			;31f1	df		.
	add hl,de		;31f2	19		.
	add hl,sp		;31f3	39		9
	pop bc			;31f4	c1		.
	jp c,l3272h		;31f5	da 72 32	. r 2
	ld a,(hl)		;31f8	7e		~
	or a			;31f9	b7		.
	jr z,l326bh		;31fa	28 6f		( o
	push bc			;31fc	c5		.
	dec a			;31fd	3d		=
	jr nz,l3210h		;31fe	20 10		  .
	inc hl			;3200	23		#
	bit 4,(iy+009h)		;3201	fd cb 09 66	. . . f
	ld a,082h		;3205	3e 82		> .
	jr z,l320bh		;3207	28 02		( .
	ld a,03fh		;3209	3e 3f		> ?
l320bh:
	rst 10h			;320b	d7		.
	ld b,007h		;320c	06 07		. .
	jr l3212h		;320e	18 02		. .
l3210h:
	ld b,008h		;3210	06 08		. .
l3212h:
	call l1885h		;3212	cd 85 18	. . .
	rst 20h			;3215	e7		.
	jr nz,$-66		;3216	20 bc		  .
	ld a,(hl)		;3218	7e		~
	inc hl			;3219	23		#
	rst 10h			;321a	d7		.
	rst 20h			;321b	e7		.
	cp (hl)			;321c	be		.
	ld b,002h		;321d	06 02		. .
l321fh:
	push bc			;321f	c5		.
	ld c,(hl)		;3220	4e		N
	inc hl			;3221	23		#
	ld b,(hl)		;3222	46		F
	inc hl			;3223	23		#
	push hl			;3224	e5		.
	call sub_1921h		;3225	cd 21 19	. ! .
	pop hl			;3228	e1		.
	pop bc			;3229	c1		.
	djnz l321fh		;322a	10 f3		. .
	rst 20h			;322c	e7		.
	and b			;322d	a0		.
	ld a,(hl)		;322e	7e		~
	inc hl			;322f	23		#
	push hl			;3230	e5		.
	call sub_328ah		;3231	cd 8a 32	. . 2
	pop hl			;3234	e1		.
	ld a,01eh		;3235	3e 1e		> .
	call sub_170ch		;3237	cd 0c 17	. . .
	ld b,002h		;323a	06 02		. .
l323ch:
	push bc			;323c	c5		.
	rst 20h			;323d	e7		.
	and b			;323e	a0		.
	ld a,(hl)		;323f	7e		~
	inc hl			;3240	23		#
	push hl			;3241	e5		.
	call sub_3281h		;3242	cd 81 32	. . 2
	pop hl			;3245	e1		.
	pop bc			;3246	c1		.
	djnz l323ch		;3247	10 f3		. .
	rst 20h			;3249	e7		.
	adc a,l			;324a	8d		.
	pop bc			;324b	c1		.
	djnz l31f0h		;324c	10 a2		. .
	call sub_2d73h		;324e	cd 73 2d	. s -
l3251h:
	jr nz,l31eeh		;3251	20 9b		  .
	rst 20h			;3253	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3254: message More... (listing pager).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg3254' (start 0x3254 end 0x325b)
Msg3254_start:
	defb 04dh		;3254	4d		M
	defb 06fh		;3255	6f		o
	defb 072h		;3256	72		r
	defb 065h		;3257	65		e
	defb 02eh		;3258	2e		.
	defb 02eh		;3259	2e		.
	defb 0aeh		;325a	ae		.
Msg3254_end:
	ld (ix+001h),000h	;325b	dd 36 01 00	. 6 . .
	call AutoRepeatKey	;325f	cd 16 17	. . .
	cp 007h			;3262	fe 07		. .
	jr nz,l3251h		;3264	20 eb		  .
	ld a,00ah		;3266	3e 0a		> .
	call sub_170ch		;3268	cd 0c 17	. . .
l326bh:
	rst 20h			;326b	e7		.
	dec c			;326c	0d		.
	ld b,l			;326d	45		E
	ld c,(hl)		;326e	4e		N
	call nz,l0518h		;326f	c4 18 05	. . .
l3272h:
	ld b,a			;3272	47		G
	call sub_3472h		;3273	cd 72 34	. r 4
	rst 8			;3276	cf		.
	call sub_0839h		;3277	cd 39 08	. 9 .
	pop hl			;327a	e1		.
	ld (0ffd0h),hl		;327b	22 d0 ff	" . .
	jp sub_2f85h		;327e	c3 85 2f	. . /
sub_3281h:
	ld b,000h		;3281	06 00		. .
	ld c,a			;3283	4f		O
	ld de,l1023h		;3284	11 23 10	. # .
	jp sub_18ech		;3287	c3 ec 18	. . .
sub_328ah:
	ld c,a			;328a	4f		O
	ld b,000h		;328b	06 00		. .
	jp sub_191eh		;328d	c3 1e 19	. . .
	ld d,000h		;3290	16 00		. .
	ld a,(l00fbh)		;3292	3a fb 00	: . .
	sub (iy-007h)		;3295	fd 96 f9	. . .
	jr z,l329ch		;3298	28 02		( .
	res 0,e			;329a	cb 83		. .
l329ch:
	ld hl,0e3f7h		;329c	21 f7 e3	! . .
	ld bc,l0105h		;329f	01 05 01	. . .
	jp sub_02d6h		;32a2	c3 d6 02	. . .
l32a5h:
	ld hl,l11beh		;32a5	21 be 11	! . .
	ex (sp),hl		;32a8	e3		.
	call sub_34e8h		;32a9	cd e8 34	. . 4
	ret nz			;32ac	c0		.
	ld (ix+000h),015h	;32ad	dd 36 00 15	. 6 . .
	ld bc,Rst10Vector	;32b1	01 10 00	. . .
	call sub_02d6h		;32b4	cd d6 02	. . .
	ld d,027h		;32b7	16 27		. '
	bit 5,a			;32b9	cb 6f		. o
	jr z,l32bfh		;32bb	28 02		( .
	ld d,04fh		;32bd	16 4f		. O
l32bfh:
	ld bc,Rst10Vector	;32bf	01 10 00	. . .
	call sub_02d6h		;32c2	cd d6 02	. . .
	ld e,001h		;32c5	1e 01		. .
	rlca			;32c7	07		.
	jr c,l32cch		;32c8	38 02		8 .
l32cah:
	ld e,000h		;32ca	1e 00		. .
l32cch:
	push de			;32cc	d5		.
	ld (ix+001h),000h	;32cd	dd 36 01 00	. 6 . .
	call FillRowToEnd	;32d1	cd 82 26	. . &
	pop bc			;32d4	c1		.
	push bc			;32d5	c5		.
	ld (iy+007h),001h	;32d6	fd 36 07 01	. 6 . .
	ld c,b			;32da	48		H
	ld b,000h		;32db	06 00		. .
	call sub_192eh		;32dd	cd 2e 19	. . .
	rst 20h			;32e0	e7		.
	xor a			;32e1	af		.
	pop bc			;32e2	c1		.
	push bc			;32e3	c5		.
	ld b,000h		;32e4	06 00		. .
	call sub_192eh		;32e6	cd 2e 19	. . .
	call LookupToken	;32e9	cd a8 34	. . 4
	ld b,a			;32ec	47		G
	ld (ix+001h),005h	;32ed	dd 36 01 05	. 6 . .
	ld (ix+00ah),008h	;32f1	dd 36 0a 08	. 6 . .
l32f5h:
	ld a,082h		;32f5	3e 82		> .
	rst 10h			;32f7	d7		.
	djnz l32f5h		;32f8	10 fb		. .
	ld (ix+00ah),006h	;32fa	dd 36 0a 06	. 6 . .
	rst 20h			;32fe	e7		.
	adc a,l			;32ff	8d		.
	bit 7,(iy+013h)		;3300	fd cb 13 7e	. . . ~
	jr z,l3314h		;3304	28 0e		( .
	rst 20h			;3306	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3307: message Formating. (disk format).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg3307' (start 0x3307 end 0x3311)
Msg3307_start:
	defb 046h		;3307	46		F
	defb 06fh		;3308	6f		o
	defb 072h		;3309	72		r
	defb 06dh		;330a	6d		m
	defb 061h		;330b	61		a
	defb 074h		;330c	74		t
	defb 069h		;330d	69		i
	defb 06eh		;330e	6e		n
	defb 067h		;330f	67		g
	defb 0aeh		;3310	ae		.
Msg3307_end:
	call PrintFrameCount	;3311	cd 7d 34	. } 4
l3314h:
	pop de			;3314	d1		.
	push de			;3315	d5		.
	ld a,e			;3316	7b		{
	ld c,00eh		;3317	0e 0e		. .
	call sub_02d6h		;3319	cd d6 02	. . .
	jr c,l337eh		;331c	38 60		8 `
	call LookupToken	;331e	cd a8 34	. . 4
	pop de			;3321	d1		.
	ld c,000h		;3322	0e 00		. .
	ld hl,0e3f7h		;3324	21 f7 e3	! . .
	push hl			;3327	e5		.
l3328h:
	ld (hl),000h		;3328	36 00		6 .
	inc hl			;332a	23		#
	ld (hl),d		;332b	72		r
	inc hl			;332c	23		#
	ld (hl),e		;332d	73		s
	inc hl			;332e	23		#
	inc c			;332f	0c		.
	ld (hl),000h		;3330	36 00		6 .
	inc hl			;3332	23		#
	ld (hl),b		;3333	70		p
	inc hl			;3334	23		#
	cp c			;3335	b9		.
	jr nz,l3328h		;3336	20 f0		  .
	ld (hl),0feh		;3338	36 fe		6 .
	ex de,hl		;333a	eb		.
	push hl			;333b	e5		.
	ld c,001h		;333c	0e 01		. .
l333eh:
	ld hl,0e3fah		;333e	21 fa e3	! . .
	ld b,000h		;3341	06 00		. .
l3343h:
	ld a,(hl)		;3343	7e		~
	or a			;3344	b7		.
	jr nz,l334ch		;3345	20 05		  .
	ld (hl),c		;3347	71		q
	inc c			;3348	0c		.
	inc b			;3349	04		.
	jr l3353h		;334a	18 07		. .
l334ch:
	call sub_3518h		;334c	cd 18 35	. . 5
	jr nc,l3360h		;334f	30 0f		0 .
	jr l3343h		;3351	18 f0		. .
l3353h:
	ld a,(iy+018h)		;3353	fd 7e 18	. ~ .
l3356h:
	call sub_3518h		;3356	cd 18 35	. . 5
	jr nc,l3360h		;3359	30 05		0 .
	dec a			;335b	3d		=
	jr nz,l3356h		;335c	20 f8		  .
	jr l3343h		;335e	18 e3		. .
l3360h:
	ld a,b			;3360	78		x
	or a			;3361	b7		.
	jr nz,l333eh		;3362	20 da		  .
	pop de			;3364	d1		.
	pop hl			;3365	e1		.
	push de			;3366	d5		.
	push hl			;3367	e5		.
	ld bc,Rst10Vector	;3368	01 10 00	. . .
	call sub_02d6h		;336b	cd d6 02	. . .
	pop hl			;336e	e1		.
	ld b,a			;336f	47		G
	ld de,05a85h		;3370	11 85 5a	. . Z
	push de			;3373	d5		.
	ld c,00bh		;3374	0e 0b		. .
	or a			;3376	b7		.
	bit 7,(iy+013h)		;3377	fd cb 13 7e	. . . ~
	call nz,sub_02d6h	;337b	c4 d6 02	. . .
l337eh:
	jp c,l3466h		;337e	da 66 34	. f 4
	ld (ix+001h),000h	;3381	dd 36 01 00	. 6 . .
	rst 20h			;3385	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3386: message Checking.  (disk verify).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg3386' (start 0x3386 end 0x3390)
Msg3386_start:
	defb 043h		;3386	43		C
	defb 068h		;3387	68		h
	defb 065h		;3388	65		e
	defb 063h		;3389	63		c
	defb 06bh		;338a	6b		k
	defb 069h		;338b	69		i
	defb 06eh		;338c	6e		n
	defb 067h		;338d	67		g
	defb 02eh		;338e	2e		.
	defb 0a0h		;338f	a0		.
Msg3386_end:
	call PrintFrameCount	;3390	cd 7d 34	. } 4
	ld de,WriteAnyBankByte	;3393	11 00 00	. . .
	ld hl,0e3f7h		;3396	21 f7 e3	! . .
	push hl			;3399	e5		.
	ld c,00ch		;339a	0e 0c		. .
	call sub_02d6h		;339c	cd d6 02	. . .
	call c,sub_3460h	;339f	dc 60 34	. ` 4
	call LookupToken	;33a2	cd a8 34	. . 4
	pop hl			;33a5	e1		.
	exx			;33a6	d9		.
	pop hl			;33a7	e1		.
	exx			;33a8	d9		.
	ld de,(0dfd2h)		;33a9	ed 5b d2 df	. [ . .
	ld bc,(0dfd4h)		;33ad	ed 4b d4 df	. K . .
l33b1h:
	ld a,(hl)		;33b1	7e		~
	cp 0feh			;33b2	fe fe		. .
	jr z,l33cah		;33b4	28 14		( .
	inc de			;33b6	13		.
	or a			;33b7	b7		.
	ld a,032h		;33b8	3e 32		> 2
	jr nz,l33bfh		;33ba	20 03		  .
	ld a,034h		;33bc	3e 34		> 4
	inc bc			;33be	03		.
l33bfh:
	exx			;33bf	d9		.
	ld (hl),a		;33c0	77		w
	inc hl			;33c1	23		#
	exx			;33c2	d9		.
	inc hl			;33c3	23		#
	inc hl			;33c4	23		#
	inc hl			;33c5	23		#
	inc hl			;33c6	23		#
	inc hl			;33c7	23		#
	jr l33b1h		;33c8	18 e7		. .
l33cah:
	ld (0dfd2h),de		;33ca	ed 53 d2 df	. S . .
	ld (0dfd4h),bc		;33ce	ed 43 d4 df	. C . .
	ld (ix+001h),00ah	;33d2	dd 36 01 0a	. 6 . .
	call sub_3485h		;33d6	cd 85 34	. . 4
	pop de			;33d9	d1		.
	ld a,e			;33da	7b		{
	or a			;33db	b7		.
	jp nz,l32cah		;33dc	c2 ca 32	. . 2
	dec d			;33df	15		.
	ld a,d			;33e0	7a		z
	cp 0ffh			;33e1	fe ff		. .
	jp nz,l32bfh		;33e3	c2 bf 32	. . 2
	ld bc,Rst10Vector	;33e6	01 10 00	. . .
	call sub_02d6h		;33e9	cd d6 02	. . .
	ld e,a			;33ec	5f		_
	and 043h		;33ed	e6 43		. C
	cp 001h			;33ef	fe 01		. .
	jr nz,l344ch		;33f1	20 59		  Y
	bit 7,(iy+013h)		;33f3	fd cb 13 7e	. . . ~
	jr z,l344ch		;33f7	28 53		( S
	ld hl,0e3f7h		;33f9	21 f7 e3	! . .
	xor a			;33fc	af		.
	ld b,a			;33fd	47		G
l33feh:
	ld (hl),a		;33fe	77		w
	inc hl			;33ff	23		#
	djnz l33feh		;3400	10 fc		. .
	ld a,016h		;3402	3e 16		> .
	bit 7,e			;3404	cb 7b		. {
	jr nz,l340ah		;3406	20 02		  .
	ld a,018h		;3408	3e 18		> .
l340ah:
	bit 5,e			;340a	cb 6b		. k
	jr nz,l3410h		;340c	20 02		  .
	or 001h			;340e	f6 01		. .
l3410h:
	ld (0e4dah),a		;3410	32 da e4	2 . .
	ld hl,(0dfd2h)		;3413	2a d2 df	* . .
	ld de,0fff0h		;3416	11 f0 ff	. . .
	add hl,de		;3419	19		.
	ld (0e4d6h),hl		;341a	22 d6 e4	" . .
	ld (0e4dch),hl		;341d	22 dc e4	" . .
	ld hl,(0dfd4h)		;3420	2a d4 df	* . .
	ld (0e4d4h),hl		;3423	22 d4 e4	" . .
	ld a,010h		;3426	3e 10		> .
	ld (0e4deh),a		;3428	32 de e4	2 . .
	ld a,(l00fbh)		;342b	3a fb 00	: . .
	cp (iy-007h)		;342e	fd be f9	. . .
	jr nz,l3438h		;3431	20 05		  .
	ld a,001h		;3433	3e 01		> .
	ld (0e4d9h),a		;3435	32 d9 e4	2 . .
l3438h:
	ld hl,0e4ech		;3438	21 ec e4	! . .
	call sub_34c2h		;343b	cd c2 34	. . 4
	ld c,007h		;343e	0e 07		. .
	call sub_02d6h		;3440	cd d6 02	. . .
	jr c,l3466h		;3443	38 21		8 !
	ld e,008h		;3445	1e 08		. .
	call sub_3522h		;3447	cd 22 35	. " 5
	jr c,l3466h		;344a	38 1a		8 .
l344ch:
	ld hl,0e097h		;344c	21 97 e0	! . .
	rst 30h			;344f	f7		.
	call sub_0839h		;3450	cd 39 08	. 9 .
	rst 20h			;3453	e7		.
	dec c			;3454	0d		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3455: message Complete.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg3455' (start 0x3455 end 0x345d)
Msg3455_start:
	defb 043h		;3455	43		C
	defb 06fh		;3456	6f		o
	defb 06dh		;3457	6d		m
	defb 070h		;3458	70		p
	defb 06ch		;3459	6c		l
	defb 065h		;345a	65		e
	defb 074h		;345b	74		t
	defb 0e5h		;345c	e5		.
Msg3455_end:
	jp sub_2f85h		;345d	c3 85 2f	. . /
sub_3460h:
	ld a,(0ffddh)		;3460	3a dd ff	: . .
	cp 014h			;3463	fe 14		. .
	ret z			;3465	c8		.
l3466h:
	ld b,046h		;3466	06 46		. F
	call sub_3472h		;3468	cd 72 34	. r 4
	rst 8			;346b	cf		.
	call sub_2f85h		;346c	cd 85 2f	. . /
	jp l11beh		;346f	c3 be 11	. . .
sub_3472h:
	rst 20h			;3472	e7		.
	adc a,l			;3473	8d		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ReadNmiPort: read the Scorpion NMI status port #FFDD (magic
; button state; v4.01 relocates it to #FFDC).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ReadNmiPort:
	ld hl,(0ffddh)		;3474	2a dd ff	* . .
	ld h,000h		;3477	26 00		& .
	ld (0dda7h),hl		;3479	22 a7 dd	" . .
	ret			;347c	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintFrameCount: display the frame counters at DFD2/DFD4.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintFrameCount:
	ld de,(0dfd2h)		;347d	ed 5b d2 df	. [ . .
	ld bc,(0dfd4h)		;3481	ed 4b d4 df	. K . .
sub_3485h:
	push hl			;3485	e5		.
	push bc			;3486	c5		.
	push de			;3487	d5		.
	rst 20h			;3488	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgTotal: Total label of the frame-counter
; readout (PrintFrameCount).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgTotal' (start 0x3489 end 0x3490)
MsgTotal_start:
	defb 020h		;3489	20		 
	defb 054h		;348a	54		T
	defb 06fh		;348b	6f		o
	defb 074h		;348c	74		t
	defb 061h		;348d	61		a
	defb 06ch		;348e	6c		l
	defb 0a0h		;348f	a0		.
MsgTotal_end:
	pop bc			;3490	c1		.
	pop de			;3491	d1		.
	push bc			;3492	c5		.
	push de			;3493	d5		.
	call sub_1929h		;3494	cd 29 19	. ) .
	rst 20h			;3497	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgGold: / Gold - second frame-counter label.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgGold' (start 0x3498 end 0x349f)
MsgGold_start:
	defb 02fh		;3498	2f		/
	defb 020h		;3499	20		 
	defb 047h		;349a	47		G
	defb 06fh		;349b	6f		o
	defb 06ch		;349c	6c		l
	defb 064h		;349d	64		d
	defb 0a0h		;349e	a0		.
MsgGold_end:
	pop bc			;349f	c1		.
	push bc			;34a0	c5		.
	call sub_1929h		;34a1	cd 29 19	. ) .
	pop bc			;34a4	c1		.
	pop de			;34a5	d1		.
	pop hl			;34a6	e1		.
	ret			;34a7	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; LookupToken: resolve a token against the RAM table at
; $FFC8.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
LookupToken:
	ld bc,Rst10Vector	;34a8	01 10 00	. . .
	call sub_02d6h		;34ab	cd d6 02	. . .
	ld b,a			;34ae	47		G
	and 003h		;34af	e6 03		. .
	push af			;34b1	f5		.
	bit 6,b			;34b2	cb 70		. p
	jr nz,l34b8h		;34b4	20 02		  .
	add a,004h		;34b6	c6 04		. .
l34b8h:
	ld d,000h		;34b8	16 00		. .
	ld e,a			;34ba	5f		_
	pop bc			;34bb	c1		.
	ld hl,(0ffc8h)		;34bc	2a c8 ff	* . .
	add hl,de		;34bf	19		.
	ld a,(hl)		;34c0	7e		~
	ret			;34c1	c9		.
sub_34c2h:
	push hl			;34c2	e5		.
	ld b,008h		;34c3	06 08		. .
l34c5h:
	ld (hl),020h		;34c5	36 20		6  
	inc hl			;34c7	23		#
	djnz l34c5h		;34c8	10 fb		. .
	ld a,089h		;34ca	3e 89		> .
	call sub_1177h		;34cc	cd 77 11	. w .
	jr c,l34e5h		;34cf	38 14		8 .
	jr z,l34e5h		;34d1	28 12		( .
	pop de			;34d3	d1		.
	push de			;34d4	d5		.
	ld bc,Rst08Vector	;34d5	01 08 00	. . .
l34d8h:
	ld a,(hl)		;34d8	7e		~
	cp 020h			;34d9	fe 20		.  
	jr c,l34e2h		;34db	38 05		8 .
	ldi			;34dd	ed a0		. .
	jp pe,l34d8h		;34df	ea d8 34	. . 4
l34e2h:
	or a			;34e2	b7		.
	pop hl			;34e3	e1		.
	ret			;34e4	c9		.
l34e5h:
	scf			;34e5	37		7
	pop hl			;34e6	e1		.
	ret			;34e7	c9		.
sub_34e8h:
	ld hl,0e097h		;34e8	21 97 e0	! . .
	rst 30h			;34eb	f7		.
	call sub_265bh		;34ec	cd 5b 26	. [ &
	rst 20h			;34ef	e7		.
	dec de			;34f0	1b		.
	nop			;34f1	00		.
	ld a,(bc)		;34f2	0a		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg34F3: message Insert disk, press Y key.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg34F3' (start 0x34f3 end 0x350b)
Msg34F3_start:
	defb 049h		;34f3	49		I
	defb 06eh		;34f4	6e		n
	defb 073h		;34f5	73		s
	defb 065h		;34f6	65		e
	defb 072h		;34f7	72		r
	defb 074h		;34f8	74		t
	defb 020h		;34f9	20		 
	defb 064h		;34fa	64		d
	defb 069h		;34fb	69		i
	defb 073h		;34fc	73		s
	defb 06bh		;34fd	6b		k
	defb 02ch		;34fe	2c		,
	defb 020h		;34ff	20		 
	defb 070h		;3500	70		p
	defb 072h		;3501	72		r
	defb 065h		;3502	65		e
	defb 073h		;3503	73		s
	defb 073h		;3504	73		s
	defb 020h		;3505	20		 
	defb 059h		;3506	59		Y
	defb 020h		;3507	20		 
	defb 06bh		;3508	6b		k
	defb 065h		;3509	65		e
	defb 0f9h		;350a	f9		.
Msg34F3_end:
	call AutoRepeatKey	;350b	cd 16 17	. . .
	and 05fh		;350e	e6 5f		. _
	cp 059h			;3510	fe 59		. Y
	ret nz			;3512	c0		.
	rst 18h			;3513	df		.
	or (hl)			;3514	b6		.
	jr c,$-79		;3515	38 af		8 .
	ret			;3517	c9		.
sub_3518h:
	inc hl			;3518	23		#
	inc hl			;3519	23		#
	inc hl			;351a	23		#
	inc hl			;351b	23		#
	inc hl			;351c	23		#
	or a			;351d	b7		.
	sbc hl,de		;351e	ed 52		. R
	add hl,de		;3520	19		.
	ret			;3521	c9		.
sub_3522h:
	ld hl,0e3f7h		;3522	21 f7 e3	! . .
	ld d,000h		;3525	16 00		. .
	ld bc,l0105h+1		;3527	01 06 01	. . .
	jp sub_02d6h		;352a	c3 d6 02	. . .
l352dh:
	ei			;352d	fb		.
	halt			;352e	76		v
	di			;352f	f3		.
	ld de,(UserPc)		;3530	ed 5b 6b dd	. [ k .
	call sub_354ah		;3534	cd 4a 35	. J 5
	ld de,00ff3h		;3537	11 f3 0f	. . .
	call sub_354ah		;353a	cd 4a 35	. J 5
	ld hl,l3d30h		;353d	21 30 3d	! 0 =
	ld (UserPc),hl		;3540	22 6b dd	" k .
	jp l011ch		;3543	c3 1c 01	. . .
	ld hl,l2e1ch		;3546	21 1c 2e	! . .
	ex (sp),hl		;3549	e3		.
sub_354ah:
	ld hl,(UserSp)		;354a	2a 6d dd	* m .
	dec hl			;354d	2b		+
	ld a,d			;354e	7a		z
	rst 0			;354f	c7		.
	dec hl			;3550	2b		+
	ld a,e			;3551	7b		{
	rst 0			;3552	c7		.
	ld (UserSp),hl		;3553	22 6d dd	" m .
	ret			;3556	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CallTrdos - enter a TR-DOS session: #1FFD=$02, #7FFD=$17,
; LDIR 6912 bytes $C000 -> $4000 (DOS screen into place),
; then restore #7FFD=$10 / #1FFD=$12 on return.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CallTrdos:
	bit 3,(iy-002h)		;3557	fd cb fe 5e	. . . ^
	jp z,sub_04cch		;355b	ca cc 04	. . .
	ld bc,01ffdh		;355e	01 fd 1f	. . .
	ld a,002h		;3561	3e 02		> .
	out (c),a		;3563	ed 79		. y
	ld b,07fh		;3565	06 7f		. .
	ld a,017h		;3567	3e 17		> .
	out (c),a		;3569	ed 79		. y
	ld hl,0c000h		;356b	21 00 c0	! . .
	ld de,ZeroPad3F91_end	;356e	11 00 40	. . @
	ld bc,01b00h		;3571	01 00 1b	. . .
	ldir			;3574	ed b0		. .
	ld bc,07ffdh		;3576	01 fd 7f	. . .
	ld a,010h		;3579	3e 10		> .
	out (c),a		;357b	ed 79		. y
	ld b,01fh		;357d	06 1f		. .
	ld a,012h		;357f	3e 12		> .
	out (c),a		;3581	ed 79		. y
	ret			;3583	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RestorePaging: DFDB shadow -> DFDD paging mirror, ports
; refreshed.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RestorePaging:
	push hl			;3584	e5		.
	ld hl,(PagingBackup)	;3585	2a db df	* . .
	ld (PagingState),hl	;3588	22 dd df	" . .
	pop hl			;358b	e1		.
	ret			;358c	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SaveToTape: header + data bytes to tape through
; FeedTapeOutput; leader and parity via the $2F3E+ waits.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SaveToTape:
	call CheckAddressRange	;358d	cd d0 2e	. . .
	set 4,(iy+009h)		;3590	fd cb 09 e6	. . . .
	jr l3599h		;3594	18 03		. .
	call CheckAddressRange	;3596	cd d0 2e	. . .
l3599h:
	ld bc,sub_16c7h		;3599	01 c7 16	. . .
	jr l35adh		;359c	18 0f		. .
	call CheckAddressRange	;359e	cd d0 2e	. . .
	set 4,(iy+009h)		;35a1	fd cb 09 e6	. . . .
	jr l35aah		;35a5	18 03		. .
	call CheckAddressRange	;35a7	cd d0 2e	. . .
l35aah:
	ld bc,l1608h		;35aa	01 08 16	. . .
l35adh:
	ex de,hl		;35ad	eb		.
	call InitErrorWindow	;35ae	cd cb 35	. . 5
l35b1h:
	call sub_2f3eh		;35b1	cd 3e 2f	. > /
	rst 20h			;35b4	e7		.
	adc a,l			;35b5	8d		.
	push de			;35b6	d5		.
	push bc			;35b7	c5		.
	call sub_35c9h		;35b8	cd c9 35	. . 5
	pop bc			;35bb	c1		.
	pop de			;35bc	d1		.
	or a			;35bd	b7		.
	sbc hl,de		;35be	ed 52		. R
	add hl,de		;35c0	19		.
	jr c,l35b1h		;35c1	38 ee		8 .
	call sub_0839h		;35c3	cd 39 08	. 9 .
	ld a,081h		;35c6	3e 81		> .
	ret			;35c8	c9		.
sub_35c9h:
	push bc			;35c9	c5		.
	ret			;35ca	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; InitErrorWindow: open the service window stack and install
; the error hook below so RST 08 lands in a visible window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
InitErrorWindow:
	push hl			;35cb	e5		.
	push de			;35cc	d5		.
	push bc			;35cd	c5		.
	bit 4,(iy+009h)		;35ce	fd cb 09 66	. . . f
	ld hl,PromptWindowDef	;35d2	21 35 e0	! 5 .
	jr z,l35dah		;35d5	28 03		( .
	ld hl,0e019h		;35d7	21 19 e0	! . .
l35dah:
	rst 30h			;35da	f7		.
	push af			;35db	f5		.
	call z,sub_265bh	;35dc	cc 5b 26	. [ &
l35dfh:
	ld (iy+00ah),000h	;35df	fd 36 0a 00	. 6 . .
	pop af			;35e3	f1		.
	jr nz,l35eah		;35e4	20 04		  .
	ld (ix+000h),015h	;35e6	dd 36 00 15	. 6 . .
l35eah:
	ld hl,ErrorWindowHook	;35ea	21 f4 35	! . 5
	ld (0de12h),hl		;35ed	22 12 de	" . .
	pop bc			;35f0	c1		.
	pop de			;35f1	d1		.
	pop hl			;35f2	e1		.
	ret			;35f3	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ErrorWindowHook: the (DE12)-vectored handler printing
; errors into the popup window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ErrorWindowHook:
	call sub_0839h		;35f4	cd 39 08	. 9 .
	ld a,09eh		;35f7	3e 9e		> .
	jp ExitToError		;35f9	c3 54 0a	. T .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MenuBuilder: popup-menu code of the ROM tail. It
; calls OpenPopup ($2B5F), then prints the five
; entries of KeywordTable ($3746) with helper
; strings indexed at ErrorShortStrings+3*i+2
; ($3CEC). The option loop ends in the interrupt-
; mode dispatcher: a 3-bytes-per-entry jump table
; at $3671 (im 0 / im 1 / im 2 / invalid) entered
; through jp (hl) at $3670. The final routine
; ($367D) copies ten (dest,word) pairs from
; RamInitTable ($3F69) into the workspace.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MenuBuilder' (start 0x35fc end 0x3690)
MenuBuilder_start:
	call PopupTabStops_end	;35fc	cd 5f 2b	. _ +
	ld hl,EncodedTable3690_end	;35ff	21 46 37	! F 7
	ld c,000h		;3602	0e 00		. .
l3604h:
	ld b,005h		;3604	06 05		. .
l3606h:
	ld a,(hl)		;3606	7e		~
	or a			;3607	b7		.
	ld a,081h		;3608	3e 81		> .
	ret z			;360a	c8		.
	push bc			;360b	c5		.
	rst 20h			;360c	e7		.
	adc a,l			;360d	8d		.
	ld b,00ah		;360e	06 0a		. .
	call sub_189eh		;3610	cd 9e 18	. . .
	pop bc			;3613	c1		.
	push bc			;3614	c5		.
	push hl			;3615	e5		.
	ld hl,l3cech		;3616	21 ec 3c	! . <
	ld a,c			;3619	79		y
	add a,a			;361a	87		.
	add a,c			;361b	81		.
	ld c,a			;361c	4f		O
	inc c			;361d	0c		.
	inc c			;361e	0c		.
	ld b,000h		;361f	06 00		. .
	add hl,bc		;3621	09		.
	ld a,(hl)		;3622	7e		~
	bit 7,a			;3623	cb 7f		. .
	jr z,l3629h		;3625	28 02		( .
	jr l362dh		;3627	18 04		. .
l3629h:
	bit 5,a			;3629	cb 6f		. o
	jr z,l3637h		;362b	28 0a		( .
l362dh:
	rst 20h			;362d	e7		.
	ld h,e			;362e	63		c
	ld l,a			;362f	6f		o
	ld l,l			;3630	6d		m
	ld (hl),b		;3631	70		p
	ld l,h			;3632	6c		l
	ld h,l			;3633	65		e
	ret m			;3634	f8		.
	jr l364ah		;3635	18 13		. .
l3637h:
	bit 6,a			;3637	cb 77		. w
	push af			;3639	f5		.
	jr nz,l3641h		;363a	20 05		  .
	rst 20h			;363c	e7		.
	jr nz,l35dfh		;363d	20 a0		  .
	jr l3644h		;363f	18 03		. .
l3641h:
	rst 20h			;3641	e7		.
	inc a			;3642	3c		<
	cp l			;3643	bd		.
l3644h:
	pop af			;3644	f1		.
	and 01fh		;3645	e6 1f		. .
	call sub_328ah		;3647	cd 8a 32	. . 2
l364ah:
	pop hl			;364a	e1		.
	pop bc			;364b	c1		.
	inc c			;364c	0c		.
	djnz l3606h		;364d	10 b7		. .
	push hl			;364f	e5		.
	push bc			;3650	c5		.
	call AutoRepeatKey	;3651	cd 16 17	. . .
	pop bc			;3654	c1		.
	pop hl			;3655	e1		.
	cp 007h			;3656	fe 07		. .
	jr nz,l3604h		;3658	20 aa		  .
	ld a,09ah		;365a	3e 9a		> .
	ret			;365c	c9		.
	ld a,e			;365d	7b		{
	and 0fch		;365e	e6 fc		. .
	or d			;3660	b2		.
	ld a,017h		;3661	3e 17		> .
	ret nz			;3663	c0		.
	ld a,081h		;3664	3e 81		> .
	ld hl,WriteAnyBankByte	;3666	21 00 00	! . .
	add hl,de		;3669	19		.
	add hl,de		;366a	19		.
	add hl,de		;366b	19		.
	ld de,l3671h		;366c	11 71 36	. q 6
	add hl,de		;366f	19		.
	jp (hl)			;3670	e9		.
l3671h:
	im 0			;3671	ed 46		. F
	ret			;3673	c9		.
	im 1			;3674	ed 56		. V
	ret			;3676	c9		.
	im 2			;3677	ed 5e		. ^
	ret			;3679	c9		.
	ld a,017h		;367a	3e 17		> .
	ret			;367c	c9		.
	ld hl,EncodedTable3E45_end	;367d	21 69 3f	! i ?
	ld b,00ah		;3680	06 0a		. .
l3682h:
	ld e,(hl)		;3682	5e		^
	inc hl			;3683	23		#
	ld d,(hl)		;3684	56		V
	inc hl			;3685	23		#
	ld a,(hl)		;3686	7e		~
	ld (de),a		;3687	12		.
	inc hl			;3688	23		#
	inc de			;3689	13		.
	ld a,(hl)		;368a	7e		~
	ld (de),a		;368b	12		.
	inc hl			;368c	23		#
	djnz l3682h		;368d	10 f3		. .
	ret			;368f	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EncodedTable3690: XOR-masked ROM table, unmasked
; at boot into $E2DB by the second DecodeXorTable
; call ($05BB); keys at XorDecodeKeys ($00FC).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MenuBuilder_end:

; BLOCK 'EncodedTable3690' (start 0x3690 end 0x3746)
EncodedTable3690_start:
	defb 0a6h		;3690	a6		.
	defb 038h		;3691	38		8
	defb 0cfh		;3692	cf		.
	defb 035h		;3693	35		5
	defb 0a0h		;3694	a0		.
	defb 0dch		;3695	dc		.
	defb 0c4h		;3696	c4		.
	defb 0beh		;3697	be		.
	defb 023h		;3698	23		#
	defb 0c5h		;3699	c5		.
	defb 031h		;369a	31		1
	defb 0d0h		;369b	d0		.
	defb 02ch		;369c	2c		,
	defb 0bbh		;369d	bb		.
	defb 0cbh		;369e	cb		.
	defb 0f4h		;369f	f4		.
	defb 0cch		;36a0	cc		.
	defb 0b0h		;36a1	b0		.
	defb 0b8h		;36a2	b8		.
	defb 0c0h		;36a3	c0		.
	defb 0f7h		;36a4	f7		.
	defb 0d8h		;36a5	d8		.
	defb 026h		;36a6	26		&
	defb 08dh		;36a7	8d		.
	defb 0f3h		;36a8	f3		.
	defb 089h		;36a9	89		.
	defb 0c9h		;36aa	c9		.
	defb 0e0h		;36ab	e0		.
	defb 01ch		;36ac	1c		.
	defb 08bh		;36ad	8b		.
	defb 030h		;36ae	30		0
	defb 010h		;36af	10		.
	defb 0f9h		;36b0	f9		.
	defb 085h		;36b1	85		.
	defb 019h		;36b2	19		.
	defb 0e8h		;36b3	e8		.
	defb 014h		;36b4	14		.
	defb 083h		;36b5	83		.
	defb 0fdh		;36b6	fd		.
	defb 0fbh		;36b7	fb		.
	defb 09fh		;36b8	9f		.
	defb 000h		;36b9	00		.
	defb 0e4h		;36ba	e4		.
	defb 016h		;36bb	16		.
	defb 0f1h		;36bc	f1		.
	defb 00fh		;36bd	0f		.
	defb 09ah		;36be	9a		.
	defb 035h		;36bf	35		5
	defb 0e5h		;36c0	e5		.
	defb 0deh		;36c1	de		.
	defb 0eeh		;36c2	ee		.
	defb 003h		;36c3	03		.
	defb 004h		;36c4	04		.
	defb 05ah		;36c5	5a		Z
	defb 0dbh		;36c6	db		.
	defb 015h		;36c7	15		.
	defb 0feh		;36c8	fe		.
	defb 01eh		;36c9	1e		.
	defb 0ceh		;36ca	ce		.
	defb 02eh		;36cb	2e		.
	defb 003h		;36cc	03		.
	defb 0ffh		;36cd	ff		.
	defb 06ah		;36ce	6a		j
	defb 01ah		;36cf	1a		.
	defb 062h		;36d0	62		b
	defb 020h		;36d1	20		 
	defb 00fh		;36d2	0f		.
	defb 0f5h		;36d3	f5		.
	defb 060h		;36d4	60		`
	defb 0c3h		;36d5	c3		.
	defb 0d2h		;36d6	d2		.
	defb 0c1h		;36d7	c1		.
	defb 004h		;36d8	04		.
	defb 0fbh		;36d9	fb		.
	defb 078h		;36da	78		x
	defb 03eh		;36db	3e		>
	defb 011h		;36dc	11		.
	defb 0efh		;36dd	ef		.
	defb 07ah		;36de	7a		z
	defb 00ah		;36df	0a		.
	defb 012h		;36e0	12		.
	defb 030h		;36e1	30		0
	defb 01fh		;36e2	1f		.
	defb 0e5h		;36e3	e5		.
	defb 070h		;36e4	70		p
	defb 0cbh		;36e5	cb		.
	defb 0c6h		;36e6	c6		.
	defb 0bah		;36e7	ba		.
	defb 037h		;36e8	37		7
	defb 016h		;36e9	16		.
	defb 0cch		;36ea	cc		.
	defb 0fdh		;36eb	fd		.
	defb 02fh		;36ec	2f		/
	defb 031h		;36ed	31		1
	defb 003h		;36ee	03		.
	defb 02dh		;36ef	2d		-
	defb 033h		;36f0	33		3
	defb 03eh		;36f1	3e		>
	defb 029h		;36f2	29		)
	defb 030h		;36f3	30		0
	defb 066h		;36f4	66		f
	defb 0c7h		;36f5	c7		.
	defb 0deh		;36f6	de		.
	defb 0d9h		;36f7	d9		.
	defb 004h		;36f8	04		.
	defb 01ch		;36f9	1c		.
	defb 07bh		;36fa	7b		{
	defb 0edh		;36fb	ed		.
	defb 015h		;36fc	15		.
	defb 021h		;36fd	21		!
	defb 0deh		;36fe	de		.
	defb 0cdh		;36ff	cd		.
	defb 01ah		;3700	1a		.
	defb 0ebh		;3701	eb		.
	defb 0dah		;3702	da		.
	defb 029h		;3703	29		)
	defb 0d4h		;3704	d4		.
	defb 035h		;3705	35		5
	defb 015h		;3706	15		.
	defb 046h		;3707	46		F
	defb 0b8h		;3708	b8		.
	defb 02fh		;3709	2f		/
	defb 0a6h		;370a	a6		.
	defb 091h		;370b	91		.
	defb 098h		;370c	98		.
	defb 0a7h		;370d	a7		.
	defb 096h		;370e	96		.
	defb 05dh		;370f	5d		]
	defb 0a0h		;3710	a0		.
	defb 021h		;3711	21		!
	defb 061h		;3712	61		a
	defb 048h		;3713	48		H
	defb 0b4h		;3714	b4		.
	defb 023h		;3715	23		#
	defb 05dh		;3716	5d		]
	defb 05bh		;3717	5b		[
	defb 07bh		;3718	7b		{
	defb 056h		;3719	56		V
	defb 0aah		;371a	aa		.
	defb 039h		;371b	39		9
	defb 080h		;371c	80		.
	defb 0b3h		;371d	b3		.
	defb 0a0h		;371e	a0		.
	defb 06eh		;371f	6e		n
	defb 03eh		;3720	3e		>
	defb 0adh		;3721	ad		.
	defb 06eh		;3722	6e		n
	defb 004h		;3723	04		.
	defb 0aah		;3724	aa		.
	defb 0a9h		;3725	a9		.
	defb 0aeh		;3726	ae		.
	defb 055h		;3727	55		U
	defb 074h		;3728	74		t
	defb 044h		;3729	44		D
	defb 094h		;372a	94		.
	defb 0b3h		;372b	b3		.
	defb 041h		;372c	41		A
	defb 04fh		;372d	4f		O
	defb 073h		;372e	73		s
	defb 04fh		;372f	4f		O
	defb 04eh		;3730	4e		N
	defb 085h		;3731	85		.
	defb 043h		;3732	43		C
	defb 04ah		;3733	4a		J
	defb 0a1h		;3734	a1		.
	defb 0a5h		;3735	a5		.
	defb 045h		;3736	45		E
	defb 0adh		;3737	ad		.
	defb 057h		;3738	57		W
	defb 01dh		;3739	1d		.
	defb 084h		;373a	84		.
	defb 05ah		;373b	5a		Z
	defb 01dh		;373c	1d		.
	defb 081h		;373d	81		.
	defb 051h		;373e	51		Q
	defb 016h		;373f	16		.
	defb 08eh		;3740	8e		.
	defb 083h		;3741	83		.
	defb 039h		;3742	39		9
	defb 0abh		;3743	ab		.
	defb 078h		;3744	78		x
	defb 068h		;3745	68		h
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeywordTable: menu/command keyword strings
; ('PC','BAS','BREa','BR','CAL','CATalog',...); the
; final character of each entry carries bit 7,
; and the menu loop at $3606 stops at a $00 byte.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EncodedTable3690_end:

; BLOCK 'KeywordTable' (start 0x3746 end 0x3818)
KeywordTable_start:
	defb 02eh		;3746	2e		.
	defb 050h		;3747	50		P
	defb 0c3h		;3748	c3		.
	defb 0aeh		;3749	ae		.
	defb 042h		;374a	42		B
	defb 041h		;374b	41		A
	defb 053h		;374c	53		S
	defb 0c5h		;374d	c5		.
	defb 042h		;374e	42		B
	defb 052h		;374f	52		R
	defb 045h		;3750	45		E
	defb 061h		;3751	61		a
	defb 0ebh		;3752	eb		.
	defb 042h		;3753	42		B
	defb 052h		;3754	52		R
	defb 0cbh		;3755	cb		.
	defb 043h		;3756	43		C
	defb 041h		;3757	41		A
	defb 04ch		;3758	4c		L
	defb 0ech		;3759	ec		.
	defb 043h		;375a	43		C
	defb 041h		;375b	41		A
	defb 054h		;375c	54		T
	defb 061h		;375d	61		a
	defb 06ch		;375e	6c		l
	defb 06fh		;375f	6f		o
	defb 067h		;3760	67		g
	defb 075h		;3761	75		u
	defb 0e5h		;3762	e5		.
	defb 043h		;3763	43		C
	defb 048h		;3764	48		H
	defb 065h		;3765	65		e
	defb 063h		;3766	63		c
	defb 0ebh		;3767	eb		.
	defb 044h		;3768	44		D
	defb 041h		;3769	41		A
	defb 053h		;376a	53		S
	defb 0edh		;376b	ed		.
	defb 044h		;376c	44		D
	defb 041h		;376d	41		A
	defb 054h		;376e	54		T
	defb 0c1h		;376f	c1		.
	defb 044h		;3770	44		D
	defb 045h		;3771	45		E
	defb 04ch		;3772	4c		L
	defb 042h		;3773	42		B
	defb 072h		;3774	72		r
	defb 065h		;3775	65		e
	defb 061h		;3776	61		a
	defb 0ebh		;3777	eb		.
	defb 044h		;3778	44		D
	defb 049h		;3779	49		I
	defb 053h		;377a	53		S
	defb 0f3h		;377b	f3		.
	defb 044h		;377c	44		D
	defb 055h		;377d	55		U
	defb 06dh		;377e	6d		m
	defb 0f0h		;377f	f0		.
	defb 045h		;3780	45		E
	defb 052h		;3781	52		R
	defb 041h		;3782	41		A
	defb 073h		;3783	73		s
	defb 0e5h		;3784	e5		.
	defb 045h		;3785	45		E
	defb 058h		;3786	58		X
	defb 0d8h		;3787	d8		.
	defb 045h		;3788	45		E
	defb 058h		;3789	58		X
	defb 020h		;378a	20		 
	defb 041h		;378b	41		A
	defb 0c6h		;378c	c6		.
	defb 045h		;378d	45		E
	defb 058h		;378e	58		X
	defb 069h		;378f	69		i
	defb 0f4h		;3790	f4		.
	defb 046h		;3791	46		F
	defb 049h		;3792	49		I
	defb 04ch		;3793	4c		L
	defb 0ech		;3794	ec		.
	defb 046h		;3795	46		F
	defb 049h		;3796	49		I
	defb 04eh		;3797	4e		N
	defb 0e4h		;3798	e4		.
	defb 049h		;3799	49		I
	defb 04eh		;379a	4e		N
	defb 054h		;379b	54		T
	defb 065h		;379c	65		e
	defb 072h		;379d	72		r
	defb 072h		;379e	72		r
	defb 075h		;379f	75		u
	defb 070h		;37a0	70		p
	defb 0f4h		;37a1	f4		.
	defb 049h		;37a2	49		I
	defb 0ceh		;37a3	ce		.
	defb 04ah		;37a4	4a		J
	defb 075h		;37a5	75		u
	defb 06dh		;37a6	6d		m
	defb 0f0h		;37a7	f0		.
	defb 04ch		;37a8	4c		L
	defb 042h		;37a9	42		B
	defb 072h		;37aa	72		r
	defb 065h		;37ab	65		e
	defb 061h		;37ac	61		a
	defb 0ebh		;37ad	eb		.
	defb 04ch		;37ae	4c		L
	defb 049h		;37af	49		I
	defb 053h		;37b0	53		S
	defb 054h		;37b1	54		T
	defb 042h		;37b2	42		B
	defb 072h		;37b3	72		r
	defb 065h		;37b4	65		e
	defb 061h		;37b5	61		a
	defb 0ebh		;37b6	eb		.
	defb 04ch		;37b7	4c		L
	defb 043h		;37b8	43		C
	defb 041h		;37b9	41		A
	defb 054h		;37ba	54		T
	defb 061h		;37bb	61		a
	defb 06ch		;37bc	6c		l
	defb 06fh		;37bd	6f		o
	defb 067h		;37be	67		g
	defb 075h		;37bf	75		u
	defb 0e5h		;37c0	e5		.
	defb 04ch		;37c1	4c		L
	defb 044h		;37c2	44		D
	defb 049h		;37c3	49		I
	defb 053h		;37c4	53		S
	defb 0f3h		;37c5	f3		.
	defb 04ch		;37c6	4c		L
	defb 044h		;37c7	44		D
	defb 055h		;37c8	55		U
	defb 06dh		;37c9	6d		m
	defb 0f0h		;37ca	f0		.
	defb 04ch		;37cb	4c		L
	defb 04fh		;37cc	4f		O
	defb 041h		;37cd	41		A
	defb 0c4h		;37ce	c4		.
	defb 04dh		;37cf	4d		M
	defb 045h		;37d0	45		E
	defb 04dh		;37d1	4d		M
	defb 06fh		;37d2	6f		o
	defb 072h		;37d3	72		r
	defb 0f9h		;37d4	f9		.
	defb 04dh		;37d5	4d		M
	defb 04fh		;37d6	4f		O
	defb 056h		;37d7	56		V
	defb 0e5h		;37d8	e5		.
	defb 04fh		;37d9	4f		O
	defb 050h		;37da	50		P
	defb 054h		;37db	54		T
	defb 069h		;37dc	69		i
	defb 06fh		;37dd	6f		o
	defb 0eeh		;37de	ee		.
	defb 04fh		;37df	4f		O
	defb 055h		;37e0	55		U
	defb 0d4h		;37e1	d4		.
	defb 050h		;37e2	50		P
	defb 055h		;37e3	55		U
	defb 053h		;37e4	53		S
	defb 0c8h		;37e5	c8		.
	defb 050h		;37e6	50		P
	defb 04fh		;37e7	4f		O
	defb 0d0h		;37e8	d0		.
	defb 052h		;37e9	52		R
	defb 041h		;37ea	41		A
	defb 0cdh		;37eb	cd		.
	defb 052h		;37ec	52		R
	defb 04fh		;37ed	4f		O
	defb 0cdh		;37ee	cd		.
	defb 053h		;37ef	53		S
	defb 041h		;37f0	41		A
	defb 056h		;37f1	56		V
	defb 0c5h		;37f2	c5		.
	defb 053h		;37f3	53		S
	defb 045h		;37f4	45		E
	defb 061h		;37f5	61		a
	defb 072h		;37f6	72		r
	defb 063h		;37f7	63		c
	defb 0e8h		;37f8	e8		.
	defb 053h		;37f9	53		S
	defb 043h		;37fa	43		C
	defb 052h		;37fb	52		R
	defb 065h		;37fc	65		e
	defb 065h		;37fd	65		e
	defb 0eeh		;37fe	ee		.
	defb 053h		;37ff	53		S
	defb 048h		;3800	48		H
	defb 06fh		;3801	6f		o
	defb 0f7h		;3802	f7		.
	defb 053h		;3803	53		S
	defb 04ch		;3804	4c		L
	defb 04fh		;3805	4f		O
	defb 0d7h		;3806	d7		.
	defb 057h		;3807	57		W
	defb 04fh		;3808	4f		O
	defb 052h		;3809	52		R
	defb 04bh		;380a	4b		K
	defb 073h		;380b	73		s
	defb 070h		;380c	70		p
	defb 061h		;380d	61		a
	defb 063h		;380e	63		c
	defb 0e5h		;380f	e5		.
	defb 000h		;3810	00		.
sub_3811h:
	defb 006h		;3811	06		.
	defb 00eh		;3812	0e		.
	defb 0cdh		;3813	cd		.
	defb 00eh		;3814	0e		.
	defb 003h		;3815	03		.
	defb 007h		;3816	07		.
	defb 0c9h		;3817	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ErrorBitMap: error-class bit map (the
; ErrorTextTable2 pointer installed by
; SetErrorTables, $116A); one flag per report
; number.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
KeywordTable_end:

; BLOCK 'ErrorBitMap' (start 0x3818 end 0x3820)
ErrorBitMap_start:
	defb 06dh		;3818	6d		m
	defb 061h		;3819	61		a
	defb 069h		;381a	69		i
	defb 0eeh		;381b	ee		.
	defb 06dh		;381c	6d		m
	defb 065h		;381d	65		e
	defb 06eh		;381e	6e		n
	defb 0f5h		;381f	f5		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WordLists: packed word lists for the token
; recogniser - menu words ('main','menu',
; 'previous',...), interface words ('speed',
; 'RS232','9600',...) and the long error words
; ('abandoned','breakpoint',...).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ErrorBitMap_end:

; BLOCK 'WordLists' (start 0x3820 end 0x3b7f)
WordLists_start:
	defb 070h		;3820	70		p
	defb 072h		;3821	72		r
	defb 065h		;3822	65		e
	defb 076h		;3823	76		v
	defb 069h		;3824	69		i
	defb 06fh		;3825	6f		o
	defb 075h		;3826	75		u
	defb 0f3h		;3827	f3		.
	defb 063h		;3828	63		c
	defb 06fh		;3829	6f		o
	defb 06eh		;382a	6e		n
	defb 074h		;382b	74		t
	defb 069h		;382c	69		i
	defb 06eh		;382d	6e		n
	defb 075h		;382e	75		u
	defb 0e5h		;382f	e5		.
	defb 070h		;3830	70		p
	defb 072h		;3831	72		r
	defb 06fh		;3832	6f		o
	defb 067h		;3833	67		g
	defb 072h		;3834	72		r
	defb 061h		;3835	61		a
	defb 0edh		;3836	ed		.
	defb 06dh		;3837	6d		m
	defb 061h		;3838	61		a
	defb 067h		;3839	67		g
	defb 069h		;383a	69		i
	defb 0e3h		;383b	e3		.
	defb 062h		;383c	62		b
	defb 075h		;383d	75		u
	defb 074h		;383e	74		t
	defb 074h		;383f	74		t
	defb 06fh		;3840	6f		o
	defb 0eeh		;3841	ee		.
	defb 06dh		;3842	6d		m
	defb 06fh		;3843	6f		o
	defb 06eh		;3844	6e		n
	defb 069h		;3845	69		i
	defb 074h		;3846	74		t
	defb 06fh		;3847	6f		o
	defb 0f2h		;3848	f2		.
	defb 070h		;3849	70		p
	defb 072h		;384a	72		r
	defb 069h		;384b	69		i
	defb 06eh		;384c	6e		n
	defb 0f4h		;384d	f4		.
	defb 073h		;384e	73		s
	defb 063h		;384f	63		c
	defb 072h		;3850	72		r
	defb 065h		;3851	65		e
	defb 065h		;3852	65		e
	defb 0eeh		;3853	ee		.
	defb 074h		;3854	74		t
	defb 065h		;3855	65		e
	defb 073h		;3856	73		s
	defb 0f4h		;3857	f4		.
	defb 070h		;3858	70		p
	defb 072h		;3859	72		r
	defb 069h		;385a	69		i
	defb 06eh		;385b	6e		n
	defb 074h		;385c	74		t
	defb 065h		;385d	65		e
	defb 0f2h		;385e	f2		.
	defb 064h		;385f	64		d
	defb 069h		;3860	69		i
	defb 073h		;3861	73		s
	defb 0ebh		;3862	eb		.
	defb 053h		;3863	53		S
	defb 065h		;3864	65		e
	defb 074h		;3865	74		t
	defb 020h		;3866	20		 
	defb 055h		;3867	55		U
	defb 0f0h		;3868	f0		.
	defb 0e1h		;3869	e1		.
	defb 070h		;386a	70		p
	defb 06fh		;386b	6f		o
	defb 073h		;386c	73		s
	defb 069h		;386d	69		i
	defb 074h		;386e	74		t
	defb 069h		;386f	69		i
	defb 076h		;3870	76		v
	defb 0e5h		;3871	e5		.
	defb 06eh		;3872	6e		n
	defb 065h		;3873	65		e
	defb 067h		;3874	67		g
	defb 061h		;3875	61		a
	defb 074h		;3876	74		t
	defb 069h		;3877	69		i
	defb 076h		;3878	76		v
	defb 0e5h		;3879	e5		.
	defb 052h		;387a	52		R
	defb 041h		;387b	41		A
	defb 0cdh		;387c	cd		.
	defb 053h		;387d	53		S
	defb 068h		;387e	68		h
	defb 061h		;387f	61		a
	defb 064h		;3880	64		d
	defb 06fh		;3881	6f		o
	defb 0f7h		;3882	f7		.
	defb 073h		;3883	73		s
	defb 065h		;3884	65		e
	defb 072h		;3885	72		r
	defb 076h		;3886	76		v
	defb 069h		;3887	69		i
	defb 063h		;3888	63		c
	defb 0e5h		;3889	e5		.
	defb 01bh		;388a	1b		.
	defb 001h		;388b	01		.
	defb 088h		;388c	88		.
	defb 01bh		;388d	1b		.
	defb 003h		;388e	03		.
	defb 08dh		;388f	8d		.
	defb 0ebh		;3890	eb		.
	defb 063h		;3891	63		c
	defb 06fh		;3892	6f		o
	defb 06eh		;3893	6e		n
	defb 073h		;3894	73		s
	defb 074h		;3895	74		t
	defb 061h		;3896	61		a
	defb 06eh		;3897	6e		n
	defb 0f4h		;3898	f4		.
	defb 0e4h		;3899	e4		.
	defb 0e4h		;389a	e4		.
	defb 0f2h		;389b	f2		.
	defb 0e2h		;389c	e2		.
	defb 069h		;389d	69		i
	defb 06eh		;389e	6e		n
	defb 074h		;389f	74		t
	defb 065h		;38a0	65		e
	defb 072h		;38a1	72		r
	defb 066h		;38a2	66		f
	defb 061h		;38a3	61		a
	defb 063h		;38a4	63		c
	defb 0e5h		;38a5	e5		.
	defb 073h		;38a6	73		s
	defb 070h		;38a7	70		p
	defb 065h		;38a8	65		e
	defb 065h		;38a9	65		e
	defb 0e4h		;38aa	e4		.
	defb 064h		;38ab	64		d
	defb 061h		;38ac	61		a
	defb 074h		;38ad	74		t
	defb 0e1h		;38ae	e1		.
	defb 062h		;38af	62		b
	defb 069h		;38b0	69		i
	defb 074h		;38b1	74		t
	defb 0f3h		;38b2	f3		.
	defb 073h		;38b3	73		s
	defb 074h		;38b4	74		t
	defb 06fh		;38b5	6f		o
	defb 0f0h		;38b6	f0		.
	defb 073h		;38b7	73		s
	defb 074h		;38b8	74		t
	defb 072h		;38b9	72		r
	defb 069h		;38ba	69		i
	defb 06eh		;38bb	6e		n
	defb 0e7h		;38bc	e7		.
	defb 06ch		;38bd	6c		l
	defb 069h		;38be	69		i
	defb 06dh		;38bf	6d		m
	defb 069h		;38c0	69		i
	defb 074h		;38c1	74		t
	defb 065h		;38c2	65		e
	defb 0f2h		;38c3	f2		.
	defb 02bh		;38c4	2b		+
	defb 04ch		;38c5	4c		L
	defb 0c6h		;38c6	c6		.
	defb 06ch		;38c7	6c		l
	defb 065h		;38c8	65		e
	defb 06eh		;38c9	6e		n
	defb 067h		;38ca	67		g
	defb 074h		;38cb	74		t
	defb 0e8h		;38cc	e8		.
	defb 070h		;38cd	70		p
	defb 061h		;38ce	61		a
	defb 067h		;38cf	67		g
	defb 0e5h		;38d0	e5		.
	defb 077h		;38d1	77		w
	defb 069h		;38d2	69		i
	defb 064h		;38d3	64		d
	defb 074h		;38d4	74		t
	defb 0e8h		;38d5	e8		.
	defb 063h		;38d6	63		c
	defb 065h		;38d7	65		e
	defb 06eh		;38d8	6e		n
	defb 074h		;38d9	74		t
	defb 072h		;38da	72		r
	defb 06fh		;38db	6f		o
	defb 06eh		;38dc	6e		n
	defb 069h		;38dd	69		i
	defb 063h		;38de	63		c
	defb 0f3h		;38df	f3		.
	defb 052h		;38e0	52		R
	defb 053h		;38e1	53		S
	defb 032h		;38e2	32		2
	defb 033h		;38e3	33		3
	defb 0b2h		;38e4	b2		.
	defb 039h		;38e5	39		9
	defb 036h		;38e6	36		6
	defb 030h		;38e7	30		0
	defb 0b0h		;38e8	b0		.
	defb 031h		;38e9	31		1
	defb 032h		;38ea	32		2
	defb 030h		;38eb	30		0
	defb 0b0h		;38ec	b0		.
	defb 0b8h		;38ed	b8		.
	defb 0b7h		;38ee	b7		.
	defb 0b1h		;38ef	b1		.
	defb 0b2h		;38f0	b2		.
	defb 04fh		;38f1	4f		O
	defb 046h		;38f2	46		F
	defb 0c6h		;38f3	c6		.
	defb 04fh		;38f4	4f		O
	defb 0ceh		;38f5	ce		.
	defb 072h		;38f6	72		r
	defb 065h		;38f7	65		e
	defb 073h		;38f8	73		s
	defb 065h		;38f9	65		e
	defb 0f4h		;38fa	f4		.
	defb 06ch		;38fb	6c		l
	defb 069h		;38fc	69		i
	defb 06eh		;38fd	6e		n
	defb 0e5h		;38fe	e5		.
	defb 066h		;38ff	66		f
	defb 065h		;3900	65		e
	defb 065h		;3901	65		e
	defb 0e4h		;3902	e4		.
	defb 063h		;3903	63		c
	defb 061h		;3904	61		a
	defb 072h		;3905	72		r
	defb 072h		;3906	72		r
	defb 069h		;3907	69		i
	defb 061h		;3908	61		a
	defb 067h		;3909	67		g
	defb 0e5h		;390a	e5		.
	defb 072h		;390b	72		r
	defb 065h		;390c	65		e
	defb 074h		;390d	74		t
	defb 075h		;390e	75		u
	defb 072h		;390f	72		r
	defb 0eeh		;3910	ee		.
	defb 063h		;3911	63		c
	defb 06fh		;3912	6f		o
	defb 06dh		;3913	6d		m
	defb 070h		;3914	70		p
	defb 075h		;3915	75		u
	defb 074h		;3916	74		t
	defb 065h		;3917	65		e
	defb 0f2h		;3918	f2		.
	defb 066h		;3919	66		f
	defb 06fh		;391a	6f		o
	defb 072h		;391b	72		r
	defb 0edh		;391c	ed		.
	defb 073h		;391d	73		s
	defb 06fh		;391e	6f		o
	defb 075h		;391f	75		u
	defb 06eh		;3920	6e		n
	defb 0e4h		;3921	e4		.
	defb 066h		;3922	66		f
	defb 06fh		;3923	6f		o
	defb 072h		;3924	72		r
	defb 06dh		;3925	6d		m
	defb 061h		;3926	61		a
	defb 0f4h		;3927	f4		.
	defb 064h		;3928	64		d
	defb 072h		;3929	72		r
	defb 069h		;392a	69		i
	defb 076h		;392b	76		v
	defb 0e5h		;392c	e5		.
	defb 074h		;392d	74		t
	defb 072h		;392e	72		r
	defb 061h		;392f	61		a
	defb 063h		;3930	63		c
	defb 0ebh		;3931	eb		.
	defb 073h		;3932	73		s
	defb 069h		;3933	69		i
	defb 064h		;3934	64		d
	defb 0e5h		;3935	e5		.
	defb 073h		;3936	73		s
	defb 065h		;3937	65		e
	defb 063h		;3938	63		c
	defb 074h		;3939	74		t
	defb 06fh		;393a	6f		o
	defb 0f2h		;393b	f2		.
	defb 0c1h		;393c	c1		.
	defb 0c2h		;393d	c2		.
	defb 0c3h		;393e	c3		.
	defb 0c4h		;393f	c4		.
	defb 034h		;3940	34		4
	defb 0b0h		;3941	b0		.
	defb 038h		;3942	38		8
	defb 0b0h		;3943	b0		.
	defb 046h		;3944	46		F
	defb 0cdh		;3945	cd		.
	defb 04dh		;3946	4d		M
	defb 046h		;3947	46		F
	defb 0cdh		;3948	cd		.
	defb 031h		;3949	31		1
	defb 032h		;394a	32		2
	defb 0b8h		;394b	b8		.
	defb 032h		;394c	32		2
	defb 035h		;394d	35		5
	defb 0b6h		;394e	b6		.
	defb 035h		;394f	35		5
	defb 031h		;3950	31		1
	defb 0b2h		;3951	b2		.
	defb 031h		;3952	31		1
	defb 030h		;3953	30		0
	defb 032h		;3954	32		2
	defb 0b4h		;3955	b4		.
	defb 06fh		;3956	6f		o
	defb 06eh		;3957	6e		n
	defb 0e5h		;3958	e5		.
	defb 064h		;3959	64		d
	defb 06fh		;395a	6f		o
	defb 075h		;395b	75		u
	defb 062h		;395c	62		b
	defb 06ch		;395d	6c		l
	defb 0e5h		;395e	e5		.
	defb 068h		;395f	68		h
	defb 061h		;3960	61		a
	defb 06ch		;3961	6c		l
	defb 0e6h		;3962	e6		.
	defb 04ch		;3963	4c		L
	defb 04fh		;3964	4f		O
	defb 0d7h		;3965	d7		.
	defb 048h		;3966	48		H
	defb 049h		;3967	49		I
	defb 047h		;3968	47		G
	defb 0c8h		;3969	c8		.
	defb 072h		;396a	72		r
	defb 065h		;396b	65		e
	defb 074h		;396c	74		t
	defb 072h		;396d	72		r
	defb 0f9h		;396e	f9		.
	defb 075h		;396f	75		u
	defb 074h		;3970	74		t
	defb 069h		;3971	69		i
	defb 06ch		;3972	6c		l
	defb 069h		;3973	69		i
	defb 074h		;3974	74		t
	defb 0f9h		;3975	f9		.
	defb 061h		;3976	61		a
	defb 06eh		;3977	6e		n
	defb 061h		;3978	61		a
	defb 06ch		;3979	6c		l
	defb 079h		;397a	79		y
	defb 0f3h		;397b	f3		.
	defb 062h		;397c	62		b
	defb 061h		;397d	61		a
	defb 0e4h		;397e	e4		.
	defb 063h		;397f	63		c
	defb 061h		;3980	61		a
	defb 074h		;3981	74		t
	defb 061h		;3982	61		a
	defb 06ch		;3983	6c		l
	defb 06fh		;3984	6f		o
	defb 067h		;3985	67		g
	defb 075h		;3986	75		u
	defb 0e5h		;3987	e5		.
	defb 069h		;3988	69		i
	defb 06eh		;3989	6e		n
	defb 074h		;398a	74		t
	defb 065h		;398b	65		e
	defb 072h		;398c	72		r
	defb 06ch		;398d	6c		l
	defb 065h		;398e	65		e
	defb 061h		;398f	61		a
	defb 076h		;3990	76		v
	defb 069h		;3991	69		i
	defb 06eh		;3992	6e		n
	defb 0e7h		;3993	e7		.
	defb 0aah		;3994	aa		.
	defb 06eh		;3995	6e		n
	defb 06fh		;3996	6f		o
	defb 072h		;3997	72		r
	defb 06dh		;3998	6d		m
	defb 061h		;3999	61		a
	defb 0ech		;399a	ec		.
	defb 066h		;399b	66		f
	defb 061h		;399c	61		a
	defb 073h		;399d	73		s
	defb 0f4h		;399e	f4		.
	defb 00ch		;399f	0c		.
	defb 01bh		;39a0	1b		.
	defb 00ah		;39a1	0a		.
	defb 087h		;39a2	87		.
	defb 01bh		;39a3	1b		.
	defb 00ch		;39a4	0c		.
	defb 08dh		;39a5	8d		.
	defb 06fh		;39a6	6f		o
	defb 0e6h		;39a7	e6		.
	defb 073h		;39a8	73		s
	defb 061h		;39a9	61		a
	defb 076h		;39aa	76		v
	defb 0e5h		;39ab	e5		.
	defb 06ch		;39ac	6c		l
	defb 06fh		;39ad	6f		o
	defb 061h		;39ae	61		a
	defb 0e4h		;39af	e4		.
	defb 02eh		;39b0	2e		.
	defb 02eh		;39b1	2e		.
	defb 0aeh		;39b2	ae		.
	defb 0a6h		;39b3	a6		.
l39b4h:
	defb 061h		;39b4	61		a
	defb 0f4h		;39b5	f4		.
	defb 061h		;39b6	61		a
	defb 062h		;39b7	62		b
	defb 061h		;39b8	61		a
	defb 06eh		;39b9	6e		n
	defb 064h		;39ba	64		d
	defb 06fh		;39bb	6f		o
	defb 06eh		;39bc	6e		n
	defb 065h		;39bd	65		e
	defb 0e4h		;39be	e4		.
	defb 061h		;39bf	61		a
	defb 06ch		;39c0	6c		l
	defb 072h		;39c1	72		r
	defb 065h		;39c2	65		e
	defb 061h		;39c3	61		a
	defb 064h		;39c4	64		d
	defb 0f9h		;39c5	f9		.
	defb 062h		;39c6	62		b
	defb 061h		;39c7	61		a
	defb 0e4h		;39c8	e4		.
	defb 062h		;39c9	62		b
	defb 069h		;39ca	69		i
	defb 0e7h		;39cb	e7		.
	defb 062h		;39cc	62		b
	defb 06ch		;39cd	6c		l
	defb 06fh		;39ce	6f		o
	defb 063h		;39cf	63		c
	defb 06bh		;39d0	6b		k
	defb 0f3h		;39d1	f3		.
	defb 062h		;39d2	62		b
	defb 072h		;39d3	72		r
	defb 065h		;39d4	65		e
	defb 061h		;39d5	61		a
	defb 06bh		;39d6	6b		k
	defb 070h		;39d7	70		p
	defb 06fh		;39d8	6f		o
	defb 069h		;39d9	69		i
	defb 06eh		;39da	6e		n
	defb 0f4h		;39db	f4		.
	defb 062h		;39dc	62		b
	defb 0f9h		;39dd	f9		.
	defb 063h		;39de	63		c
	defb 06fh		;39df	6f		o
	defb 06dh		;39e0	6d		m
	defb 06dh		;39e1	6d		m
	defb 061h		;39e2	61		a
	defb 06eh		;39e3	6e		n
	defb 0e4h		;39e4	e4		.
	defb 065h		;39e5	65		e
	defb 06eh		;39e6	6e		n
	defb 074h		;39e7	74		t
	defb 065h		;39e8	65		e
	defb 0f2h		;39e9	f2		.
	defb 065h		;39ea	65		e
	defb 06dh		;39eb	6d		m
	defb 070h		;39ec	70		p
	defb 074h		;39ed	74		t
	defb 0f9h		;39ee	f9		.
	defb 064h		;39ef	64		d
	defb 065h		;39f0	65		e
	defb 066h		;39f1	66		f
	defb 069h		;39f2	69		i
	defb 06eh		;39f3	6e		n
	defb 069h		;39f4	69		i
	defb 074h		;39f5	74		t
	defb 069h		;39f6	69		i
	defb 06fh		;39f7	6f		o
	defb 0eeh		;39f8	ee		.
	defb 064h		;39f9	64		d
	defb 065h		;39fa	65		e
	defb 076h		;39fb	76		v
	defb 069h		;39fc	69		i
	defb 073h		;39fd	73		s
	defb 069h		;39fe	69		i
	defb 06fh		;39ff	6f		o
	defb 0eeh		;3a00	ee		.
	defb 066h		;3a01	66		f
	defb 061h		;3a02	61		a
	defb 069h		;3a03	69		i
	defb 06ch		;3a04	6c		l
	defb 065h		;3a05	65		e
	defb 0e4h		;3a06	e4		.
	defb 066h		;3a07	66		f
	defb 069h		;3a08	69		i
	defb 06ch		;3a09	6c		l
	defb 0e5h		;3a0a	e5		.
	defb 066h		;3a0b	66		f
	defb 069h		;3a0c	69		i
	defb 06eh		;3a0d	6e		n
	defb 069h		;3a0e	69		i
	defb 073h		;3a0f	73		s
	defb 0e8h		;3a10	e8		.
	defb 066h		;3a11	66		f
	defb 06fh		;3a12	6f		o
	defb 075h		;3a13	75		u
	defb 06eh		;3a14	6e		n
	defb 0e4h		;3a15	e4		.
	defb 066h		;3a16	66		f
	defb 065h		;3a17	65		e
	defb 0f7h		;3a18	f7		.
	defb 069h		;3a19	69		i
	defb 064h		;3a1a	64		d
	defb 065h		;3a1b	65		e
	defb 06eh		;3a1c	6e		n
	defb 074h		;3a1d	74		t
	defb 069h		;3a1e	69		i
	defb 063h		;3a1f	63		c
	defb 061h		;3a20	61		a
	defb 0ech		;3a21	ec		.
	defb 069h		;3a22	69		i
	defb 06eh		;3a23	6e		n
	defb 073h		;3a24	73		s
	defb 075h		;3a25	75		u
	defb 066h		;3a26	66		f
	defb 066h		;3a27	66		f
	defb 069h		;3a28	69		i
	defb 063h		;3a29	63		c
	defb 069h		;3a2a	69		i
	defb 065h		;3a2b	65		e
	defb 06eh		;3a2c	6e		n
	defb 0f4h		;3a2d	f4		.
	defb 06bh		;3a2e	6b		k
	defb 06eh		;3a2f	6e		n
	defb 06fh		;3a30	6f		o
	defb 077h		;3a31	77		w
	defb 0eeh		;3a32	ee		.
	defb 06ch		;3a33	6c		l
	defb 06fh		;3a34	6f		o
	defb 06eh		;3a35	6e		n
	defb 0e7h		;3a36	e7		.
	defb 06dh		;3a37	6d		m
	defb 061h		;3a38	61		a
	defb 06eh		;3a39	6e		n
	defb 0f9h		;3a3a	f9		.
	defb 06eh		;3a3b	6e		n
	defb 0efh		;3a3c	ef		.
	defb 06eh		;3a3d	6e		n
	defb 06fh		;3a3e	6f		o
	defb 0f4h		;3a3f	f4		.
	defb 06eh		;3a40	6e		n
	defb 075h		;3a41	75		u
	defb 06dh		;3a42	6d		m
	defb 062h		;3a43	62		b
	defb 065h		;3a44	65		e
	defb 0f2h		;3a45	f2		.
	defb 06eh		;3a46	6e		n
	defb 061h		;3a47	61		a
	defb 06dh		;3a48	6d		m
	defb 0e5h		;3a49	e5		.
	defb 06fh		;3a4a	6f		o
	defb 0e6h		;3a4b	e6		.
	defb 06fh		;3a4c	6f		o
	defb 070h		;3a4d	70		p
	defb 065h		;3a4e	65		e
	defb 072h		;3a4f	72		r
	defb 061h		;3a50	61		a
	defb 06eh		;3a51	6e		n
	defb 064h		;3a52	64		d
	defb 0f3h		;3a53	f3		.
	defb 06fh		;3a54	6f		o
	defb 070h		;3a55	70		p
	defb 063h		;3a56	63		c
	defb 06fh		;3a57	6f		o
	defb 064h		;3a58	64		d
	defb 0e5h		;3a59	e5		.
	defb 06fh		;3a5a	6f		o
	defb 070h		;3a5b	70		p
	defb 065h		;3a5c	65		e
	defb 0eeh		;3a5d	ee		.
	defb 06fh		;3a5e	6f		o
	defb 075h		;3a5f	75		u
	defb 0f4h		;3a60	f4		.
	defb 070h		;3a61	70		p
	defb 072h		;3a62	72		r
	defb 065h		;3a63	65		e
	defb 073h		;3a64	73		s
	defb 065h		;3a65	65		e
	defb 06eh		;3a66	6e		n
	defb 0f4h		;3a67	f4		.
	defb 070h		;3a68	70		p
	defb 072h		;3a69	72		r
	defb 06fh		;3a6a	6f		o
	defb 067h		;3a6b	67		g
	defb 072h		;3a6c	72		r
	defb 061h		;3a6d	61		a
	defb 0edh		;3a6e	ed		.
	defb 072h		;3a6f	72		r
	defb 061h		;3a70	61		a
	defb 06eh		;3a71	6e		n
	defb 067h		;3a72	67		g
	defb 0e5h		;3a73	e5		.
	defb 072h		;3a74	72		r
	defb 065h		;3a75	65		e
	defb 064h		;3a76	64		d
	defb 065h		;3a77	65		e
	defb 066h		;3a78	66		f
	defb 069h		;3a79	69		i
	defb 06eh		;3a7a	6e		n
	defb 065h		;3a7b	65		e
	defb 0e4h		;3a7c	e4		.
	defb 073h		;3a7d	73		s
	defb 074h		;3a7e	74		t
	defb 061h		;3a7f	61		a
	defb 063h		;3a80	63		c
	defb 0ebh		;3a81	eb		.
	defb 073h		;3a82	73		s
	defb 070h		;3a83	70		p
	defb 061h		;3a84	61		a
	defb 063h		;3a85	63		c
	defb 0e5h		;3a86	e5		.
	defb 073h		;3a87	73		s
	defb 079h		;3a88	79		y
	defb 06eh		;3a89	6e		n
	defb 074h		;3a8a	74		t
	defb 061h		;3a8b	61		a
	defb 0f8h		;3a8c	f8		.
	defb 073h		;3a8d	73		s
	defb 074h		;3a8e	74		t
	defb 072h		;3a8f	72		r
	defb 069h		;3a90	69		i
	defb 06eh		;3a91	6e		n
	defb 0e7h		;3a92	e7		.
	defb 073h		;3a93	73		s
	defb 06dh		;3a94	6d		m
	defb 061h		;3a95	61		a
	defb 06ch		;3a96	6c		l
	defb 0ech		;3a97	ec		.
	defb 073h		;3a98	73		s
	defb 074h		;3a99	74		t
	defb 061h		;3a9a	61		a
	defb 072h		;3a9b	72		r
	defb 0f4h		;3a9c	f4		.
	defb 073h		;3a9d	73		s
	defb 065h		;3a9e	65		e
	defb 061h		;3a9f	61		a
	defb 072h		;3aa0	72		r
	defb 063h		;3aa1	63		c
	defb 0e8h		;3aa2	e8		.
	defb 074h		;3aa3	74		t
	defb 06fh		;3aa4	6f		o
	defb 0efh		;3aa5	ef		.
	defb 075h		;3aa6	75		u
	defb 06eh		;3aa7	6e		n
	defb 064h		;3aa8	64		d
	defb 065h		;3aa9	65		e
	defb 066h		;3aaa	66		f
	defb 069h		;3aab	69		i
	defb 06eh		;3aac	6e		n
	defb 065h		;3aad	65		e
	defb 0e4h		;3aae	e4		.
	defb 077h		;3aaf	77		w
	defb 06fh		;3ab0	6f		o
	defb 072h		;3ab1	72		r
	defb 06bh		;3ab2	6b		k
	defb 073h		;3ab3	73		s
	defb 070h		;3ab4	70		p
	defb 061h		;3ab5	61		a
	defb 063h		;3ab6	63		c
	defb 0e5h		;3ab7	e5		.
	defb 077h		;3ab8	77		w
	defb 06fh		;3ab9	6f		o
	defb 072h		;3aba	72		r
	defb 0e4h		;3abb	e4		.
	defb 07ah		;3abc	7a		z
	defb 065h		;3abd	65		e
	defb 072h		;3abe	72		r
	defb 0efh		;3abf	ef		.
	defb 0beh		;3ac0	be		.
	defb 073h		;3ac1	73		s
	defb 074h		;3ac2	74		t
	defb 06fh		;3ac3	6f		o
	defb 0f0h		;3ac4	f0		.
	defb 065h		;3ac5	65		e
	defb 072h		;3ac6	72		r
	defb 072h		;3ac7	72		r
	defb 06fh		;3ac8	6f		o
	defb 0f2h		;3ac9	f2		.
	defb 071h		;3aca	71		q
	defb 075h		;3acb	75		u
	defb 069h		;3acc	69		i
	defb 0f4h		;3acd	f4		.
	defb 06fh		;3ace	6f		o
	defb 070h		;3acf	70		p
	defb 065h		;3ad0	65		e
	defb 072h		;3ad1	72		r
	defb 061h		;3ad2	61		a
	defb 06eh		;3ad3	6e		n
	defb 0e4h		;3ad4	e4		.
	defb 0a9h		;3ad5	a9		.
	defb 062h		;3ad6	62		b
	defb 072h		;3ad7	72		r
	defb 061h		;3ad8	61		a
	defb 063h		;3ad9	63		c
	defb 06bh		;3ada	6b		k
	defb 065h		;3adb	65		e
	defb 074h		;3adc	74		t
	defb 0f3h		;3add	f3		.
	defb 069h		;3ade	69		i
	defb 06eh		;3adf	6e		n
	defb 076h		;3ae0	76		v
	defb 061h		;3ae1	61		a
	defb 06ch		;3ae2	6c		l
	defb 069h		;3ae3	69		i
	defb 0e4h		;3ae4	e4		.
	defb 074h		;3ae5	74		t
	defb 079h		;3ae6	79		y
	defb 070h		;3ae7	70		p
	defb 0e5h		;3ae8	e5		.
	defb 069h		;3ae9	69		i
	defb 06ch		;3aea	6c		l
	defb 06ch		;3aeb	6c		l
	defb 065h		;3aec	65		e
	defb 067h		;3aed	67		g
	defb 061h		;3aee	61		a
	defb 0ech		;3aef	ec		.
	defb 076h		;3af0	76		v
	defb 061h		;3af1	61		a
	defb 06ch		;3af2	6c		l
	defb 075h		;3af3	75		u
	defb 0e5h		;3af4	e5		.
	defb 062h		;3af5	62		b
	defb 061h		;3af6	61		a
	defb 06eh		;3af7	6e		n
	defb 0ebh		;3af8	eb		.
	defb 052h		;3af9	52		R
	defb 041h		;3afa	41		A
	defb 0cdh		;3afb	cd		.
	defb 052h		;3afc	52		R
	defb 04fh		;3afd	4f		O
	defb 0cdh		;3afe	cd		.
	defb 066h		;3aff	66		f
	defb 06fh		;3b00	6f		o
	defb 072h		;3b01	72		r
	defb 062h		;3b02	62		b
	defb 069h		;3b03	69		i
	defb 064h		;3b04	64		d
	defb 064h		;3b05	64		d
	defb 065h		;3b06	65		e
	defb 0eeh		;3b07	ee		.
	defb 070h		;3b08	70		p
	defb 072h		;3b09	72		r
	defb 069h		;3b0a	69		i
	defb 06eh		;3b0b	6e		n
	defb 0f4h		;3b0c	f4		.
	defb 020h		;3b0d	20		 
	defb 057h		;3b0e	57		W
	defb 06fh		;3b0f	6f		o
	defb 072h		;3b10	72		r
	defb 06bh		;3b11	6b		k
	defb 069h		;3b12	69		i
	defb 06eh		;3b13	6e		n
	defb 067h		;3b14	67		g
	defb 020h		;3b15	20		 
	defb 02eh		;3b16	2e		.
	defb 02eh		;3b17	2e		.
	defb 0aeh		;3b18	ae		.
	defb 061h		;3b19	61		a
	defb 064h		;3b1a	64		d
	defb 064h		;3b1b	64		d
	defb 072h		;3b1c	72		r
	defb 065h		;3b1d	65		e
	defb 073h		;3b1e	73		s
	defb 0f3h		;3b1f	f3		.
	defb 073h		;3b20	73		s
	defb 074h		;3b21	74		t
	defb 061h		;3b22	61		a
	defb 074h		;3b23	74		t
	defb 0e5h		;3b24	e5		.
	defb 073h		;3b25	73		s
	defb 065h		;3b26	65		e
	defb 06ch		;3b27	6c		l
	defb 065h		;3b28	65		e
	defb 063h		;3b29	63		c
	defb 074h		;3b2a	74		t
	defb 069h		;3b2b	69		i
	defb 06fh		;3b2c	6f		o
	defb 06eh		;3b2d	6e		n
	defb 073h		;3b2e	73		s
	defb 0bah		;3b2f	ba		.
	defb 069h		;3b30	69		i
	defb 06eh		;3b31	6e		n
	defb 069h		;3b32	69		i
	defb 0f4h		;3b33	f4		.
	defb 063h		;3b34	63		c
	defb 06fh		;3b35	6f		o
	defb 075h		;3b36	75		u
	defb 06eh		;3b37	6e		n
	defb 0f4h		;3b38	f4		.
	defb 02ah		;3b39	2a		*
	defb 02ah		;3b3a	2a		*
	defb 0aah		;3b3b	aa		.
	defb 063h		;3b3c	63		c
	defb 075h		;3b3d	75		u
	defb 072h		;3b3e	72		r
	defb 065h		;3b3f	65		e
	defb 06eh		;3b40	6e		n
	defb 0f4h		;3b41	f4		.
l3b42h:
	defb 061h		;3b42	61		a
	defb 06eh		;3b43	6e		n
	defb 061h		;3b44	61		a
	defb 06ch		;3b45	6c		l
	defb 079h		;3b46	79		y
	defb 073h		;3b47	73		s
	defb 065h		;3b48	65		e
	defb 0f2h		;3b49	f2		.
	defb 064h		;3b4a	64		d
	defb 065h		;3b4b	65		e
	defb 066h		;3b4c	66		f
	defb 069h		;3b4d	69		i
	defb 06eh		;3b4e	6e		n
	defb 069h		;3b4f	69		i
	defb 074h		;3b50	74		t
	defb 069h		;3b51	69		i
	defb 06fh		;3b52	6f		o
	defb 06eh		;3b53	6e		n
	defb 073h		;3b54	73		s
	defb 0bah		;3b55	ba		.
	defb 06ch		;3b56	6c		l
	defb 065h		;3b57	65		e
	defb 066h		;3b58	66		f
	defb 0f4h		;3b59	f4		.
	defb 066h		;3b5a	66		f
	defb 06fh		;3b5b	6f		o
	defb 0f2h		;3b5c	f2		.
	defb 062h		;3b5d	62		b
	defb 079h		;3b5e	79		y
	defb 074h		;3b5f	74		t
	defb 065h		;3b60	65		e
	defb 0f3h		;3b61	f3		.
	defb 050h		;3b62	50		P
	defb 072h		;3b63	72		r
	defb 06fh		;3b64	6f		o
	defb 066h		;3b65	66		f
	defb 065h		;3b66	65		e
	defb 073h		;3b67	73		s
	defb 073h		;3b68	73		s
	defb 069h		;3b69	69		i
	defb 06fh		;3b6a	6f		o
	defb 06eh		;3b6b	6e		n
	defb 061h		;3b6c	61		a
	defb 0ech		;3b6d	ec		.
	defb 064h		;3b6e	64		d
	defb 069h		;3b6f	69		i
	defb 073h		;3b70	73		s
	defb 0ebh		;3b71	eb		.
	defb 054h		;3b72	54		T
	defb 052h		;3b73	52		R
	defb 044h		;3b74	44		D
	defb 04fh		;3b75	4f		O
	defb 0d3h		;3b76	d3		.
	defb 065h		;3b77	65		e
	defb 078h		;3b78	78		x
	defb 069h		;3b79	69		i
	defb 073h		;3b7a	73		s
	defb 0f4h		;3b7b	f4		.
	defb 052h		;3b7c	52		R
	defb 02fh		;3b7d	2f		/
	defb 0d7h		;3b7e	d7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ErrorShortStrings: dense pool of 2-4 character
; strings (the ErrorTextTable base) - error
; fragments, month names and menu decorations,
; reached by the menu builder via $3CEC+3*i+2.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WordLists_end:

; BLOCK 'ErrorShortStrings' (start 0x3b7f end 0x3e45)
ErrorShortStrings_start:
	defb 015h		;3b7f	15		.
	defb 013h		;3b80	13		.
	defb 014h		;3b81	14		.
	defb 008h		;3b82	08		.
	defb 096h		;3b83	96		.
	defb 001h		;3b84	01		.
	defb 082h		;3b85	82		.
	defb 009h		;3b86	09		.
	defb 08ah		;3b87	8a		.
	defb 08eh		;3b88	8e		.
	defb 00eh		;3b89	0e		.
	defb 097h		;3b8a	97		.
	defb 08bh		;3b8b	8b		.
	defb 003h		;3b8c	03		.
	defb 082h		;3b8d	82		.
	defb 004h		;3b8e	04		.
	defb 085h		;3b8f	85		.
	defb 006h		;3b90	06		.
	defb 087h		;3b91	87		.
	defb 088h		;3b92	88		.
	defb 08ch		;3b93	8c		.
	defb 00dh		;3b94	0d		.
	defb 0deh		;3b95	de		.
	defb 00fh		;3b96	0f		.
	defb 088h		;3b97	88		.
	defb 090h		;3b98	90		.
	defb 091h		;3b99	91		.
	defb 08ah		;3b9a	8a		.
	defb 092h		;3b9b	92		.
	defb 018h		;3b9c	18		.
	defb 099h		;3b9d	99		.
	defb 01ah		;3b9e	1a		.
	defb 09bh		;3b9f	9b		.
	defb 097h		;3ba0	97		.
	defb 01ah		;3ba1	1a		.
	defb 01ch		;3ba2	1c		.
	defb 09bh		;3ba3	9b		.
	defb 00eh		;3ba4	0e		.
	defb 08ch		;3ba5	8c		.
	defb 09dh		;3ba6	9d		.
	defb 0a8h		;3ba7	a8		.
	defb 0a9h		;3ba8	a9		.
	defb 09eh		;3ba9	9e		.
	defb 0aah		;3baa	aa		.
	defb 0abh		;3bab	ab		.
	defb 01fh		;3bac	1f		.
	defb 0a0h		;3bad	a0		.
	defb 0ach		;3bae	ac		.
	defb 0adh		;3baf	ad		.
	defb 021h		;3bb0	21		!
	defb 0a0h		;3bb1	a0		.
	defb 0afh		;3bb2	af		.
	defb 0aeh		;3bb3	ae		.
	defb 09fh		;3bb4	9f		.
	defb 022h		;3bb5	22		"
	defb 023h		;3bb6	23		#
	defb 0a4h		;3bb7	a4		.
	defb 0b1h		;3bb8	b1		.
	defb 0b0h		;3bb9	b0		.
	defb 022h		;3bba	22		"
	defb 0a7h		;3bbb	a7		.
	defb 026h		;3bbc	26		&
	defb 0a5h		;3bbd	a5		.
	defb 0b2h		;3bbe	b2		.
	defb 033h		;3bbf	33		3
	defb 0b4h		;3bc0	b4		.
	defb 035h		;3bc1	35		5
	defb 0b6h		;3bc2	b6		.
	defb 0b7h		;3bc3	b7		.
	defb 05ch		;3bc4	5c		\
	defb 05fh		;3bc5	5f		_
	defb 0ddh		;3bc6	dd		.
	defb 038h		;3bc7	38		8
	defb 0b4h		;3bc8	b4		.
	defb 00eh		;3bc9	0e		.
	defb 088h		;3bca	88		.
	defb 032h		;3bcb	32		2
	defb 092h		;3bcc	92		.
	defb 0b9h		;3bcd	b9		.
	defb 00eh		;3bce	0e		.
	defb 00dh		;3bcf	0d		.
	defb 0d1h		;3bd0	d1		.
	defb 00dh		;3bd1	0d		.
	defb 0bbh		;3bd2	bb		.
	defb 0bfh		;3bd3	bf		.
	defb 0c0h		;3bd4	c0		.
	defb 0c1h		;3bd5	c1		.
	defb 0c2h		;3bd6	c2		.
	defb 0bch		;3bd7	bc		.
	defb 0c3h		;3bd8	c3		.
	defb 0c4h		;3bd9	c4		.
	defb 0bdh		;3bda	bd		.
	defb 0cbh		;3bdb	cb		.
	defb 0cch		;3bdc	cc		.
	defb 0c6h		;3bdd	c6		.
	defb 0c5h		;3bde	c5		.
	defb 0beh		;3bdf	be		.
	defb 0c7h		;3be0	c7		.
	defb 0c8h		;3be1	c8		.
	defb 0c9h		;3be2	c9		.
	defb 0cah		;3be3	ca		.
	defb 03ah		;3be4	3a		:
	defb 08dh		;3be5	8d		.
	defb 021h		;3be6	21		!
	defb 005h		;3be7	05		.
	defb 080h		;3be8	80		.
	defb 0cfh		;3be9	cf		.
	defb 04dh		;3bea	4d		M
	defb 0cfh		;3beb	cf		.
	defb 04dh		;3bec	4d		M
	defb 0ceh		;3bed	ce		.
	defb 0ceh		;3bee	ce		.
	defb 0bah		;3bef	ba		.
	defb 050h		;3bf0	50		P
	defb 098h		;3bf1	98		.
	defb 00dh		;3bf2	0d		.
	defb 0d1h		;3bf3	d1		.
	defb 00bh		;3bf4	0b		.
	defb 08dh		;3bf5	8d		.
	defb 052h		;3bf6	52		R
	defb 08dh		;3bf7	8d		.
	defb 053h		;3bf8	53		S
	defb 00dh		;3bf9	0d		.
	defb 080h		;3bfa	80		.
	defb 054h		;3bfb	54		T
	defb 08dh		;3bfc	8d		.
	defb 0d5h		;3bfd	d5		.
	defb 032h		;3bfe	32		2
	defb 088h		;3bff	88		.
	defb 00ch		;3c00	0c		.
	defb 056h		;3c01	56		V
	defb 0aeh		;3c02	ae		.
	defb 00ch		;3c03	0c		.
	defb 056h		;3c04	56		V
	defb 0afh		;3c05	af		.
	defb 0d7h		;3c06	d7		.
	defb 0d8h		;3c07	d8		.
	defb 037h		;3c08	37		7
	defb 09eh		;3c09	9e		.
	defb 059h		;3c0a	59		Y
	defb 056h		;3c0b	56		V
	defb 058h		;3c0c	58		X
	defb 00bh		;3c0d	0b		.
	defb 05bh		;3c0e	5b		[
	defb 037h		;3c0f	37		7
	defb 056h		;3c10	56		V
	defb 0dah		;3c11	da		.
	defb 05ch		;3c12	5c		\
	defb 0deh		;3c13	de		.
	defb 05dh		;3c14	5d		]
	defb 0deh		;3c15	de		.
	defb 05ch		;3c16	5c		\
	defb 05fh		;3c17	5f		_
	defb 05dh		;3c18	5d		]
	defb 05bh		;3c19	5b		[
	defb 085h		;3c1a	85		.
l3c1bh:
	defb 00ah		;3c1b	0a		.
	defb 089h		;3c1c	89		.
	defb 006h		;3c1d	06		.
	defb 093h		;3c1e	93		.
	defb 007h		;3c1f	07		.
	defb 080h		;3c20	80		.
	defb 011h		;3c21	11		.
	defb 001h		;3c22	01		.
	defb 080h		;3c23	80		.
	defb 032h		;3c24	32		2
	defb 01ah		;3c25	1a		.
	defb 080h		;3c26	80		.
	defb 0c1h		;3c27	c1		.
	defb 00ah		;3c28	0a		.
	defb 09ah		;3c29	9a		.
	defb 02fh		;3c2a	2f		/
	defb 0a4h		;3c2b	a4		.
	defb 00ah		;3c2c	0a		.
	defb 09bh		;3c2d	9b		.
	defb 02ch		;3c2e	2c		,
	defb 012h		;3c2f	12		.
	defb 09dh		;3c30	9d		.
	defb 02ch		;3c31	2c		,
	defb 017h		;3c32	17		.
	defb 09dh		;3c33	9d		.
	defb 020h		;3c34	20		 
	defb 01ch		;3c35	1c		.
	defb 0a3h		;3c36	a3		.
	defb 015h		;3c37	15		.
	defb 09ah		;3c38	9a		.
	defb 004h		;3c39	04		.
	defb 0a8h		;3c3a	a8		.
	defb 027h		;3c3b	27		'
	defb 0b3h		;3c3c	b3		.
	defb 019h		;3c3d	19		.
	defb 09ah		;3c3e	9a		.
	defb 01ah		;3c3f	1a		.
	defb 02ch		;3c40	2c		,
	defb 085h		;3c41	85		.
	defb 035h		;3c42	35		5
	defb 019h		;3c43	19		.
	defb 091h		;3c44	91		.
	defb 034h		;3c45	34		4
	defb 035h		;3c46	35		5
	defb 019h		;3c47	19		.
	defb 091h		;3c48	91		.
	defb 02ch		;3c49	2c		,
	defb 017h		;3c4a	17		.
	defb 0b6h		;3c4b	b6		.
	defb 038h		;3c4c	38		8
	defb 039h		;3c4d	39		9
	defb 0b7h		;3c4e	b7		.
	defb 009h		;3c4f	09		.
	defb 019h		;3c50	19		.
	defb 091h		;3c51	91		.
	defb 03ah		;3c52	3a		:
	defb 03bh		;3c53	3b		;
	defb 0b5h		;3c54	b5		.
	defb 038h		;3c55	38		8
	defb 03ch		;3c56	3c		<
	defb 0bdh		;3c57	bd		.
	defb 038h		;3c58	38		8
	defb 03ch		;3c59	3c		<
	defb 0beh		;3c5a	be		.
	defb 009h		;3c5b	09		.
	defb 082h		;3c5c	82		.
	defb 03fh		;3c5d	3f		?
	defb 03ch		;3c5e	3c		<
	defb 0beh		;3c5f	be		.
	defb 03fh		;3c60	3f		?
	defb 03ch		;3c61	3c		<
	defb 0bdh		;3c62	bd		.
	defb 02ah		;3c63	2a		*
	defb 031h		;3c64	31		1
	defb 090h		;3c65	90		.
	defb 040h		;3c66	40		@
	defb 082h		;3c67	82		.
	defb 028h		;3c68	28		(
	defb 02ch		;3c69	2c		,
	defb 096h		;3c6a	96		.
	defb 028h		;3c6b	28		(
	defb 019h		;3c6c	19		.
	defb 091h		;3c6d	91		.
	defb 018h		;3c6e	18		.
	defb 02bh		;3c6f	2b		+
	defb 0a8h		;3c70	a8		.
	defb 038h		;3c71	38		8
	defb 01ah		;3c72	1a		.
	defb 087h		;3c73	87		.
	defb 038h		;3c74	38		8
	defb 043h		;3c75	43		C
	defb 087h		;3c76	87		.
	defb 038h		;3c77	38		8
	defb 039h		;3c78	39		9
	defb 087h		;3c79	87		.
	defb 004h		;3c7a	04		.
	defb 042h		;3c7b	42		B
	defb 087h		;3c7c	87		.
	defb 007h		;3c7d	07		.
	defb 0adh		;3c7e	ad		.
	defb 048h		;3c7f	48		H
	defb 007h		;3c80	07		.
	defb 0c4h		;3c81	c4		.
	defb 018h		;3c82	18		.
	defb 042h		;3c83	42		B
	defb 03ch		;3c84	3c		<
	defb 039h		;3c85	39		9
	defb 043h		;3c86	43		C
	defb 045h		;3c87	45		E
	defb 046h		;3c88	46		F
	defb 0afh		;3c89	af		.
	defb 047h		;3c8a	47		G
	defb 02dh		;3c8b	2d		-
	defb 0c7h		;3c8c	c7		.
	defb 004h		;3c8d	04		.
	defb 09eh		;3c8e	9e		.
	defb 010h		;3c8f	10		.
	defb 0a2h		;3c90	a2		.
	defb 004h		;3c91	04		.
	defb 0b5h		;3c92	b5		.
	defb 014h		;3c93	14		.
	defb 022h		;3c94	22		"
	defb 0a6h		;3c95	a6		.
	defb 022h		;3c96	22		"
	defb 0adh		;3c97	ad		.
	defb 03ah		;3c98	3a		:
	defb 02fh		;3c99	2f		/
	defb 09bh		;3c9a	9b		.
	defb 018h		;3c9b	18		.
	defb 08ch		;3c9c	8c		.
	defb 02fh		;3c9d	2f		/
	defb 019h		;3c9e	19		.
	defb 095h		;3c9f	95		.
	defb 03fh		;3ca0	3f		?
	defb 039h		;3ca1	39		9
	defb 0afh		;3ca2	af		.
	defb 049h		;3ca3	49		I
	defb 0cah		;3ca4	ca		.
	defb 026h		;3ca5	26		&
	defb 04bh		;3ca6	4b		K
	defb 04ch		;3ca7	4c		L
	defb 025h		;3ca8	25		%
	defb 080h		;3ca9	80		.
	defb 0cdh		;3caa	cd		.
	defb 025h		;3cab	25		%
	defb 019h		;3cac	19		.
	defb 08bh		;3cad	8b		.
	defb 043h		;3cae	43		C
	defb 01ch		;3caf	1c		.
	defb 0a5h		;3cb0	a5		.
	defb 049h		;3cb1	49		I
	defb 0b3h		;3cb2	b3		.
	defb 020h		;3cb3	20		 
	defb 01ch		;3cb4	1c		.
	defb 025h		;3cb5	25		%
	defb 0a6h		;3cb6	a6		.
	defb 025h		;3cb7	25		%
	defb 08bh		;3cb8	8b		.
	defb 02eh		;3cb9	2e		.
	defb 094h		;3cba	94		.
	defb 0aeh		;3cbb	ae		.
	defb 04eh		;3cbc	4e		N
	defb 022h		;3cbd	22		"
	defb 019h		;3cbe	19		.
	defb 091h		;3cbf	91		.
	defb 015h		;3cc0	15		.
	defb 0cfh		;3cc1	cf		.
	defb 04fh		;3cc2	4f		O
	defb 019h		;3cc3	19		.
	defb 0d0h		;3cc4	d0		.
	defb 00dh		;3cc5	0d		.
	defb 008h		;3cc6	08		.
	defb 0b0h		;3cc7	b0		.
	defb 00eh		;3cc8	0e		.
	defb 081h		;3cc9	81		.
	defb 02ch		;3cca	2c		,
	defb 012h		;3ccb	12		.
	defb 086h		;3ccc	86		.
	defb 00ah		;3ccd	0a		.
	defb 0c2h		;3cce	c2		.
	defb 00eh		;3ccf	0e		.
	defb 0cfh		;3cd0	cf		.
	defb 038h		;3cd1	38		8
	defb 00fh		;3cd2	0f		.
	defb 09bh		;3cd3	9b		.
	defb 004h		;3cd4	04		.
	defb 046h		;3cd5	46		F
	defb 01ch		;3cd6	1c		.
	defb 08fh		;3cd7	8f		.
	defb 00fh		;3cd8	0f		.
	defb 003h		;3cd9	03		.
	defb 0d1h		;3cda	d1		.
	defb 00fh		;3cdb	0f		.
	defb 019h		;3cdc	19		.
	defb 091h		;3cdd	91		.
	defb 04fh		;3cde	4f		O
	defb 018h		;3cdf	18		.
	defb 0a6h		;3ce0	a6		.
	defb 03ah		;3ce1	3a		:
	defb 00fh		;3ce2	0f		.
	defb 0b9h		;3ce3	b9		.
	defb 080h		;3ce4	80		.
	defb 080h		;3ce5	80		.
	defb 080h		;3ce6	80		.
	defb 080h		;3ce7	80		.
	defb 080h		;3ce8	80		.
	defb 052h		;3ce9	52		R
	defb 033h		;3cea	33		3
	defb 080h		;3ceb	80		.
l3cech:
	defb 0b4h		;3cec	b4		.
	defb 02dh		;3ced	2d		-
	defb 080h		;3cee	80		.
	defb 0beh		;3cef	be		.
	defb 02dh		;3cf0	2d		-
	defb 080h		;3cf1	80		.
	defb 0d4h		;3cf2	d4		.
	defb 02bh		;3cf3	2b		+
	defb 001h		;3cf4	01		.
	defb 060h		;3cf5	60		`
	defb 02ah		;3cf6	2a		*
	defb 045h		;3cf7	45		E
	defb 03fh		;3cf8	3f		?
	defb 02ah		;3cf9	2a		*
	defb 002h		;3cfa	02		.
	defb 039h		;3cfb	39		9
	defb 02eh		;3cfc	2e		.
	defb 042h		;3cfd	42		B
	defb 04ch		;3cfe	4c		L
	defb 031h		;3cff	31		1
l3d00h:
	defb 041h		;3d00	41		A
	defb 010h		;3d01	10		.
	defb 02dh		;3d02	2d		-
	defb 044h		;3d03	44		D
	defb 041h		;3d04	41		A
	defb 030h		;3d05	30		0
	defb 041h		;3d06	41		A
	defb 0adh		;3d07	ad		.
	defb 02dh		;3d08	2d		-
	defb 081h		;3d09	81		.
	defb 00ch		;3d0a	0c		.
	defb 02ah		;3d0b	2a		*
	defb 001h		;3d0c	01		.
	defb 096h		;3d0d	96		.
	defb 035h		;3d0e	35		5
	defb 042h		;3d0f	42		B
	defb 0a7h		;3d10	a7		.
	defb 035h		;3d11	35		5
	defb 042h		;3d12	42		B
	defb 0d7h		;3d13	d7		.
	defb 027h		;3d14	27		'
	defb 020h		;3d15	20		 
	defb 002h		;3d16	02		.
	defb 02eh		;3d17	2e		.
	defb 000h		;3d18	00		.
	defb 006h		;3d19	06		.
	defb 02eh		;3d1a	2e		.
	defb 000h		;3d1b	00		.
	defb 0d4h		;3d1c	d4		.
	defb 009h		;3d1d	09		.
	defb 000h		;3d1e	00		.
	defb 095h		;3d1f	95		.
	defb 02dh		;3d20	2d		-
	defb 044h		;3d21	44		D
	defb 00ch		;3d22	0c		.
	defb 02ch		;3d23	2c		,
	defb 082h		;3d24	82		.
	defb 023h		;3d25	23		#
	defb 02eh		;3d26	2e		.
	defb 001h		;3d27	01		.
	defb 027h		;3d28	27		'
	defb 02bh		;3d29	2b		+
	defb 001h		;3d2a	01		.
	defb 054h		;3d2b	54		T
	defb 02eh		;3d2c	2e		.
	defb 042h		;3d2d	42		B
	defb 0adh		;3d2e	ad		.
	defb 028h		;3d2f	28		(
l3d30h:
	defb 000h		;3d30	00		.
	defb 0adh		;3d31	ad		.
	defb 028h		;3d32	28		(
	defb 000h		;3d33	00		.
	defb 048h		;3d34	48		H
	defb 031h		;3d35	31		1
	defb 041h		;3d36	41		A
	defb 08dh		;3d37	8d		.
	defb 035h		;3d38	35		5
	defb 042h		;3d39	42		B
	defb 09eh		;3d3a	9e		.
	defb 035h		;3d3b	35		5
	defb 042h		;3d3c	42		B
	defb 0f5h		;3d3d	f5		.
	defb 027h		;3d3e	27		'
	defb 020h		;3d3f	20		 
	defb 0fch		;3d40	fc		.
	defb 02dh		;3d41	2d		-
	defb 001h		;3d42	01		.
	defb 0d0h		;3d43	d0		.
	defb 02ch		;3d44	2c		,
	defb 044h		;3d45	44		D
	defb 0eeh		;3d46	ee		.
	defb 02ah		;3d47	2a		*
	defb 002h		;3d48	02		.
	defb 01bh		;3d49	1b		.
	defb 02bh		;3d4a	2b		+
	defb 002h		;3d4b	02		.
	defb 046h		;3d4c	46		F
	defb 035h		;3d4d	35		5
	defb 001h		;3d4e	01		.
	defb 0edh		;3d4f	ed		.
	defb 02dh		;3d50	2d		-
	defb 000h		;3d51	00		.
	defb 091h		;3d52	91		.
	defb 02eh		;3d53	2e		.
	defb 001h		;3d54	01		.
	defb 060h		;3d55	60		`
	defb 02eh		;3d56	2e		.
	defb 001h		;3d57	01		.
	defb 0e6h		;3d58	e6		.
	defb 027h		;3d59	27		'
	defb 020h		;3d5a	20		 
	defb 003h		;3d5b	03		.
	defb 02ch		;3d5c	2c		,
	defb 083h		;3d5d	83		.
	defb 0fdh		;3d5e	fd		.
	defb 02eh		;3d5f	2e		.
	defb 041h		;3d60	41		A
	defb 070h		;3d61	70		p
	defb 02bh		;3d62	2b		+
	defb 000h		;3d63	00		.
	defb 097h		;3d64	97		.
	defb 028h		;3d65	28		(
	defb 001h		;3d66	01		.
	defb 0f1h		;3d67	f1		.
	defb 02bh		;3d68	2b		+
	defb 001h		;3d69	01		.
	defb 000h		;3d6a	00		.
l3d6bh:
	defb 02dh		;3d6b	2d		-
	defb 0e8h		;3d6c	e8		.
	defb 05dh		;3d6d	5d		]
	defb 0e8h		;3d6e	e8		.
	defb 06fh		;3d6f	6f		o
	defb 0e8h		;3d70	e8		.
	defb 04bh		;3d71	4b		K
	defb 0e8h		;3d72	e8		.
	defb 000h		;3d73	00		.
	defb 000h		;3d74	00		.
	defb 08dh		;3d75	8d		.
	defb 0e8h		;3d76	e8		.
	defb 000h		;3d77	00		.
	defb 000h		;3d78	00		.
	defb 0bfh		;3d79	bf		.
	defb 0e8h		;3d7a	e8		.
	defb 0f7h		;3d7b	f7		.
	defb 0e8h		;3d7c	e8		.
	defb 0cdh		;3d7d	cd		.
	defb 0e8h		;3d7e	e8		.
l3d7fh:
	defb 040h		;3d7f	40		@
	defb 051h		;3d80	51		Q
	defb 040h		;3d81	40		@
	defb 04bh		;3d82	4b		K
	defb 040h		;3d83	40		@
	defb 04dh		;3d84	4d		M
	defb 040h		;3d85	40		@
	defb 045h		;3d86	45		E
	defb 040h		;3d87	40		@
	defb 04eh		;3d88	4e		N
	defb 046h		;3d89	46		F
	defb 04ch		;3d8a	4c		L
	defb 0ech		;3d8b	ec		.
	defb 01fh		;3d8c	1f		.
	defb 001h		;3d8d	01		.
	defb 009h		;3d8e	09		.
	defb 00dh		;3d8f	0d		.
	defb 03bh		;3d90	3b		;
	defb 0f1h		;3d91	f1		.
	defb 01fh		;3d92	1f		.
	defb 0cah		;3d93	ca		.
	defb 0ffh		;3d94	ff		.
	defb 080h		;3d95	80		.
	defb 03ch		;3d96	3c		<
	defb 03dh		;3d97	3d		=
	defb 00dh		;3d98	0d		.
	defb 038h		;3d99	38		8
	defb 0f1h		;3d9a	f1		.
	defb 01fh		;3d9b	1f		.
	defb 0cah		;3d9c	ca		.
	defb 0ffh		;3d9d	ff		.
	defb 020h		;3d9e	20		 
	defb 039h		;3d9f	39		9
	defb 03ah		;3da0	3a		:
	defb 042h		;3da1	42		B
	defb 052h		;3da2	52		R
	defb 018h		;3da3	18		.
	defb 001h		;3da4	01		.
	defb 01bh		;3da5	1b		.
	defb 00dh		;3da6	0d		.
	defb 03bh		;3da7	3b		;
	defb 0edh		;3da8	ed		.
	defb 01fh		;3da9	1f		.
	defb 0cah		;3daa	ca		.
	defb 0ffh		;3dab	ff		.
	defb 080h		;3dac	80		.
	defb 03ch		;3dad	3c		<
	defb 03dh		;3dae	3d		=
	defb 04dh		;3daf	4d		M
	defb 038h		;3db0	38		8
	defb 0edh		;3db1	ed		.
	defb 01fh		;3db2	1f		.
	defb 0cah		;3db3	ca		.
	defb 0ffh		;3db4	ff		.
	defb 040h		;3db5	40		@
	defb 039h		;3db6	39		9
	defb 03ah		;3db7	3a		:
	defb 047h		;3db8	47		G
	defb 033h		;3db9	33		3
	defb 0ebh		;3dba	eb		.
	defb 01fh		;3dbb	1f		.
	defb 034h		;3dbc	34		4
	defb 035h		;3dbd	35		5
	defb 036h		;3dbe	36		6
	defb 037h		;3dbf	37		7
	defb 00fh		;3dc0	0f		.
	defb 040h		;3dc1	40		@
	defb 0f1h		;3dc2	f1		.
	defb 01fh		;3dc3	1f		.
	defb 0cah		;3dc4	ca		.
	defb 0ffh		;3dc5	ff		.
	defb 041h		;3dc6	41		A
	defb 042h		;3dc7	42		B
	defb 043h		;3dc8	43		C
	defb 044h		;3dc9	44		D
	defb 00fh		;3dca	0f		.
	defb 01ah		;3dcb	1a		.
	defb 0edh		;3dcc	ed		.
	defb 01fh		;3dcd	1f		.
	defb 0cah		;3dce	ca		.
	defb 0ffh		;3dcf	ff		.
	defb 047h		;3dd0	47		G
	defb 048h		;3dd1	48		H
	defb 049h		;3dd2	49		I
	defb 04ah		;3dd3	4a		J
	defb 041h		;3dd4	41		A
	defb 031h		;3dd5	31		1
	defb 014h		;3dd6	14		.
	defb 001h		;3dd7	01		.
	defb 025h		;3dd8	25		%
	defb 026h		;3dd9	26		&
	defb 041h		;3dda	41		A
	defb 030h		;3ddb	30		0
	defb 014h		;3ddc	14		.
	defb 080h		;3ddd	80		.
	defb 025h		;3dde	25		%
	defb 026h		;3ddf	26		&
	defb 040h		;3de0	40		@
	defb 00ah		;3de1	0a		.
	defb 040h		;3de2	40		@
	defb 029h		;3de3	29		)
	defb 002h		;3de4	02		.
	defb 027h		;3de5	27		'
	defb 03fh		;3de6	3f		?
	defb 000h		;3de7	00		.
	defb 0eah		;3de8	ea		.
	defb 002h		;3de9	02		.
	defb 028h		;3dea	28		(
	defb 03eh		;3deb	3e		>
	defb 000h		;3dec	00		.
	defb 079h		;3ded	79		y
	defb 002h		;3dee	02		.
	defb 02ah		;3def	2a		*
	defb 028h		;3df0	28		(
	defb 000h		;3df1	00		.
	defb 021h		;3df2	21		!
	defb 042h		;3df3	42		B
	defb 02bh		;3df4	2b		+
	defb 027h		;3df5	27		'
	defb 000h		;3df6	00		.
	defb 021h		;3df7	21		!
	defb 002h		;3df8	02		.
	defb 02eh		;3df9	2e		.
	defb 029h		;3dfa	29		)
	defb 000h		;3dfb	00		.
	defb 021h		;3dfc	21		!
	defb 001h		;3dfd	01		.
	defb 023h		;3dfe	23		#
	defb 015h		;3dff	15		.
l3e00h:
	defb 080h		;3e00	80		.
	defb 00eh		;3e01	0e		.
	defb 00fh		;3e02	0f		.
	defb 041h		;3e03	41		A
	defb 01ah		;3e04	1a		.
	defb 015h		;3e05	15		.
	defb 002h		;3e06	02		.
	defb 01bh		;3e07	1b		.
	defb 01ch		;3e08	1c		.
	defb 001h		;3e09	01		.
	defb 01dh		;3e0a	1d		.
	defb 015h		;3e0b	15		.
	defb 004h		;3e0c	04		.
	defb 01eh		;3e0d	1e		.
	defb 01fh		;3e0e	1f		.
	defb 001h		;3e0f	01		.
	defb 020h		;3e10	20		 
	defb 015h		;3e11	15		.
	defb 008h		;3e12	08		.
	defb 021h		;3e13	21		!
	defb 022h		;3e14	22		"
	defb 041h		;3e15	41		A
	defb 017h		;3e16	17		.
	defb 015h		;3e17	15		.
	defb 001h		;3e18	01		.
	defb 018h		;3e19	18		.
	defb 019h		;3e1a	19		.
	defb 040h		;3e1b	40		@
	defb 00bh		;3e1c	0b		.
	defb 040h		;3e1d	40		@
	defb 02ch		;3e1e	2c		,
	defb 080h		;3e1f	80		.
	defb 007h		;3e20	07		.
	defb 080h		;3e21	80		.
	defb 008h		;3e22	08		.
	defb 040h		;3e23	40		@
	defb 009h		;3e24	09		.
	defb 040h		;3e25	40		@
	defb 02dh		;3e26	2d		-
	defb 040h		;3e27	40		@
	defb 00ah		;3e28	0a		.
	defb 041h		;3e29	41		A
	defb 003h		;3e2a	03		.
	defb 012h		;3e2b	12		.
	defb 004h		;3e2c	04		.
	defb 00eh		;3e2d	0e		.
	defb 00fh		;3e2e	0f		.
	defb 040h		;3e2f	40		@
	defb 003h		;3e30	03		.
	defb 040h		;3e31	40		@
	defb 055h		;3e32	55		U
	defb 040h		;3e33	40		@
	defb 054h		;3e34	54		T
	defb 040h		;3e35	40		@
	defb 00ch		;3e36	0c		.
	defb 040h		;3e37	40		@
	defb 004h		;3e38	04		.
	defb 040h		;3e39	40		@
	defb 053h		;3e3a	53		S
	defb 040h		;3e3b	40		@
l3e3ch:
	defb 05ah		;3e3c	5a		Z
	defb 040h		;3e3d	40		@
	defb 05bh		;3e3e	5b		[
	defb 041h		;3e3f	41		A
	defb 058h		;3e40	58		X
	defb 019h		;3e41	19		.
	defb 040h		;3e42	40		@
	defb 056h		;3e43	56		V
	defb 057h		;3e44	57		W
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EncodedTable3E45: second XOR-masked region,
; unmasked at boot into DecodedTables ($E82D) by
; DecodeTables ($05A2) using the keys at $00FC.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ErrorShortStrings_end:

; BLOCK 'EncodedTable3E45' (start 0x3e45 end 0x3f69)
EncodedTable3E45_start:
	defb 0cfh		;3e45	cf		.
	defb 0f4h		;3e46	f4		.
	defb 0f7h		;3e47	f7		.
	defb 0bbh		;3e48	bb		.
	defb 057h		;3e49	57		W
	defb 056h		;3e4a	56		V
	defb 0d1h		;3e4b	d1		.
	defb 0bfh		;3e4c	bf		.
	defb 057h		;3e4d	57		W
	defb 086h		;3e4e	86		.
	defb 0b2h		;3e4f	b2		.
	defb 0ach		;3e50	ac		.
	defb 055h		;3e51	55		U
	defb 0a7h		;3e52	a7		.
	defb 0cbh		;3e53	cb		.
	defb 0cch		;3e54	cc		.
	defb 049h		;3e55	49		I
	defb 086h		;3e56	86		.
	defb 0c3h		;3e57	c3		.
	defb 0abh		;3e58	ab		.
	defb 0e1h		;3e59	e1		.
	defb 091h		;3e5a	91		.
	defb 0d0h		;3e5b	d0		.
	defb 0afh		;3e5c	af		.
	defb 059h		;3e5d	59		Y
	defb 097h		;3e5e	97		.
	defb 0d9h		;3e5f	d9		.
	defb 0bbh		;3e60	bb		.
	defb 02dh		;3e61	2d		-
	defb 019h		;3e62	19		.
	defb 0e8h		;3e63	e8		.
	defb 0b4h		;3e64	b4		.
	defb 0e9h		;3e65	e9		.
	defb 0d9h		;3e66	d9		.
	defb 037h		;3e67	37		7
	defb 01ah		;3e68	1a		.
	defb 0b2h		;3e69	b2		.
	defb 0d0h		;3e6a	d0		.
	defb 037h		;3e6b	37		7
	defb 037h		;3e6c	37		7
	defb 0a2h		;3e6d	a2		.
	defb 098h		;3e6e	98		.
	defb 023h		;3e6f	23		#
	defb 09ch		;3e70	9c		.
	defb 0b1h		;3e71	b1		.
	defb 091h		;3e72	91		.
	defb 025h		;3e73	25		%
	defb 09bh		;3e74	9b		.
	defb 09ah		;3e75	9a		.
	defb 085h		;3e76	85		.
	defb 087h		;3e77	87		.
	defb 0cbh		;3e78	cb		.
	defb 025h		;3e79	25		%
	defb 00ch		;3e7a	0c		.
	defb 0b0h		;3e7b	b0		.
	defb 0cfh		;3e7c	cf		.
	defb 035h		;3e7d	35		5
	defb 0adh		;3e7e	ad		.
	defb 0bfh		;3e7f	bf		.
	defb 0c3h		;3e80	c3		.
	defb 03fh		;3e81	3f		?
	defb 0aeh		;3e82	ae		.
	defb 0afh		;3e83	af		.
	defb 0c7h		;3e84	c7		.
	defb 03fh		;3e85	3f		?
	defb 0a5h		;3e86	a5		.
	defb 0b1h		;3e87	b1		.
	defb 0b0h		;3e88	b0		.
	defb 0b5h		;3e89	b5		.
	defb 0fdh		;3e8a	fd		.
	defb 013h		;3e8b	13		.
	defb 03eh		;3e8c	3e		>
	defb 082h		;3e8d	82		.
	defb 08ah		;3e8e	8a		.
	defb 015h		;3e8f	15		.
	defb 04fh		;3e90	4f		O
	defb 08dh		;3e91	8d		.
	defb 0f5h		;3e92	f5		.
	defb 027h		;3e93	27		'
	defb 0ceh		;3e94	ce		.
	defb 094h		;3e95	94		.
	defb 0e9h		;3e96	e9		.
	defb 0c6h		;3e97	c6		.
	defb 0d0h		;3e98	d0		.
	defb 081h		;3e99	81		.
	defb 0edh		;3e9a	ed		.
	defb 0a7h		;3e9b	a7		.
	defb 0d2h		;3e9c	d2		.
	defb 097h		;3e9d	97		.
	defb 0f9h		;3e9e	f9		.
	defb 06fh		;3e9f	6f		o
	defb 05fh		;3ea0	5f		_
	defb 0ffh		;3ea1	ff		.
	defb 0e5h		;3ea2	e5		.
	defb 011h		;3ea3	11		.
	defb 0bdh		;3ea4	bd		.
	defb 0a2h		;3ea5	a2		.
	defb 040h		;3ea6	40		@
	defb 057h		;3ea7	57		W
	defb 01bh		;3ea8	1b		.
	defb 0f5h		;3ea9	f5		.
	defb 0dch		;3eaa	dc		.
	defb 07ah		;3eab	7a		z
	defb 01fh		;3eac	1f		.
	defb 0c7h		;3ead	c7		.
	defb 0adh		;3eae	ad		.
	defb 06ch		;3eaf	6c		l
	defb 067h		;3eb0	67		g
	defb 0d9h		;3eb1	d9		.
	defb 0a9h		;3eb2	a9		.
	defb 07ah		;3eb3	7a		z
	defb 063h		;3eb4	63		c
	defb 0d3h		;3eb5	d3		.
	defb 0b5h		;3eb6	b5		.
	defb 077h		;3eb7	77		w
	defb 07fh		;3eb8	7f		.
	defb 0d5h		;3eb9	d5		.
	defb 0b1h		;3eba	b1		.
	defb 067h		;3ebb	67		g
	defb 00fh		;3ebc	0f		.
	defb 03fh		;3ebd	3f		?
	defb 0bdh		;3ebe	bd		.
	defb 06ch		;3ebf	6c		l
	defb 003h		;3ec0	03		.
	defb 039h		;3ec1	39		9
	defb 0b0h		;3ec2	b0		.
	defb 06dh		;3ec3	6d		m
	defb 007h		;3ec4	07		.
	defb 026h		;3ec5	26		&
	defb 08ch		;3ec6	8c		.
	defb 041h		;3ec7	41		A
	defb 03bh		;3ec8	3b		;
	defb 00ch		;3ec9	0c		.
	defb 088h		;3eca	88		.
	defb 05fh		;3ecb	5f		_
	defb 03fh		;3ecc	3f		?
	defb 014h		;3ecd	14		.
	defb 084h		;3ece	84		.
	defb 048h		;3ecf	48		H
	defb 033h		;3ed0	33		3
	defb 017h		;3ed1	17		.
	defb 080h		;3ed2	80		.
	defb 049h		;3ed3	49		I
	defb 037h		;3ed4	37		7
	defb 01ah		;3ed5	1a		.
	defb 07ah		;3ed6	7a		z
	defb 065h		;3ed7	65		e
	defb 04bh		;3ed8	4b		K
	defb 065h		;3ed9	65		e
	defb 02dh		;3eda	2d		-
	defb 0c3h		;3edb	c3		.
	defb 0eeh		;3edc	ee		.
	defb 053h		;3edd	53		S
	defb 03eh		;3ede	3e		>
	defb 034h		;3edf	34		4
	defb 09fh		;3ee0	9f		.
	defb 05eh		;3ee1	5e		^
	defb 025h		;3ee2	25		%
	defb 03eh		;3ee3	3e		>
	defb 086h		;3ee4	86		.
	defb 060h		;3ee5	60		`
	defb 024h		;3ee6	24		$
	defb 017h		;3ee7	17		.
	defb 05bh		;3ee8	5b		[
	defb 0b5h		;3ee9	b5		.
	defb 09ch		;3eea	9c		.
	defb 037h		;3eeb	37		7
	defb 05fh		;3eec	5f		_
	defb 028h		;3eed	28		(
	defb 0b4h		;3eee	b4		.
	defb 02bh		;3eef	2b		+
	defb 053h		;3ef0	53		S
	defb 02dh		;3ef1	2d		-
	defb 0e9h		;3ef2	e9		.
	defb 028h		;3ef3	28		(
	defb 057h		;3ef4	57		W
	defb 03eh		;3ef5	3e		>
	defb 0f5h		;3ef6	f5		.
	defb 037h		;3ef7	37		7
	defb 04bh		;3ef8	4b		K
	defb 04eh		;3ef9	4e		N
	defb 0b0h		;3efa	b0		.
	defb 02fh		;3efb	2f		/
	defb 04fh		;3efc	4f		O
	defb 023h		;3efd	23		#
	defb 0f4h		;3efe	f4		.
	defb 03dh		;3eff	3d		=
	defb 043h		;3f00	43		C
	defb 014h		;3f01	14		.
	defb 0f9h		;3f02	f9		.
	defb 022h		;3f03	22		"
	defb 047h		;3f04	47		G
	defb 019h		;3f05	19		.
	defb 0c5h		;3f06	c5		.
	defb 012h		;3f07	12		.
	defb 07bh		;3f08	7b		{
	defb 074h		;3f09	74		t
	defb 085h		;3f0a	85		.
	defb 00ah		;3f0b	0a		.
	defb 07fh		;3f0c	7f		.
	defb 03bh		;3f0d	3b		;
	defb 0c4h		;3f0e	c4		.
	defb 03bh		;3f0f	3b		;
	defb 071h		;3f10	71		q
	defb 03dh		;3f11	3d		=
	defb 075h		;3f12	75		u
	defb 09bh		;3f13	9b		.
	defb 0b6h		;3f14	b6		.
	defb 01dh		;3f15	1d		.
	defb 069h		;3f16	69		i
	defb 01eh		;3f17	1e		.
	defb 08eh		;3f18	8e		.
	defb 006h		;3f19	06		.
	defb 06dh		;3f1a	6d		m
	defb 023h		;3f1b	23		#
	defb 0c7h		;3f1c	c7		.
	defb 007h		;3f1d	07		.
	defb 061h		;3f1e	61		a
	defb 029h		;3f1f	29		)
	defb 0feh		;3f20	fe		.
	defb 019h		;3f21	19		.
	defb 065h		;3f22	65		e
	defb 023h		;3f23	23		#
	defb 0fbh		;3f24	fb		.
	defb 06ah		;3f25	6a		j
	defb 061h		;3f26	61		a
	defb 0eeh		;3f27	ee		.
	defb 066h		;3f28	66		f
	defb 065h		;3f29	65		e
	defb 0e2h		;3f2a	e2		.
	defb 06dh		;3f2b	6d		m
	defb 061h		;3f2c	61		a
	defb 0f2h		;3f2d	f2		.
	defb 061h		;3f2e	61		a
	defb 070h		;3f2f	70		p
	defb 0f2h		;3f30	f2		.
	defb 06dh		;3f31	6d		m
	defb 061h		;3f32	61		a
	defb 0f9h		;3f33	f9		.
	defb 06ah		;3f34	6a		j
	defb 075h		;3f35	75		u
	defb 0eeh		;3f36	ee		.
	defb 06ah		;3f37	6a		j
	defb 075h		;3f38	75		u
	defb 0ech		;3f39	ec		.
	defb 061h		;3f3a	61		a
	defb 075h		;3f3b	75		u
	defb 0e7h		;3f3c	e7		.
	defb 073h		;3f3d	73		s
	defb 065h		;3f3e	65		e
	defb 0f0h		;3f3f	f0		.
	defb 06fh		;3f40	6f		o
	defb 063h		;3f41	63		c
	defb 0f4h		;3f42	f4		.
	defb 06eh		;3f43	6e		n
	defb 06fh		;3f44	6f		o
	defb 0f6h		;3f45	f6		.
	defb 064h		;3f46	64		d
	defb 065h		;3f47	65		e
	defb 0e3h		;3f48	e3		.
	defb 01ah		;3f49	1a		.
	defb 00fh		;3f4a	0f		.
	defb 008h		;3f4b	08		.
	defb 004h		;3f4c	04		.
	defb 016h		;3f4d	16		.
	defb 010h		;3f4e	10		.
	defb 009h		;3f4f	09		.
	defb 005h		;3f50	05		.
	defb 0c0h		;3f51	c0		.
	defb 0ffh		;3f52	ff		.
	defb 000h		;3f53	00		.
	defb 003h		;3f54	03		.
	defb 0c0h		;3f55	c0		.
	defb 0c0h		;3f56	c0		.
	defb 0ffh		;3f57	ff		.
	defb 0ffh		;3f58	ff		.
	defb 0a1h		;3f59	a1		.
	defb 0a1h		;3f5a	a1		.
	defb 000h		;3f5b	00		.
	defb 000h		;3f5c	00		.
	defb 000h		;3f5d	00		.
	defb 000h		;3f5e	00		.
	defb 000h		;3f5f	00		.
	defb 000h		;3f60	00		.
	defb 0ffh		;3f61	ff		.
	defb 0ffh		;3f62	ff		.
	defb 0ffh		;3f63	ff		.
	defb 0ffh		;3f64	ff		.
	defb 000h		;3f65	00		.
	defb 0ffh		;3f66	ff		.
	defb 000h		;3f67	00		.
	defb 000h		;3f68	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RamInitTable: ten (dest,word) pairs written into
; workspace/screen RAM by the walker at $367D -
; window defaults at $DFFA-$DFFE, further cells
; at $E0AC-$E0B4 and two tags at $C063/$C064.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EncodedTable3E45_end:

; BLOCK 'RamInitTable' (start 0x3f69 end 0x3f91)
RamInitTable_start:
	defb 0fah		;3f69	fa		.
	defb 0dfh		;3f6a	df		.
	defb 008h		;3f6b	08		.
	defb 0deh		;3f6c	de		.
	defb 0fch		;3f6d	fc		.
	defb 0dfh		;3f6e	df		.
	defb 042h		;3f6f	42		B
	defb 0deh		;3f70	de		.
	defb 0feh		;3f71	fe		.
	defb 0dfh		;3f72	df		.
	defb 042h		;3f73	42		B
	defb 0deh		;3f74	de		.
	defb 0ach		;3f75	ac		.
	defb 0e0h		;3f76	e0		.
	defb 0a0h		;3f77	a0		.
	defb 0f8h		;3f78	f8		.
	defb 0aeh		;3f79	ae		.
	defb 0e0h		;3f7a	e0		.
	defb 0ech		;3f7b	ec		.
	defb 03ch		;3f7c	3c		<
	defb 0b0h		;3f7d	b0		.
	defb 0e0h		;3f7e	e0		.
	defb 046h		;3f7f	46		F
	defb 037h		;3f80	37		7
	defb 0b2h		;3f81	b2		.
	defb 0e0h		;3f82	e0		.
	defb 08dh		;3f83	8d		.
	defb 028h		;3f84	28		(
	defb 0b4h		;3f85	b4		.
	defb 0e0h		;3f86	e0		.
	defb 085h		;3f87	85		.
	defb 028h		;3f88	28		(
	defb 063h		;3f89	63		c
	defb 0c0h		;3f8a	c0		.
	defb 000h		;3f8b	00		.
	defb 000h		;3f8c	00		.
	defb 064h		;3f8d	64		d
	defb 0c0h		;3f8e	c0		.
	defb 000h		;3f8f	00		.
	defb 000h		;3f90	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ZeroPad3F91: two orphan one-character strings
; ('c'+$80, 'd'+$80) and zero padding up to the
; end of the 16 KiB page.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RamInitTable_end:

; BLOCK 'ZeroPad3F91' (start 0x3f91 end 0x4000)
ZeroPad3F91_start:
	defb 000h		;3f91	00		.
	defb 000h		;3f92	00		.
	defb 000h		;3f93	00		.
	defb 000h		;3f94	00		.
	defb 000h		;3f95	00		.
l3f96h:
	defb 000h		;3f96	00		.
	defb 000h		;3f97	00		.
	defb 000h		;3f98	00		.
	defb 000h		;3f99	00		.
	defb 000h		;3f9a	00		.
	defb 000h		;3f9b	00		.
	defb 000h		;3f9c	00		.
	defb 000h		;3f9d	00		.
	defb 000h		;3f9e	00		.
	defb 000h		;3f9f	00		.
	defb 000h		;3fa0	00		.
	defb 000h		;3fa1	00		.
	defb 000h		;3fa2	00		.
	defb 000h		;3fa3	00		.
	defb 000h		;3fa4	00		.
	defb 000h		;3fa5	00		.
	defb 000h		;3fa6	00		.
	defb 000h		;3fa7	00		.
	defb 000h		;3fa8	00		.
	defb 000h		;3fa9	00		.
	defb 000h		;3faa	00		.
	defb 000h		;3fab	00		.
	defb 000h		;3fac	00		.
	defb 000h		;3fad	00		.
	defb 000h		;3fae	00		.
	defb 000h		;3faf	00		.
	defb 000h		;3fb0	00		.
	defb 000h		;3fb1	00		.
	defb 000h		;3fb2	00		.
	defb 000h		;3fb3	00		.
	defb 000h		;3fb4	00		.
	defb 000h		;3fb5	00		.
	defb 000h		;3fb6	00		.
	defb 000h		;3fb7	00		.
	defb 000h		;3fb8	00		.
	defb 000h		;3fb9	00		.
	defb 000h		;3fba	00		.
	defb 000h		;3fbb	00		.
	defb 000h		;3fbc	00		.
	defb 000h		;3fbd	00		.
	defb 000h		;3fbe	00		.
	defb 000h		;3fbf	00		.
	defb 000h		;3fc0	00		.
	defb 000h		;3fc1	00		.
	defb 000h		;3fc2	00		.
	defb 000h		;3fc3	00		.
	defb 000h		;3fc4	00		.
	defb 000h		;3fc5	00		.
	defb 000h		;3fc6	00		.
	defb 000h		;3fc7	00		.
	defb 000h		;3fc8	00		.
	defb 000h		;3fc9	00		.
	defb 000h		;3fca	00		.
	defb 000h		;3fcb	00		.
	defb 000h		;3fcc	00		.
	defb 000h		;3fcd	00		.
	defb 000h		;3fce	00		.
	defb 000h		;3fcf	00		.
	defb 000h		;3fd0	00		.
	defb 000h		;3fd1	00		.
	defb 000h		;3fd2	00		.
	defb 000h		;3fd3	00		.
	defb 000h		;3fd4	00		.
	defb 000h		;3fd5	00		.
	defb 000h		;3fd6	00		.
	defb 000h		;3fd7	00		.
	defb 000h		;3fd8	00		.
	defb 000h		;3fd9	00		.
	defb 000h		;3fda	00		.
	defb 000h		;3fdb	00		.
	defb 000h		;3fdc	00		.
	defb 000h		;3fdd	00		.
	defb 000h		;3fde	00		.
	defb 000h		;3fdf	00		.
	defb 000h		;3fe0	00		.
	defb 000h		;3fe1	00		.
	defb 000h		;3fe2	00		.
	defb 000h		;3fe3	00		.
	defb 000h		;3fe4	00		.
	defb 000h		;3fe5	00		.
	defb 000h		;3fe6	00		.
	defb 000h		;3fe7	00		.
	defb 000h		;3fe8	00		.
	defb 000h		;3fe9	00		.
	defb 000h		;3fea	00		.
	defb 000h		;3feb	00		.
	defb 000h		;3fec	00		.
	defb 000h		;3fed	00		.
	defb 000h		;3fee	00		.
	defb 000h		;3fef	00		.
	defb 000h		;3ff0	00		.
	defb 000h		;3ff1	00		.
	defb 000h		;3ff2	00		.
	defb 000h		;3ff3	00		.
	defb 000h		;3ff4	00		.
	defb 000h		;3ff5	00		.
	defb 000h		;3ff6	00		.
	defb 000h		;3ff7	00		.
	defb 000h		;3ff8	00		.
	defb 000h		;3ff9	00		.
	defb 000h		;3ffa	00		.
	defb 000h		;3ffb	00		.
	defb 000h		;3ffc	00		.
	defb 000h		;3ffd	00		.
	defb 000h		;3ffe	00		.
l3fffh:
	defb 000h		;3fff	00		.
ZeroPad3F91_end:
