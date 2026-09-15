; ============================================================================
;  Scorpion ZS-256 Turbo - SERVICE MONITOR  (page 2 of scorp295.rom)
; ============================================================================
;
;  Source   : data/rom/scorp295.rom, bytes $8000-$BFFF (page 2), 16 KiB
;  Family   : Scorpion service monitor, v2.95
;  Sisters  : scorpion.rom       p2 = v2.x   (base monitor - the labels here
;             were derived from it by byte-run alignment),
;             scorp_prof401.rom p2 = v4.01 (ProfRAM layout, RAM hooks).
;
;  This file: v2.95 relocates most routines above $0A54; the low core
;             is shared with the base monitor byte for byte (only the
;             RST 10h vector target differs).  TR-DOS boot is inlined
;             and the error tables sit at $3C48/$39E1.
;  Method   : z80dasm 1.2.0, code/data block map from a dedicated
;             Z80 length-decoder + reachability analysis; labels and
;             comments added by hand after studying the three listings.
;  Style    : educational, after Logan & O'Hara, "The Complete Spectrum
;             ROM Disassembly" (see docs/rom for the inspiration).
;
; ----------------------------------------------------------------------------
;  THE ROM BUNDLE
; ----------------------------------------------------------------------------
;  scorp295.rom is a 64 KiB bundle of four 16 KiB pages.  Hash comparison
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
ZeroPad3FBE_end:	equ 0x4000
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
; RST 08h - report the error whose code is in B (ReportError $1345).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst08Vector:
	jp ReportError		;0008	c3 45 13	. E .
l000bh:
	out (c),a		;000b	ed 79		. y
l000dh:
	jp WarmEntry		;000d	c3 9c 00	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 10h - print the character in A (PrintChar $0C94).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst10Vector:
	jp PrintChar		;0010	c3 94 0c	. . .
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
; RST 20h - print inline message (Rst20Handler $1D87): characters
; follow the call site, the last one carries bit 7 set.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst20Vector:
	jp Rst20Handler		;0020	c3 87 1d	. . .
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
; RST 30h - set the IX work buffer from HL (SetWorkspace $179D);
; callers wanting the default buffer enter at $179A instead.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Filler002D_end:
Rst30Vector:
	jp SetWorkspace		;0030	c3 9d 17	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Filler0033: padding after the RST 30 stub
; (nop nop jp $00B6), unreachable.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Filler0033' (start 0x0033 end 0x0038)
Filler0033_start:
	defb 000h		;0033	00		.
	defb 000h		;0034	00		.
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
l0040h:
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
	call BootContinue	;0057	cd 8b 13	. . .
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
	call sub_35f6h		;007f	cd f6 35	. . 5
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
	ld bc,l1ffdh		;00b3	01 fd 1f	. . .
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
	call CheckWatchpoints	;00e2	cd ad 29	. . )
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
	defb 012h		;00f1	12		.
	defb 0b6h		;00f2	b6		.
	defb 0e0h		;00f3	e0		.
	defb 053h		;00f4	53		S
	defb 000h		;00f5	00		.
l00f6h:
	defb 015h		;00f6	15		.
	defb 09ch		;00f7	9c		.
	defb 015h		;00f8	15		.
	defb 00bh		;00f9	0b		.
	defb 0d7h		;00fa	d7		.
l00fbh:
	defb 001h		;00fb	01		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; XorKeys: private key bytes for DecodeXorTable ($2FC7); several
; ROM tables are stored XOR-masked and are unmasked into RAM at boot.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Filler00F1_end:
XorDecodeKeys:

; BLOCK 'XorKeys' (start 0x00fc end 0x0100)
XorKeys_start:
	defb 00dh		;00fc	0d		.
l00fdh:
	defb 003h		;00fd	03		.
l00feh:
	defb 060h		;00fe	60		`
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
	jp z,l15b5h		;010c	ca b5 15	. . .
	ld a,(0c063h)		;010f	3a 63 c0	: c .
	or a			;0112	b7		.
	jp nz,l15b5h		;0113	c2 b5 15	. . .
	jp l0e0ah		;0116	c3 0a 0e	. . .
l0119h:
	call sub_02bch		;0119	cd bc 02	. . .
l011ch:
	ld sp,MonitorStack	;011c	31 b5 e2	1 . .
	call sub_04cch		;011f	cd cc 04	. . .
l0122h:
	call sub_29b3h		;0122	cd b3 29	. . )
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
	call FindWatchpoint	;016e	cd 16 2a	. . *
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
	call LookupCommand	;01a2	cd 9b 29	. . )
	push af			;01a5	f5		.
	call nz,0eb06h		;01a6	c4 06 eb	. . .
	pop af			;01a9	f1		.
l01aah:
	ld l,a			;01aa	6f		o
	ld a,083h		;01ab	3e 83		> .
	ld h,000h		;01ad	26 00		& .
	ld (0dda7h),hl		;01af	22 a7 dd	" . .
	call sub_02c2h		;01b2	cd c2 02	. . .
	jp ExitToError		;01b5	c3 1e 0e	. . .
l01b8h:
	bit 7,e			;01b8	cb 7b		. {
	jp nz,l17ceh		;01ba	c2 ce 17	. . .
	ld sp,0e25eh		;01bd	31 5e e2	1 ^ .
	res 7,(iy+016h)		;01c0	fd cb 16 be	. . . .
	call BuildStepStub	;01c4	cd aa 1e	. . .
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
	call sub_3577h		;01d9	cd 77 35	. w 5
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
	jp l1f91h		;01f6	c3 91 1f	. . .
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
	ld a,080h		;0221	3e 80		> .
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
	ret			;023d	c9		.
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
	jp PrintChar		;02ac	c3 94 0c	. . .
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
	call nz,KeyClick	;02cd	c4 27 0d	. ' .
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
; ($12/$10) re-asserted.  v2.95 (this ROM) additionally beeps here.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SaveContext:
	ld bc,l1ffdh		;0326	01 fd 1f	. . .
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
	ld bc,l1ffdh		;048c	01 fd 1f	. . .
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
	ld hl,ZeroPad3FBE_end	;04b6	21 00 40	! . @
	ld de,MonitorScreen	;04b9	11 69 c0	. i .
	jr nc,l04bfh		;04bc	30 01		0 .
	ex de,hl		;04be	eb		.
l04bfh:
	ld bc,PadColumns+1	;04bf	01 00 1b	. . .
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
	ld bc,l1ffdh		;0568	01 fd 1f	. . .
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
	ld bc,ErrorShortStrings_end	;0595	01 96 3f	. . ?
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
	ld hl,l3e72h		;05a2	21 72 3e	! r >
	ld de,DecodedTables	;05a5	11 2d e8	. - .
	call DecodeXorTable	;05a8	cd c7 2f	. . /
	ld ix,(BootConfigTable)	;05ab	dd 2a e0 f5	. * . .
	ld hl,0dff7h		;05af	21 f7 df	! . .
	ld (hl),001h		;05b2	36 01		6 .
	ld hl,HwConfigTable	;05b4	21 28 e9	! ( .
l05b7h:
	ld (hl),b		;05b7	70		p
	inc hl			;05b8	23		#
	djnz l05b7h		;05b9	10 fc		. .
	ld hl,MenuBuilder_end	;05bb	21 bd 36	! . 6
	ld de,0e2dbh		;05be	11 db e2	. . .
	call DecodeXorTable	;05c1	cd c7 2f	. . /
	jp (ix)			;05c4	dd e9		. .
	or (hl)			;05c6	b6		.
	call p,07621h		;05c7	f4 21 76	. ! v
	ccf			;05ca	3f		?
	ld de,0ffc0h		;05cb	11 c0 ff	. . .
	ld bc,Rst20Vector	;05ce	01 20 00	.   .
	ldir			;05d1	ed b0		. .
	rst 18h			;05d3	df		.
	ld bc,0cd32h		;05d4	01 32 cd	. 2 .
	xor d			;05d7	aa		.
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
	call PrintHexWord	;05fa	cd 95 1d	. . .
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
	call PrintDecimal	;060b	cd 0f 1d	. . .
	rst 20h			;060e	e7		.
	xor l			;060f	ad		.
	pop de			;0610	d1		.
	ld a,(de)		;0611	1a		.
	ld b,002h		;0612	06 02		. .
	call PrintDecimal	;0614	cd 0f 1d	. . .
	ld c,002h		;0617	0e 02		. .
	call sub_0633h		;0619	cd 33 06	. 3 .
	rst 20h			;061c	e7		.
	sub 0e7h		;061d	d6 e7		. .
	ld (0e7aeh),a		;061f	32 ae e7	2 . .
	add hl,sp		;0622	39		9
	or l			;0623	b5		.
	ld hl,(0e2d9h)		;0624	2a d9 e2	* . .
	dec hl			;0627	2b		+
	set 7,(hl)		;0628	cb fe		. .
	res 6,(iy+009h)		;062a	fd cb 09 b6	. . . .
	ld hl,ChecksumLoop	;062e	21 86 28	! . (
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
	ld bc,ZeroPad3FBE_end	;065a	01 00 40	. . @
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
	ld iy,l1010h		;0707	fd 21 10 10	. ! . .
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
	ld hl,01210h		;0739	21 10 12	! . .
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
	defb 02eh		;07c0	2e		.
	defb 00ch		;07c1	0c		.
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
	add a,h			;07eb	84		.
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
	ld bc,l1ffdh		;08d3	01 fd 1f	. . .
	ld a,01ah		;08d6	3e 1a		> .
	out (c),a		;08d8	ed 79		. y
	pop bc			;08da	c1		.
l08dbh:
	call sub_093ch		;08db	cd 3c 09	. < .
	push bc			;08de	c5		.
	ld a,c			;08df	79		y
	ld bc,l1ffdh		;08e0	01 fd 1f	. . .
	and 008h		;08e3	e6 08		. .
	or 012h			;08e5	f6 12		. .
	out (c),a		;08e7	ed 79		. y
	pop bc			;08e9	c1		.
	rrc c			;08ea	cb 09		. .
	djnz l08dbh		;08ec	10 ed		. .
	call sub_093ch		;08ee	cd 3c 09	. < .
	ld bc,l1ffdh		;08f1	01 fd 1f	. . .
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
	ld bc,l1ffdh		;0920	01 fd 1f	. . .
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
	jp l1c76h		;09d1	c3 76 1c	. v .
	ld bc,00702h		;09d4	01 02 07	. . .
	ld a,(bc)		;09d7	0a		.
	inc c			;09d8	0c		.
	dec c			;09d9	0d		.
	dec de			;09da	1b		.
l09dbh:
	ld e,077h		;09db	1e 77		. w
	dec c			;09dd	0d		.
	sub a			;09de	97		.
	dec c			;09df	0d		.
	daa			;09e0	27		'
	dec c			;09e1	0d		.
	call m,0310ch		;09e2	fc 0c 31	. . 1
	inc c			;09e5	0c		.
	defb 0edh ;next byte illegal after ed	;09e6	ed		.
	inc c			;09e7	0c		.
	ld c,c			;09e8	49		I
	dec c			;09e9	0d		.
	xor 00bh		;09ea	ee 0b		. .
	push hl			;09ec	e5		.
	push de			;09ed	d5		.
	push bc			;09ee	c5		.
	ld hl,PopAllRet		;09ef	21 89 0c	! . .
	push hl			;09f2	e5		.
	jr GlyphAddr		;09f3	18 03		. .
sub_09f5h:
	call KeyScanCode0A19_end	;09f5	cd 87 0a	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; GlyphAddr: address of the 6x8 glyph for A in Font6x8 ($FCA0),
; 8 bytes per character.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
GlyphAddr:
	sub 020h		;09f8	d6 20		.  
	ld l,a			;09fa	6f		o
	ld c,a			;09fb	4f		O
	ld h,000h		;09fc	26 00		& .
	add hl,hl		;09fe	29		)
	add hl,hl		;09ff	29		)
l0a00h:
	add hl,hl		;0a00	29		)
	ld de,Font6x8		;0a01	11 a0 fc	. . .
	add hl,de		;0a04	19		.
	ld a,(ix+00ah)		;0a05	dd 7e 0a	. ~ .
	cp 008h			;0a08	fe 08		. .
	jr nz,l0a40h		;0a0a	20 34		  4
	push hl			;0a0c	e5		.
	call BitmapAddr		;0a0d	cd 4f 0b	. O .
	pop de			;0a10	d1		.
	ld b,008h		;0a11	06 08		. .
l0a13h:
	ld a,(de)		;0a13	1a		.
	ld (hl),a		;0a14	77		w
	inc de			;0a15	13		.
	inc h			;0a16	24		$
	djnz l0a13h		;0a17	10 fa		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyScanCode0A19: keyboard-state helper code
; between GlyphAddr ($09F8) and SaveCursorCell
; ($0A87) - bit tests on the IX/IY key cells and
; the scan decode; an earlier sweep had mistyped
; it as data.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KeyScanCode0A19' (start 0x0a19 end 0x0a87)
KeyScanCode0A19_start:
	bit 1,(ix+007h)		;0a19	dd cb 07 4e	. . . N
	jr z,l0a2dh		;0a1d	28 0e		( .
	dec h			;0a1f	25		%
	ld a,h			;0a20	7c		|
	rrca			;0a21	0f		.
	rrca			;0a22	0f		.
	rrca			;0a23	0f		.
	and 003h		;0a24	e6 03		. .
	xor 058h		;0a26	ee 58		. X
	ld h,a			;0a28	67		g
	ld a,(ix+006h)		;0a29	dd 7e 06	. ~ .
	ld (hl),a		;0a2c	77		w
l0a2dh:
	bit 1,(iy+009h)		;0a2d	fd cb 09 4e	. . . N
	ret nz			;0a31	c0		.
	inc (ix+001h)		;0a32	dd 34 01	. 4 .
	ld a,(ix+00bh)		;0a35	dd 7e 0b	. ~ .
	cp (ix+001h)		;0a38	dd be 01	. . .
	ret nc			;0a3b	d0		.
	dec (ix+001h)		;0a3c	dd 35 01	. 5 .
	ret			;0a3f	c9		.
l0a40h:
	push hl			;0a40	e5		.
	call sub_0afeh		;0a41	cd fe 0a	. . .
	inc h			;0a44	24		$
	ld de,l03ffh		;0a45	11 ff 03	. . .
	ld a,(0e2d2h)		;0a48	3a d2 e2	: . .
	or a			;0a4b	b7		.
	jr z,l0a56h		;0a4c	28 08		( .
	ld b,a			;0a4e	47		G
l0a4fh:
	scf			;0a4f	37		7
	rr d			;0a50	cb 1a		. .
	rr e			;0a52	cb 1b		. .
	djnz l0a4fh		;0a54	10 f9		. .
l0a56h:
	ld (0e2bfh),de		;0a56	ed 53 bf e2	. S . .
	ld a,007h		;0a5a	3e 07		> .
l0a5ch:
	ex af,af'		;0a5c	08		.
	ex (sp),hl		;0a5d	e3		.
	inc hl			;0a5e	23		#
	ld d,(hl)		;0a5f	56		V
	ex (sp),hl		;0a60	e3		.
	ld e,000h		;0a61	1e 00		. .
	ld a,(0e2d2h)		;0a63	3a d2 e2	: . .
	or a			;0a66	b7		.
	jr z,l0a70h		;0a67	28 07		( .
	ld b,a			;0a69	47		G
l0a6ah:
	srl d			;0a6a	cb 3a		. :
	rr e			;0a6c	cb 1b		. .
	djnz l0a6ah		;0a6e	10 fa		. .
l0a70h:
	ld bc,(0e2bfh)		;0a70	ed 4b bf e2	. K . .
	ld a,(hl)		;0a74	7e		~
	and b			;0a75	a0		.
	or d			;0a76	b2		.
	ld (hl),a		;0a77	77		w
	inc hl			;0a78	23		#
	ld a,(hl)		;0a79	7e		~
	and c			;0a7a	a1		.
	or e			;0a7b	b3		.
	ld (hl),a		;0a7c	77		w
	dec hl			;0a7d	2b		+
	inc h			;0a7e	24		$
	ex af,af'		;0a7f	08		.
	dec a			;0a80	3d		=
	jr nz,l0a5ch		;0a81	20 d9		  .
	pop de			;0a83	d1		.
	jp KeyScanCode0A19_start	;0a84	c3 19 0a	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SaveCursorCell (+restore): the cursor spans 2 glyphs of a
; 6-pixel cell; the 16 covered bytes are buffered at
; CursorCellSave (E2C1) and restored when the cursor moves.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
KeyScanCode0A19_end:
SaveCursorCell:
	set 2,(iy+009h)		;0a87	fd cb 09 d6	. . . .
	bit 0,(iy+009h)		;0a8b	fd cb 09 46	. . . F
	ret z			;0a8f	c8		.
	res 0,(iy+009h)		;0a90	fd cb 09 86	. . . .
l0a94h:
	push af			;0a94	f5		.
	call sub_0afeh		;0a95	cd fe 0a	. . .
	ld de,CursorCellSave	;0a98	11 c1 e2	. . .
	ld b,008h		;0a9b	06 08		. .
l0a9dh:
	ld c,002h		;0a9d	0e 02		. .
l0a9fh:
	bit 0,(iy+009h)		;0a9f	fd cb 09 46	. . . F
	jr z,l0aa9h		;0aa3	28 04		( .
	ld a,(hl)		;0aa5	7e		~
	ld (de),a		;0aa6	12		.
	jr l0aabh		;0aa7	18 02		. .
l0aa9h:
	ld a,(de)		;0aa9	1a		.
	ld (hl),a		;0aaa	77		w
l0aabh:
	inc l			;0aab	2c		,
	inc de			;0aac	13		.
	dec c			;0aad	0d		.
	jr nz,l0a9fh		;0aae	20 ef		  .
	dec l			;0ab0	2d		-
	dec l			;0ab1	2d		-
	inc h			;0ab2	24		$
	djnz l0a9dh		;0ab3	10 e8		. .
	pop af			;0ab5	f1		.
	ret			;0ab6	c9		.
RestartCursorBlink:
	ld a,001h		;0ab7	3e 01		> .
	res 2,(iy+009h)		;0ab9	fd cb 09 96	. . . .
	bit 0,(iy+009h)		;0abd	fd cb 09 46	. . . F
	ret nz			;0ac1	c0		.
	ld (0e2d3h),a		;0ac2	32 d3 e2	2 . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BlinkCursor: toggle cursor visibility every $7D frames
; (counter at E2D3); draw/erase goes through the cell buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BlinkCursor:
	ld ix,(WorkBufferPtr)	;0ac5	dd 2a d4 e2	. * . .
	bit 0,(ix+007h)		;0ac9	dd cb 07 46	. . . F
	ret nz			;0acd	c0		.
	ld hl,0dfe8h		;0ace	21 e8 df	! . .
	bit 2,(hl)		;0ad1	cb 56		. V
	res 2,(hl)		;0ad3	cb 96		. .
	ld hl,0e2d3h		;0ad5	21 d3 e2	! . .
	jr nz,l0afbh		;0ad8	20 21		  !
	dec (hl)		;0ada	35		5
	ret nz			;0adb	c0		.
	ld (hl),07dh		;0adc	36 7d		6 }
	ld hl,0dfe8h		;0ade	21 e8 df	! . .
	bit 0,(hl)		;0ae1	cb 46		. F
	res 0,(hl)		;0ae3	cb 86		. .
	jr nz,l0a94h		;0ae5	20 ad		  .
	set 0,(hl)		;0ae7	cb c6		. .
	push hl			;0ae9	e5		.
	call l0a94h		;0aea	cd 94 0a	. . .
	pop hl			;0aed	e1		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WindowAddr: window cursor (IX+0 row, IX+1 column) ->
; absolute screen address.  The 6-pixel font packs two glyphs
; per 8-pixel byte cell, so columns address 6-pixel units.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WindowAddr:
	ld a,(ix+00ch)		;0aee	dd 7e 0c	. ~ .
	set 1,(hl)		;0af1	cb ce		. .
	push hl			;0af3	e5		.
	call GlyphAddr		;0af4	cd f8 09	. . .
	pop hl			;0af7	e1		.
	res 1,(hl)		;0af8	cb 8e		. .
	ret			;0afa	c9		.
l0afbh:
	ld (hl),00ah		;0afb	36 0a		6 .
	ret			;0afd	c9		.
sub_0afeh:
	ld a,(ix+002h)		;0afe	dd 7e 02	. ~ .
	add a,(ix+000h)		;0b01	dd 86 00	. . .
	ld h,a			;0b04	67		g
	rrca			;0b05	0f		.
	rrca			;0b06	0f		.
	rrca			;0b07	0f		.
	and 0e0h		;0b08	e6 e0		. .
	ld c,a			;0b0a	4f		O
	ld l,(ix+001h)		;0b0b	dd 6e 01	. n .
	ld a,l			;0b0e	7d		}
	add a,l			;0b0f	85		.
	add a,l			;0b10	85		.
	add a,l			;0b11	85		.
	add a,l			;0b12	85		.
	add a,l			;0b13	85		.
	ld l,a			;0b14	6f		o
	and 007h		;0b15	e6 07		. .
l0b17h:
	ld (0e2d2h),a		;0b17	32 d2 e2	2 . .
	ld a,l			;0b1a	7d		}
	rrca			;0b1b	0f		.
	rrca			;0b1c	0f		.
	rrca			;0b1d	0f		.
	add a,(ix+003h)		;0b1e	dd 86 03	. . .
	and 01fh		;0b21	e6 1f		. .
	or c			;0b23	b1		.
	ld l,a			;0b24	6f		o
	ld a,h			;0b25	7c		|
	and 018h		;0b26	e6 18		. .
	xor 040h		;0b28	ee 40		. @
	ld h,a			;0b2a	67		g
	ret			;0b2b	c9		.
sub_0b2ch:
	ld l,(ix+000h)		;0b2c	dd 6e 00	. n .
	ld h,(ix+001h)		;0b2f	dd 66 01	. f .
	ld c,(ix+002h)		;0b32	dd 4e 02	. N .
	ld b,(ix+003h)		;0b35	dd 46 03	. F .
	add hl,bc		;0b38	09		.
	ld c,l			;0b39	4d		M
	ld b,h			;0b3a	44		D
	ret			;0b3b	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AttrAddr: attribute address for the cursor position.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AttrAddr:
	call sub_0b2ch		;0b3c	cd 2c 0b	. , .
	ld a,c			;0b3f	79		y
	rrca			;0b40	0f		.
	rrca			;0b41	0f		.
	rrca			;0b42	0f		.
	ld c,a			;0b43	4f		O
	and 0e0h		;0b44	e6 e0		. .
	xor b			;0b46	a8		.
	ld l,a			;0b47	6f		o
	ld a,c			;0b48	79		y
	and 003h		;0b49	e6 03		. .
	xor 058h		;0b4b	ee 58		. X
	ld h,a			;0b4d	67		g
	ret			;0b4e	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BitmapAddr: bitmap address for the cursor position.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BitmapAddr:
	call sub_0b2ch		;0b4f	cd 2c 0b	. , .
	ld a,c			;0b52	79		y
	rrca			;0b53	0f		.
	rrca			;0b54	0f		.
	rrca			;0b55	0f		.
	and 0e0h		;0b56	e6 e0		. .
	xor b			;0b58	a8		.
	ld l,a			;0b59	6f		o
	ld a,c			;0b5a	79		y
	and 018h		;0b5b	e6 18		. .
	xor 040h		;0b5d	ee 40		. @
	ld h,a			;0b5f	67		g
	ret			;0b60	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScrollUp: shift the window bitmap (and attributes, when the
; descriptor flag bit 1 is set) one row toward row 0, then blank
; the vacated bottom row via FillWholeRow.  Called by WindowNewline
; when the cursor sits on the last row - the classic text-scroll.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScrollUp:
	ld c,(ix+000h)		;0b61	dd 4e 00	. N .
	ld b,(ix+001h)		;0b64	dd 46 01	. F .
	push bc			;0b67	c5		.
	ld (ix+000h),001h	;0b68	dd 36 00 01	. 6 . .
	ld (ix+001h),000h	;0b6c	dd 36 01 00	. 6 . .
	ld b,(ix+004h)		;0b70	dd 46 04	. F .
l0b73h:
	dec b			;0b73	05		.
	jr z,l0b91h		;0b74	28 1b		( .
	push bc			;0b76	c5		.
	call BitmapAddr		;0b77	cd 4f 0b	. O .
	or a			;0b7a	b7		.
	call CopyCharRow	;0b7b	cd a3 0b	. . .
	bit 1,(ix+007h)		;0b7e	dd cb 07 4e	. . . N
	jr z,l0b8bh		;0b82	28 07		( .
	call AttrAddr		;0b84	cd 3c 0b	. < .
	or a			;0b87	b7		.
	call CopyAttrRow	;0b88	cd ce 0b	. . .
l0b8bh:
	inc (ix+000h)		;0b8b	dd 34 00	. 4 .
	pop bc			;0b8e	c1		.
	jr l0b73h		;0b8f	18 e2		. .
l0b91h:
	ld a,(ix+004h)		;0b91	dd 7e 04	. ~ .
	dec a			;0b94	3d		=
	ld (ix+000h),a		;0b95	dd 77 00	. w .
	call sub_0c54h		;0b98	cd 54 0c	. T .
	pop bc			;0b9b	c1		.
	ld (ix+000h),c		;0b9c	dd 71 00	. q .
	ld (ix+001h),b		;0b9f	dd 70 01	. p .
	ret			;0ba2	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CopyCharRow: copy one 8-line character row between adjacent
; window rows (LDIR per line, H/D advanced within the third);
; carry selects the source row: +1 (into ScrollDown) or -1.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CopyCharRow:
	push hl			;0ba3	e5		.
	ld a,(ix+000h)		;0ba4	dd 7e 00	. ~ .
	push af			;0ba7	f5		.
	jr c,l0badh		;0ba8	38 03		8 .
	dec a			;0baa	3d		=
	jr l0baeh		;0bab	18 01		. .
l0badh:
	inc a			;0bad	3c		<
l0baeh:
	ld (ix+000h),a		;0bae	dd 77 00	. w .
	call BitmapAddr		;0bb1	cd 4f 0b	. O .
	pop af			;0bb4	f1		.
	ld (ix+000h),a		;0bb5	dd 77 00	. w .
	ld e,l			;0bb8	5d		]
	ld d,h			;0bb9	54		T
	pop hl			;0bba	e1		.
	ld a,008h		;0bbb	3e 08		> .
l0bbdh:
	ld b,000h		;0bbd	06 00		. .
	ld c,(ix+005h)		;0bbf	dd 4e 05	. N .
	push hl			;0bc2	e5		.
	push de			;0bc3	d5		.
	ldir			;0bc4	ed b0		. .
	pop de			;0bc6	d1		.
	pop hl			;0bc7	e1		.
	inc h			;0bc8	24		$
	inc d			;0bc9	14		.
	dec a			;0bca	3d		=
	jr nz,l0bbdh		;0bcb	20 f0		  .
	ret			;0bcd	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CopyAttrRow: the attribute-row twin of CopyCharRow - one LDIR
; of the window width, source row selected by carry.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CopyAttrRow:
	push hl			;0bce	e5		.
	ld a,(ix+000h)		;0bcf	dd 7e 00	. ~ .
	push af			;0bd2	f5		.
	jr c,l0bd8h		;0bd3	38 03		8 .
	dec a			;0bd5	3d		=
	jr l0bd9h		;0bd6	18 01		. .
l0bd8h:
	inc a			;0bd8	3c		<
l0bd9h:
	ld (ix+000h),a		;0bd9	dd 77 00	. w .
	call AttrAddr		;0bdc	cd 3c 0b	. < .
	pop af			;0bdf	f1		.
	ld (ix+000h),a		;0be0	dd 77 00	. w .
	ld e,l			;0be3	5d		]
	ld d,h			;0be4	54		T
	pop hl			;0be5	e1		.
	ld b,000h		;0be6	06 00		. .
	ld c,(ix+005h)		;0be8	dd 4e 05	. N .
	ldir			;0beb	ed b0		. .
	ret			;0bed	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScrollDown: reverse scroll - rows shift away from row 0 (the
; helpers are called with carry set), the vacated top row is
; blanked from column 0.  Used by the disassembly scroll-back
; (the caller sits just before ArmWatchpoint).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScrollDown:
	ld c,(ix+000h)		;0bee	dd 4e 00	. N .
	ld b,(ix+001h)		;0bf1	dd 46 01	. F .
	push bc			;0bf4	c5		.
	ld (ix+001h),000h	;0bf5	dd 36 01 00	. 6 . .
	ld b,(ix+004h)		;0bf9	dd 46 04	. F .
	dec b			;0bfc	05		.
	jr z,l0c1fh		;0bfd	28 20		(  
	dec b			;0bff	05		.
	ld (ix+000h),b		;0c00	dd 70 00	. p .
	inc b			;0c03	04		.
l0c04h:
	push bc			;0c04	c5		.
	call BitmapAddr		;0c05	cd 4f 0b	. O .
	scf			;0c08	37		7
	call CopyCharRow	;0c09	cd a3 0b	. . .
	bit 1,(ix+007h)		;0c0c	dd cb 07 4e	. . . N
	jr z,l0c19h		;0c10	28 07		( .
	call AttrAddr		;0c12	cd 3c 0b	. < .
	scf			;0c15	37		7
	call CopyAttrRow	;0c16	cd ce 0b	. . .
l0c19h:
	dec (ix+000h)		;0c19	dd 35 00	. 5 .
	pop bc			;0c1c	c1		.
	djnz l0c04h		;0c1d	10 e5		. .
l0c1fh:
	ld (ix+000h),000h	;0c1f	dd 36 00 00	. 6 . .
	call sub_0c54h		;0c23	cd 54 0c	. T .
	pop bc			;0c26	c1		.
	ld (ix+000h),c		;0c27	dd 71 00	. q .
	ld (ix+001h),b		;0c2a	dd 70 01	. p .
	ret			;0c2d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FillWindow: blank every row of the window (FillRowToEnd per
; row, attributes from the descriptor), home the cursor to row 0
; and restart the blink timer ($0AB7).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FillWindow:
	call sub_02afh		;0c2e	cd af 02	. . .
sub_0c31h:
	xor a			;0c31	af		.
	ld (ix+000h),a		;0c32	dd 77 00	. w .
	ld (ix+001h),a		;0c35	dd 77 01	. w .
	ld b,(ix+004h)		;0c38	dd 46 04	. F .
l0c3bh:
	push bc			;0c3b	c5		.
	call FillRowToEnd	;0c3c	cd 58 0c	. X .
	pop bc			;0c3f	c1		.
	inc (ix+000h)		;0c40	dd 34 00	. 4 .
	djnz l0c3bh		;0c43	10 f6		. .
	ld (ix+000h),000h	;0c45	dd 36 00 00	. 6 . .
	call RestartCursorBlink	;0c49	cd b7 0a	. . .
	ret			;0c4c	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FillWholeRow: save the cursor cell, park X at 0 and fall into
; FillRowToEnd - i.e. blank the entire current row.  The scroll
; routines enter at $267E (one instruction further) to keep the
; cursor flag untouched.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FillWholeRow:
	call KeyScanCode0A19_end	;0c4d	cd 87 0a	. . .
	ld (ix+001h),000h	;0c50	dd 36 01 00	. 6 . .
sub_0c54h:
	res 3,(iy+009h)		;0c54	fd cb 09 9e	. . . .
FillRowToEnd:
	call KeyScanCode0A19_end	;0c58	cd 87 0a	. . .
	call BitmapAddr		;0c5b	cd 4f 0b	. O .
	ld a,(ix+005h)		;0c5e	dd 7e 05	. ~ .
	sub (ix+001h)		;0c61	dd 96 01	. . .
	ld c,008h		;0c64	0e 08		. .
	ld d,a			;0c66	57		W
	ld e,l			;0c67	5d		]
l0c68h:
	ld b,d			;0c68	42		B
	ld l,e			;0c69	6b		k
l0c6ah:
	ld (hl),000h		;0c6a	36 00		6 .
	inc l			;0c6c	2c		,
	djnz l0c6ah		;0c6d	10 fb		. .
	inc h			;0c6f	24		$
	dec c			;0c70	0d		.
	jr nz,l0c68h		;0c71	20 f5		  .
	push de			;0c73	d5		.
	call AttrAddr		;0c74	cd 3c 0b	. < .
	pop bc			;0c77	c1		.
	ld d,(ix+006h)		;0c78	dd 56 06	. V .
l0c7bh:
	ld (hl),d		;0c7b	72		r
	inc hl			;0c7c	23		#
	djnz l0c7bh		;0c7d	10 fc		. .
	ret			;0c7f	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrinterPutChar: buffer a character for the printer/parallel
; port (buffer pointer at E2D9).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrinterPutChar:
	ld hl,(0e2d9h)		;0c80	2a d9 e2	* . .
	ld (hl),a		;0c83	77		w
	inc hl			;0c84	23		#
	ld (0e2d9h),hl		;0c85	22 d9 e2	" . .
	ret			;0c88	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PopAllRet: pop every register and return - the standard exit
; of window handlers.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PopAllRet:
	pop bc			;0c89	c1		.
	pop de			;0c8a	d1		.
	pop hl			;0c8b	e1		.
	ret			;0c8c	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintCharBit7: print A with bit 7 stripped (terminator-safe).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintCharBit7:
	push af			;0c8d	f5		.
	and 07fh		;0c8e	e6 7f		. .
	jr l0c95h		;0c90	18 03		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintSpace.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintSpace:
	ld a,020h		;0c92	3e 20		>  
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintChar - the RST 10h backend and the single character
; sink of the monitor; everything funnels into
; DispatchOutput next.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintChar:
	push af			;0c94	f5		.
l0c95h:
	call DispatchOutput	;0c95	cd 9a 0c	. . .
	pop af			;0c98	f1		.
	ret			;0c99	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DispatchOutput: the output switchboard - DFE8 bits 4/5/6
; route each character to tape ($084D), printer or the screen
; window; installed print hooks watch the stream as well.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DispatchOutput:
	push hl			;0c9a	e5		.
	push de			;0c9b	d5		.
	push bc			;0c9c	c5		.
	ld hl,PopAllRet		;0c9d	21 89 0c	! . .
	push hl			;0ca0	e5		.
	ld hl,0dfe8h		;0ca1	21 e8 df	! . .
	bit 6,(hl)		;0ca4	cb 76		. v
	jr nz,PrinterPutChar	;0ca6	20 d8		  .
	bit 5,(ix+007h)		;0ca8	dd cb 07 6e	. . . n
	ret nz			;0cac	c0		.
	bit 4,(hl)		;0cad	cb 66		. f
	jp nz,FeedTapeOutput	;0caf	c2 4d 08	. M .
	bit 5,(hl)		;0cb2	cb 6e		. n
	jr nz,l0cddh		;0cb4	20 27		  '
	cp 020h			;0cb6	fe 20		.  
	jr c,l0cc8h		;0cb8	38 0e		8 .
	call sub_09f5h		;0cba	cd f5 09	. . .
	ret nz			;0cbd	c0		.
	bit 4,(ix+007h)		;0cbe	dd cb 07 66	. . . f
	ret z			;0cc2	c8		.
	call WindowHome		;0cc3	cd f4 0c	. . .
	jr WindowNewline	;0cc6	18 3b		. ;
l0cc8h:
	ld hl,l09dbh		;0cc8	21 db 09	! . .
	ld bc,Rst08Vector	;0ccb	01 08 00	. . .
	ld e,l			;0cce	5d		]
	ld d,h			;0ccf	54		T
	cpdr			;0cd0	ed b9		. .
	ret nz			;0cd2	c0		.
	inc de			;0cd3	13		.
	ex de,hl		;0cd4	eb		.
	sla c			;0cd5	cb 21		. !
	add hl,bc		;0cd7	09		.
	ld e,(hl)		;0cd8	5e		^
	inc hl			;0cd9	23		#
	ld d,(hl)		;0cda	56		V
	ex de,hl		;0cdb	eb		.
	jp (hl)			;0cdc	e9		.
l0cddh:
	res 5,(iy+009h)		;0cdd	fd cb 09 ae	. . . .
	ld hl,(0e2d6h)		;0ce1	2a d6 e2	* . .
	jp (hl)			;0ce4	e9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; InstallPrintHook: park a user routine at the E2D6 vector;
; every printed character passes through it.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
InstallPrintHook:
	ld (0e2d6h),hl		;0ce5	22 d6 e2	" . .
	set 5,(iy+009h)		;0ce8	fd cb 09 ee	. . . .
	ret			;0cec	c9		.
	bit 2,(ix+007h)		;0ced	dd cb 07 56	. . . V
	call nz,WindowNewline	;0cf1	c4 03 0d	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WindowHome: cursor to row/column 0 of the current window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WindowHome:
	call KeyScanCode0A19_end	;0cf4	cd 87 0a	. . .
	xor a			;0cf7	af		.
	ld (ix+001h),a		;0cf8	dd 77 01	. w .
	ret			;0cfb	c9		.
	bit 3,(ix+007h)		;0cfc	dd cb 07 5e	. . . ^
	call nz,WindowHome	;0d00	c4 f4 0c	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WindowNewline: advance to the next row (scroll at the bottom
; edge).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WindowNewline:
	call sub_0d0dh		;0d03	cd 0d 0d	. . .
	ret nz			;0d06	c0		.
	call KeyScanCode0A19_end	;0d07	cd 87 0a	. . .
	jp ScrollUp		;0d0a	c3 61 0b	. a .
sub_0d0dh:
	ld a,(ix+004h)		;0d0d	dd 7e 04	. ~ .
	dec a			;0d10	3d		=
	cp (ix+000h)		;0d11	dd be 00	. . .
	ret z			;0d14	c8		.
	call KeyScanCode0A19_end	;0d15	cd 87 0a	. . .
	inc (ix+000h)		;0d18	dd 34 00	. 4 .
	ret			;0d1b	c9		.
sub_0d1ch:
	call KeyScanCode0A19_end	;0d1c	cd 87 0a	. . .
	xor a			;0d1f	af		.
	ld (ix+001h),a		;0d20	dd 77 01	. w .
	ld (ix+000h),a		;0d23	dd 77 00	. w .
	ret			;0d26	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyClick: brief speaker toggle (OUT $FE, bit 4 XOR) per
; keypress.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
KeyClick:
	push de			;0d27	d5		.
	ld de,07060h		;0d28	11 60 70	. ` p
	push bc			;0d2b	c5		.
	push af			;0d2c	f5		.
	ld a,(0e005h)		;0d2d	3a 05 e0	: . .
	push af			;0d30	f5		.
l0d31h:
	out (0feh),a		;0d31	d3 fe		. .
	xor 010h		;0d33	ee 10		. .
	push af			;0d35	f5		.
	ld a,r			;0d36	ed 5f		. _
	and 001h		;0d38	e6 01		. .
	add a,e			;0d3a	83		.
	ld b,a			;0d3b	47		G
l0d3ch:
	djnz l0d3ch		;0d3c	10 fe		. .
	pop af			;0d3e	f1		.
	dec d			;0d3f	15		.
	jr nz,l0d31h		;0d40	20 ef		  .
	pop af			;0d42	f1		.
l0d43h:
	out (0feh),a		;0d43	d3 fe		. .
	pop af			;0d45	f1		.
	pop bc			;0d46	c1		.
	pop de			;0d47	d1		.
	ret			;0d48	c9		.
	ld hl,l0d4fh		;0d49	21 4f 0d	! O .
	jp InstallPrintHook	;0d4c	c3 e5 0c	. . .
l0d4fh:
	ld c,(ix+000h)		;0d4f	dd 4e 00	. N .
	cp (ix+004h)		;0d52	dd be 04	. . .
	jr nc,l0d58h		;0d55	30 01		0 .
	ld c,a			;0d57	4f		O
l0d58h:
	ld a,c			;0d58	79		y
	ld (0e2d8h),a		;0d59	32 d8 e2	2 . .
	ld hl,l0d62h		;0d5c	21 62 0d	! b .
	jp InstallPrintHook	;0d5f	c3 e5 0c	. . .
l0d62h:
	call KeyScanCode0A19_end	;0d62	cd 87 0a	. . .
	ld c,(ix+00bh)		;0d65	dd 4e 0b	. N .
	dec c			;0d68	0d		.
	cp c			;0d69	b9		.
	jr nc,l0d6dh		;0d6a	30 01		0 .
	ld c,a			;0d6c	4f		O
l0d6dh:
	ld (ix+001h),c		;0d6d	dd 71 01	. q .
	ld a,(0e2d8h)		;0d70	3a d8 e2	: . .
	ld (ix+000h),a		;0d73	dd 77 00	. w .
	ret			;0d76	c9		.
	ld a,(ix+006h)		;0d77	dd 7e 06	. ~ .
	ld (0e2d1h),a		;0d7a	32 d1 e2	2 . .
	ld hl,l0d83h		;0d7d	21 83 0d	! . .
	jp InstallPrintHook	;0d80	c3 e5 0c	. . .
l0d83h:
	or (ix+006h)		;0d83	dd b6 06	. . .
	ld (ix+006h),a		;0d86	dd 77 06	. w .
	ld hl,l0d8fh		;0d89	21 8f 0d	! . .
	jp InstallPrintHook	;0d8c	c3 e5 0c	. . .
l0d8fh:
	cpl			;0d8f	2f		/
	and (ix+006h)		;0d90	dd a6 06	. . .
	ld (ix+006h),a		;0d93	dd 77 06	. w .
	ret			;0d96	c9		.
	ld a,(0e2d1h)		;0d97	3a d1 e2	: . .
	ld (ix+006h),a		;0d9a	dd 77 06	. w .
	ret			;0d9d	c9		.
	res 0,(iy+012h)		;0d9e	fd cb 12 86	. . . .
	ld sp,0e25eh		;0da2	31 5e e2	1 ^ .
	jp l15b5h		;0da5	c3 b5 15	. . .
sub_0da8h:
	ld hl,0e043h		;0da8	21 43 e0	! C .
	rst 30h			;0dab	f7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SetErrorTables: ErrorTextTable <- $3C48 (messages),
; ErrorTextTable2 <- $39E1 (bit map).  RAM extensions can point
; these at custom error sets.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SetErrorTables:
	ld hl,l3c48h		;0dac	21 48 3c	! H <
	ld (ErrorTextTable),hl	;0daf	22 d8 dd	" . .
	ld hl,l39e1h		;0db2	21 e1 39	! . 9
	ld (ErrorTextTable2),hl	;0db5	22 da dd	" . .
	ret			;0db8	c9		.
sub_0db9h:
	call sub_0da8h		;0db9	cd a8 0d	. . .
	rlc a			;0dbc	cb 07		. .
	jp z,l17a8h		;0dbe	ca a8 17	. . .
	call sub_0dcah		;0dc1	cd ca 0d	. . .
	rst 18h			;0dc4	df		.
	ld b,e			;0dc5	43		C
	ld (hl),0d8h		;0dc6	36 d8		6 .
	ld a,00ch		;0dc8	3e 0c		> .
sub_0dcah:
	push af			;0dca	f5		.
	ld hl,0de17h		;0dcb	21 17 de	! . .
	ld (0de15h),hl		;0dce	22 15 de	" . .
	call c,sub_17a4h	;0dd1	dc a4 17	. . .
	call sub_0c31h		;0dd4	cd 31 0c	. 1 .
	pop af			;0dd7	f1		.
	push af			;0dd8	f5		.
	srl a			;0dd9	cb 3f		. ?
	ld hl,05ac0h		;0ddb	21 c0 5a	! . Z
	ld e,l			;0dde	5d		]
	ld d,h			;0ddf	54		T
	ld (hl),029h		;0de0	36 29		6 )
	cp 00ah			;0de2	fe 0a		. .
	jr c,l0de8h		;0de4	38 02		8 .
	ld (hl),02ah		;0de6	36 2a		6 *
l0de8h:
	inc de			;0de8	13		.
	ld bc,Filler001B_end+1	;0de9	01 1f 00	. . .
	ldir			;0dec	ed b0		. .
	inc (ix+001h)		;0dee	dd 34 01	. 4 .
	ld b,a			;0df1	47		G
	rst 8			;0df2	cf		.
	inc (ix+000h)		;0df3	dd 34 00	. 4 .
	pop af			;0df6	f1		.
	ret			;0df7	c9		.
l0df8h:
	ld (iy+00ah),000h	;0df8	fd 36 0a 00	. 6 . .
	push af			;0dfc	f5		.
	call CommandLoop	;0dfd	cd d9 17	. . .
	pop af			;0e00	f1		.
	jr ExitToError		;0e01	18 1b		. .
l0e03h:
	ld sp,0e25eh		;0e03	31 5e e2	1 ^ .
	set 0,(iy+012h)		;0e06	fd cb 12 c6	. . . .
l0e0ah:
	call ReadPort7FFD	;0e0a	cd 11 04	. . .
	xor a			;0e0d	af		.
	ld (0dfe9h),a		;0e0e	32 e9 df	2 . .
	jr l0e14h		;0e11	18 01		. .
l0e13h:
	scf			;0e13	37		7
l0e14h:
	push af			;0e14	f5		.
	call CommandLoop	;0e15	cd d9 17	. . .
	pop af			;0e18	f1		.
	call nc,TapeMenu	;0e19	d4 99 2b	. . +
	ld a,081h		;0e1c	3e 81		> .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ExitToError: unwind to the command loop through the error
; window path (cf. InitErrorWindow $35F8).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ExitToError:
	ld sp,0e25eh		;0e1e	31 5e e2	1 ^ .
	call sub_049bh		;0e21	cd 9b 04	. . .
	ld hl,ExitToError	;0e24	21 1e 0e	! . .
	push hl			;0e27	e5		.
	call sub_0db9h		;0e28	cd b9 0d	. . .
	jr nc,ParseCommand	;0e2b	30 27		0 '
	call ScanTokens		;0e2d	cd ac 17	. . .
	inc b			;0e30	04		.
	ld e,l			;0e31	5d		]
	dec de			;0e32	1b		.
	dec b			;0e33	05		.
	ld h,b			;0e34	60		`
	dec de			;0e35	1b		.
	rrca			;0e36	0f		.
	dec l			;0e37	2d		-
	inc de			;0e38	13		.
	jp po,l1313h		;0e39	e2 13 13	. . .
	rst 0			;0e3c	c7		.
	add hl,bc		;0e3d	09		.
	inc de			;0e3e	13		.
	ret z			;0e3f	c8		.
	ld c,013h		;0e40	0e 13		. .
	ret			;0e42	c9		.
	rst 38h			;0e43	ff		.
	ld (de),a		;0e44	12		.
	jp l1304h		;0e45	c3 04 13	. . .
	call z,sub_2ce7h	;0e48	cc e7 2c	. . ,
	call sub_3065h		;0e4b	cd 65 30	. e 0
	sla e			;0e4e	cb 23		. #
	ld l,0ffh		;0e50	2e ff		. .
	xor a			;0e52	af		.
	ret			;0e53	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ParseCommand: the command/expression reader.  Accepts a typed
; line into the IX work buffer, evaluates operands (bank extension
; syntax handled by EvalOperand at $1766) and reduces the input
; to tokens for DispatchMode.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ParseCommand:
	call sub_11f3h		;0e54	cd f3 11	. . .
	ld e,000h		;0e57	1e 00		. .
	cp 00dh			;0e59	fe 0d		. .
	jp z,l0e0ah		;0e5b	ca 0a 0e	. . .
	cp 05bh			;0e5e	fe 5b		. [
	jr z,l0e6ah		;0e60	28 08		( .
	cp 028h			;0e62	fe 28		. (
	jr nz,l0e6dh		;0e64	20 07		  .
	ld e,004h		;0e66	1e 04		. .
	jr l0e6ch		;0e68	18 02		. .
l0e6ah:
	ld e,084h		;0e6a	1e 84		. .
l0e6ch:
	inc hl			;0e6c	23		#
l0e6dh:
	ld (iy+000h),e		;0e6d	fd 73 00	. s .
	bit 2,e			;0e70	cb 53		. S
	jr nz,l0ee0h		;0e72	20 6c		  l
	ld de,l12aeh		;0e74	11 ae 12	. . .
	ld bc,KeyScanTableB_start	;0e77	01 6f 12	. o .
	call sub_1237h		;0e7a	cd 37 12	. 7 .
	jp z,DispatchMode	;0e7d	ca 23 0f	. # .
	ex de,hl		;0e80	eb		.
	cp 021h			;0e81	fe 21		. !
	jr nc,l0e91h		;0e83	30 0c		0 .
	cp 00dh			;0e85	fe 0d		. .
	jr nc,l0e92h		;0e87	30 09		0 .
	set 3,(iy+000h)		;0e89	fd cb 00 de	. . . .
	sla c			;0e8d	cb 21		. !
	jr l0e96h		;0e8f	18 05		. .
l0e91h:
	inc bc			;0e91	03		.
l0e92h:
	ld a,c			;0e92	79		y
	sub 007h		;0e93	d6 07		. .
	ld c,a			;0e95	4f		O
l0e96h:
	ld a,(de)		;0e96	1a		.
	cp 03dh			;0e97	fe 3d		. =
	jp nz,DispatchMode	;0e99	c2 23 0f	. # .
	inc de			;0e9c	13		.
	ex de,hl		;0e9d	eb		.
	push bc			;0e9e	c5		.
	call sub_11cah		;0e9f	cd ca 11	. . .
	pop bc			;0ea2	c1		.
	cp 001h			;0ea3	fe 01		. .
	ld a,00ah		;0ea5	3e 0a		> .
	ret c			;0ea7	d8		.
	ld a,00bh		;0ea8	3e 0b		> .
	ret nz			;0eaa	c0		.
	bit 2,(iy+000h)		;0eab	fd cb 00 56	. . . V
	jr nz,l0f04h		;0eaf	20 53		  S
	ld hl,0dd69h		;0eb1	21 69 dd	! i .
	add hl,bc		;0eb4	09		.
	bit 3,(iy+000h)		;0eb5	fd cb 00 5e	. . . ^
	jr nz,l0ec5h		;0eb9	20 0a		  .
	ld a,d			;0ebb	7a		z
	or a			;0ebc	b7		.
	jr nz,l0eddh		;0ebd	20 1e		  .
	call sub_11bah		;0ebf	cd ba 11	. . .
	ld (hl),e		;0ec2	73		s
	jr l0edah		;0ec3	18 15		. .
l0ec5h:
	ld a,c			;0ec5	79		y
	cp 002h			;0ec6	fe 02		. .
	jr nz,l0eceh		;0ec8	20 04		  .
	res 6,(iy+00ah)		;0eca	fd cb 0a b6	. . . .
l0eceh:
	or a			;0ece	b7		.
	push af			;0ecf	f5		.
	call z,sub_11c1h	;0ed0	cc c1 11	. . .
	pop af			;0ed3	f1		.
	call nz,sub_11bah	;0ed4	c4 ba 11	. . .
	ld (hl),e		;0ed7	73		s
	inc hl			;0ed8	23		#
	ld (hl),d		;0ed9	72		r
l0edah:
	jp l0e13h		;0eda	c3 13 0e	. . .
l0eddh:
	ld a,00ch		;0edd	3e 0c		> .
	ret			;0edf	c9		.
l0ee0h:
	call sub_0fach		;0ee0	cd ac 0f	. . .
	ret c			;0ee3	d8		.
	ex de,hl		;0ee4	eb		.
	ld c,l			;0ee5	4d		M
	ld b,h			;0ee6	44		D
	ld a,(de)		;0ee7	1a		.
	inc de			;0ee8	13		.
	ld hl,IyWorkBase	;0ee9	21 df df	! . .
	cp 029h			;0eec	fe 29		. )
	jr z,l0efdh		;0eee	28 0d		( .
	cp 05dh			;0ef0	fe 5d		. ]
	jp nz,DispatchMode	;0ef2	c2 23 0f	. # .
	bit 7,(hl)		;0ef5	cb 7e		. ~
	jr z,l0f01h		;0ef7	28 08		( .
	set 3,(hl)		;0ef9	cb de		. .
	jr l0e96h		;0efb	18 99		. .
l0efdh:
	bit 7,(hl)		;0efd	cb 7e		. ~
	jr z,l0e96h		;0eff	28 95		( .
l0f01h:
	ld a,015h		;0f01	3e 15		> .
	ret			;0f03	c9		.
l0f04h:
	bit 3,(iy+000h)		;0f04	fd cb 00 5e	. . . ^
	ld l,c			;0f08	69		i
	ld h,b			;0f09	60		`
	jr nz,l0f1ch		;0f0a	20 10		  .
	ld a,d			;0f0c	7a		z
	or a			;0f0d	b7		.
	jr nz,l0eddh		;0f0e	20 cd		  .
	ld a,e			;0f10	7b		{
	rst 0			;0f11	c7		.
l0f12h:
	ld a,(0dfe9h)		;0f12	3a e9 df	: . .
	and 081h		;0f15	e6 81		. .
	ld (0dfe9h),a		;0f17	32 e9 df	2 . .
	jr l0edah		;0f1a	18 be		. .
l0f1ch:
	ld a,e			;0f1c	7b		{
	rst 0			;0f1d	c7		.
	inc hl			;0f1e	23		#
	ld a,d			;0f1f	7a		z
	rst 0			;0f20	c7		.
	jr l0f12h		;0f21	18 ef		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DispatchMode: routes between line editor, disassembler and
; register views according to the monitor flags (IY+12/IY+16
; bits), then falls into CommandLoop for the next command.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DispatchMode:
	ld hl,0e0aeh		;0f23	21 ae e0	! . .
	ld b,003h		;0f26	06 03		. .
l0f28h:
	ld e,(hl)		;0f28	5e		^
	inc hl			;0f29	23		#
	ld d,(hl)		;0f2a	56		V
	inc hl			;0f2b	23		#
	ld (0dda7h),de		;0f2c	ed 53 a7 dd	. S . .
	ld a,e			;0f30	7b		{
	or d			;0f31	b2		.
	jr z,l0f46h		;0f32	28 12		( .
	ld e,(hl)		;0f34	5e		^
	inc hl			;0f35	23		#
	ld d,(hl)		;0f36	56		V
	inc hl			;0f37	23		#
	push hl			;0f38	e5		.
	push bc			;0f39	c5		.
	call sub_11f3h		;0f3a	cd f3 11	. . .
	call FindKeyword	;0f3d	cd 00 12	. . .
	jr nz,l0f4bh		;0f40	20 09		  .
	pop bc			;0f42	c1		.
	pop hl			;0f43	e1		.
	djnz l0f28h		;0f44	10 e2		. .
l0f46h:
	ld a,016h		;0f46	3e 16		> .
	jp ExitToError		;0f48	c3 1e 0e	. . .
l0f4bh:
	pop de			;0f4b	d1		.
	ex (sp),hl		;0f4c	e3		.
	add a,c			;0f4d	81		.
	add a,c			;0f4e	81		.
	ld c,a			;0f4f	4f		O
	ld hl,(0dda7h)		;0f50	2a a7 dd	* . .
	add hl,bc		;0f53	09		.
	ld e,(hl)		;0f54	5e		^
	inc hl			;0f55	23		#
	ld d,(hl)		;0f56	56		V
	inc hl			;0f57	23		#
	ld c,(hl)		;0f58	4e		N
	pop hl			;0f59	e1		.
	push de			;0f5a	d5		.
	bit 7,c			;0f5b	cb 79		. y
	jr nz,l0f7bh		;0f5d	20 1c		  .
	bit 5,c			;0f5f	cb 69		. i
	ret nz			;0f61	c0		.
	push bc			;0f62	c5		.
	call sub_11cah		;0f63	cd ca 11	. . .
	pop bc			;0f66	c1		.
	bit 6,c			;0f67	cb 71		. q
	jr nz,l0f76h		;0f69	20 0b		  .
	cp c			;0f6b	b9		.
	jr z,l0f76h		;0f6c	28 08		( .
	ld a,00ah		;0f6e	3e 0a		> .
	jr c,l0f73h		;0f70	38 01		8 .
	inc a			;0f72	3c		<
l0f73h:
	jp ExitToError		;0f73	c3 1e 0e	. . .
l0f76h:
	ld bc,(0dd9dh)		;0f76	ed 4b 9d dd	. K . .
	ret			;0f7a	c9		.
l0f7bh:
	ld b,000h		;0f7b	06 00		. .
	push hl			;0f7d	e5		.
l0f7eh:
	dec c			;0f7e	0d		.
	bit 7,c			;0f7f	cb 79		. y
	jr nz,l0f87h		;0f81	20 04		  .
	set 1,(iy+000h)		;0f83	fd cb 00 ce	. . . .
l0f87h:
	bit 1,(iy+000h)		;0f87	fd cb 00 4e	. . . N
	push bc			;0f8b	c5		.
	jr nz,l0f93h		;0f8c	20 05		  .
	call sub_0fach		;0f8e	cd ac 0f	. . .
	jr l0f96h		;0f91	18 03		. .
l0f93h:
	call sub_1069h		;0f93	cd 69 10	. i .
l0f96h:
	pop bc			;0f96	c1		.
	jr z,l0f9eh		;0f97	28 05		( .
	jp nc,ExitToError	;0f99	d2 1e 0e	. . .
	pop hl			;0f9c	e1		.
	ret			;0f9d	c9		.
l0f9eh:
	inc b			;0f9e	04		.
	bit 7,c			;0f9f	cb 79		. y
	jr nz,l0f7eh		;0fa1	20 db		  .
	ld a,d			;0fa3	7a		z
	or a			;0fa4	b7		.
	jr z,l0f7eh		;0fa5	28 d7		( .
	ld a,00ch		;0fa7	3e 0c		> .
	jp ExitToError		;0fa9	c3 1e 0e	. . .
sub_0fach:
	dec hl			;0fac	2b		+
l0fadh:
	inc hl			;0fad	23		#
	call sub_1056h		;0fae	cd 56 10	. V .
	jr nz,l0fbbh		;0fb1	20 08		  .
	jr nc,l0fadh		;0fb3	30 f8		0 .
	xor a			;0fb5	af		.
	inc a			;0fb6	3c		<
	ld a,012h		;0fb7	3e 12		> .
	scf			;0fb9	37		7
	ret			;0fba	c9		.
l0fbbh:
	ld (iy+00fh),000h	;0fbb	fd 36 0f 00	. 6 . .
l0fbfh:
	ld de,WriteAnyBankByte	;0fbf	11 00 00	. . .
	push de			;0fc2	d5		.
	ld d,02bh		;0fc3	16 2b		. +
	ld a,(hl)		;0fc5	7e		~
	call sub_104ch		;0fc6	cd 4c 10	. L .
	jr nz,l0fcdh		;0fc9	20 02		  .
	inc hl			;0fcb	23		#
l0fcch:
	ld d,a			;0fcc	57		W
l0fcdh:
	push de			;0fcd	d5		.
	ld a,(hl)		;0fce	7e		~
	cp 028h			;0fcf	fe 28		. (
	jr z,l0fd7h		;0fd1	28 04		( .
	cp 05bh			;0fd3	fe 5b		. [
	jr nz,l0fe0h		;0fd5	20 09		  .
l0fd7h:
	inc hl			;0fd7	23		#
	inc (iy+00fh)		;0fd8	fd 34 0f	. 4 .
	pop de			;0fdb	d1		.
	ld e,a			;0fdc	5f		_
	push de			;0fdd	d5		.
	jr l0fbfh		;0fde	18 df		. .
l0fe0h:
	call sub_1069h		;0fe0	cd 69 10	. i .
	jr nz,l100ch		;0fe3	20 27		  '
l0fe5h:
	pop af			;0fe5	f1		.
	ex (sp),hl		;0fe6	e3		.
	cp 02dh			;0fe7	fe 2d		. -
	jr z,l0feeh		;0fe9	28 03		( .
	add hl,de		;0feb	19		.
	jr l0ff1h		;0fec	18 03		. .
l0feeh:
	or a			;0fee	b7		.
	sbc hl,de		;0fef	ed 52		. R
l0ff1h:
	ex (sp),hl		;0ff1	e3		.
	ld a,(hl)		;0ff2	7e		~
l0ff3h:
	inc hl			;0ff3	23		#
	call sub_104ch		;0ff4	cd 4c 10	. L .
	jr z,l0fcch		;0ff7	28 d3		( .
	cp 029h			;0ff9	fe 29		. )
	jr z,l100fh		;0ffb	28 12		( .
	cp 05dh			;0ffd	fe 5d		. ]
	jr z,l1022h		;0fff	28 21		( !
	dec hl			;1001	2b		+
	xor a			;1002	af		.
	cp (iy+00fh)		;1003	fd be 0f	. . .
	jr nz,l100ah		;1006	20 02		  .
	pop de			;1008	d1		.
	ret			;1009	c9		.
l100ah:
	ld a,013h		;100a	3e 13		> .
l100ch:
	jp ExitToError		;100c	c3 1e 0e	. . .
l100fh:
	pop de			;100f	d1		.
l1010h:
	call sub_1036h		;1010	cd 36 10	. 6 .
	pop af			;1013	f1		.
	push af			;1014	f5		.
	ld a,015h		;1015	3e 15		> .
	jr c,l100ch		;1017	38 f3		8 .
	push hl			;1019	e5		.
	ex de,hl		;101a	eb		.
	rst 28h			;101b	ef		.
	ld e,a			;101c	5f		_
	ld d,000h		;101d	16 00		. .
	pop hl			;101f	e1		.
	jr l0fe5h		;1020	18 c3		. .
l1022h:
	pop de			;1022	d1		.
l1023h:
	call sub_1036h		;1023	cd 36 10	. 6 .
	pop af			;1026	f1		.
	push af			;1027	f5		.
	ld a,015h		;1028	3e 15		> .
	jr nc,l100ch		;102a	30 e0		0 .
	push hl			;102c	e5		.
	ex de,hl		;102d	eb		.
	rst 28h			;102e	ef		.
	ld e,a			;102f	5f		_
	inc hl			;1030	23		#
	rst 28h			;1031	ef		.
	ld d,a			;1032	57		W
	pop hl			;1033	e1		.
	jr l0fe5h		;1034	18 af		. .
sub_1036h:
	xor a			;1036	af		.
	cp (iy+00fh)		;1037	fd be 0f	. . .
	jr z,l1040h		;103a	28 04		( .
	dec (iy+00fh)		;103c	fd 35 0f	. 5 .
	ret			;103f	c9		.
l1040h:
	bit 2,(iy+000h)		;1040	fd cb 00 56	. . . V
	ld a,014h		;1044	3e 14		> .
	jr z,l100ch		;1046	28 c4		( .
	pop bc			;1048	c1		.
	dec hl			;1049	2b		+
	xor a			;104a	af		.
	ret			;104b	c9		.
sub_104ch:
	cp 02bh			;104c	fe 2b		. +
	ret z			;104e	c8		.
	cp 02dh			;104f	fe 2d		. -
	ret			;1051	c9		.
	ld a,(hl)		;1052	7e		~
	cp 03ah			;1053	fe 3a		. :
	ret z			;1055	c8		.
sub_1056h:
	ld a,(hl)		;1056	7e		~
	cp 020h			;1057	fe 20		.  
	ret z			;1059	c8		.
	ld a,(hl)		;105a	7e		~
	cp 02ch			;105b	fe 2c		. ,
	ret z			;105d	c8		.
	cp 03bh			;105e	fe 3b		. ;
	ret z			;1060	c8		.
	or a			;1061	b7		.
	scf			;1062	37		7
	ret z			;1063	c8		.
	cp 00dh			;1064	fe 0d		. .
	ret nz			;1066	c0		.
	scf			;1067	37		7
	ret			;1068	c9		.
sub_1069h:
	bit 0,(iy+000h)		;1069	fd cb 00 46	. . . F
	ld a,010h		;106d	3e 10		> .
	ld (0dfefh),a		;106f	32 ef df	2 . .
	jr nz,l1088h		;1072	20 14		  .
	dec hl			;1074	2b		+
l1075h:
	inc hl			;1075	23		#
	res 0,(iy+000h)		;1076	fd cb 00 86	. . . .
	call sub_1056h		;107a	cd 56 10	. V .
	jr nz,l1083h		;107d	20 04		  .
	jr c,l10bah		;107f	38 39		8 9
	jr l1075h		;1081	18 f2		. .
l1083h:
	cp 022h			;1083	fe 22		. "
	jr nz,l10c0h		;1085	20 39		  9
	inc hl			;1087	23		#
l1088h:
	ld a,(hl)		;1088	7e		~
	cp 00dh			;1089	fe 0d		. .
	jr z,l10b6h		;108b	28 29		( )
	cp 022h			;108d	fe 22		. "
	jr z,l10b0h		;108f	28 1f		( .
	ld e,a			;1091	5f		_
	ld d,000h		;1092	16 00		. .
	bit 1,(iy+000h)		;1094	fd cb 00 4e	. . . N
	jr nz,l10a2h		;1098	20 08		  .
	inc hl			;109a	23		#
	ld a,022h		;109b	3e 22		> "
	cp (hl)			;109d	be		.
	jr nz,l10b6h		;109e	20 16		  .
	jr l10a6h		;10a0	18 04		. .
l10a2h:
	set 0,(iy+000h)		;10a2	fd cb 00 c6	. . . .
l10a6h:
	inc hl			;10a6	23		#
	xor a			;10a7	af		.
	ld (0dfefh),a		;10a8	32 ef df	2 . .
	ret			;10ab	c9		.
	ld a,00dh		;10ac	3e 0d		> .
	or a			;10ae	b7		.
	ret			;10af	c9		.
l10b0h:
	bit 1,(iy+000h)		;10b0	fd cb 00 4e	. . . N
	jr nz,l1075h		;10b4	20 bf		  .
l10b6h:
	ld a,00eh		;10b6	3e 0e		> .
	or a			;10b8	b7		.
	ret			;10b9	c9		.
l10bah:
	xor a			;10ba	af		.
	inc a			;10bb	3c		<
	ld a,00fh		;10bc	3e 0f		> .
	scf			;10be	37		7
	ret			;10bf	c9		.
l10c0h:
	ld de,l12a9h		;10c0	11 a9 12	. . .
	ld bc,l124ah		;10c3	01 4a 12	. J .
	call sub_1237h		;10c6	cd 37 12	. 7 .
	jr nz,l10d1h		;10c9	20 06		  .
	call 01108h		;10cb	cd 08 11	. . .
	ret z			;10ce	c8		.
	or a			;10cf	b7		.
	ret			;10d0	c9		.
l10d1h:
	ld de,l0001h		;10d1	11 01 00	. . .
	ld a,c			;10d4	79		y
	or a			;10d5	b7		.
	ret z			;10d6	c8		.
	dec de			;10d7	1b		.
	dec a			;10d8	3d		=
	ret z			;10d9	c8		.
	push hl			;10da	e5		.
	ld hl,0dd84h		;10db	21 84 dd	! . .
	ld a,c			;10de	79		y
	sub 023h		;10df	d6 23		. #
l10e1h:
	jr nc,l10f8h		;10e1	30 15		0 .
	ld hl,0dd6fh		;10e3	21 6f dd	! o .
	ld a,c			;10e6	79		y
	sub 00fh		;10e7	d6 0f		. .
	jr nc,l10f8h		;10e9	30 0d		0 .
	dec bc			;10eb	0b		.
	dec bc			;10ec	0b		.
	ld hl,0dd69h		;10ed	21 69 dd	! i .
	add hl,bc		;10f0	09		.
	add hl,bc		;10f1	09		.
	ld e,(hl)		;10f2	5e		^
	inc hl			;10f3	23		#
	ld d,(hl)		;10f4	56		V
l10f5h:
	pop hl			;10f5	e1		.
	xor a			;10f6	af		.
	ret			;10f7	c9		.
l10f8h:
	add a,l			;10f8	85		.
	ld l,a			;10f9	6f		o
	jr nc,l10fdh		;10fa	30 01		0 .
	inc h			;10fc	24		$
l10fdh:
	ld e,(hl)		;10fd	5e		^
	jr l10f5h		;10fe	18 f5		. .
l1100h:
	dec h			;1100	25		%
	ld (bc),a		;1101	02		.
	ld b,b			;1102	40		@
	ex af,af'		;1103	08		.
	ld l,00ah		;1104	2e 0a		. .
	inc hl			;1106	23		#
	djnz l1143h		;1107	10 3a		. :
	or 0dfh			;1109	f6 df		. .
	ld b,00ah		;110b	06 0a		. .
	or a			;110d	b7		.
	jr z,l1111h		;110e	28 01		( .
	ld b,a			;1110	47		G
l1111h:
	call sub_116dh		;1111	cd 6d 11	. m .
	jr nc,l112bh		;1114	30 15		0 .
	ld de,l1100h		;1116	11 00 11	. . .
	ld b,004h		;1119	06 04		. .
l111bh:
	ld a,(de)		;111b	1a		.
	inc de			;111c	13		.
	cp (hl)			;111d	be		.
	ld a,(de)		;111e	1a		.
	inc de			;111f	13		.
	jr z,l1129h		;1120	28 07		( .
	djnz l111bh		;1122	10 f7		. .
	ld b,(iy+017h)		;1124	fd 46 17	. F .
	jr l112bh		;1127	18 02		. .
l1129h:
	inc hl			;1129	23		#
	ld b,a			;112a	47		G
l112bh:
	ld (iy+010h),b		;112b	fd 70 10	. p .
	call sub_1133h		;112e	cd 33 11	. 3 .
	or a			;1131	b7		.
	ret			;1132	c9		.
sub_1133h:
	call sub_1175h		;1133	cd 75 11	. u .
	ld a,010h		;1136	3e 10		> .
	ret c			;1138	d8		.
	ld b,000h		;1139	06 00		. .
	ld d,b			;113b	50		P
	ld e,c			;113c	59		Y
	inc hl			;113d	23		#
l113eh:
	call sub_1175h		;113e	cd 75 11	. u .
	jr c,l1161h		;1141	38 1e		8 .
l1143h:
	push hl			;1143	e5		.
	ld hl,(0dfefh)		;1144	2a ef df	* . .
	ld h,000h		;1147	26 00		& .
	ex de,hl		;1149	eb		.
	push bc			;114a	c5		.
	ld (iy+00eh),000h	;114b	fd 36 0e 00	. 6 . .
	call sub_119dh		;114f	cd 9d 11	. . .
	pop bc			;1152	c1		.
	bit 0,(iy+00eh)		;1153	fd cb 0e 46	. . . F
	jr nz,l1169h		;1157	20 10		  .
	add hl,bc		;1159	09		.
	jr c,l1169h		;115a	38 0d		8 .
	ex de,hl		;115c	eb		.
	pop hl			;115d	e1		.
	inc hl			;115e	23		#
	jr l113eh		;115f	18 dd		. .
l1161h:
	call sub_116dh		;1161	cd 6d 11	. m .
	ld a,010h		;1164	3e 10		> .
	ret nc			;1166	d0		.
	xor a			;1167	af		.
	ret			;1168	c9		.
l1169h:
	pop hl			;1169	e1		.
	ld a,011h		;116a	3e 11		> .
	ret			;116c	c9		.
sub_116dh:
	ld a,(hl)		;116d	7e		~
	cp 030h			;116e	fe 30		. 0
	ret c			;1170	d8		.
	cp 03ah			;1171	fe 3a		. :
	ccf			;1173	3f		?
	ret			;1174	c9		.
sub_1175h:
	ld a,(hl)		;1175	7e		~
	cp 061h			;1176	fe 61		. a
	jr c,l117ch		;1178	38 02		8 .
	sub 020h		;117a	d6 20		.  
l117ch:
	sub 030h		;117c	d6 30		. 0
	ret c			;117e	d8		.
	cp 00ah			;117f	fe 0a		. .
	jr c,l1188h		;1181	38 05		8 .
	cp 011h			;1183	fe 11		. .
	ret c			;1185	d8		.
	sub 007h		;1186	d6 07		. .
l1188h:
	ld c,a			;1188	4f		O
	ld a,(0dfefh)		;1189	3a ef df	: . .
	dec a			;118c	3d		=
	cp c			;118d	b9		.
	ret			;118e	c9		.
	ld a,d			;118f	7a		z
	or e			;1190	b3		.
	jr z,l1198h		;1191	28 05		( .
	ld a,h			;1193	7c		|
	ld c,l			;1194	4d		M
	jp Divide16		;1195	c3 61 1d	. a .
l1198h:
	set 2,(iy+00eh)		;1198	fd cb 0e d6	. . . .
	ret			;119c	c9		.
sub_119dh:
	ld c,h			;119d	4c		L
	ld a,l			;119e	7d		}
	ld b,010h		;119f	06 10		. .
	ld hl,WriteAnyBankByte	;11a1	21 00 00	! . .
l11a4h:
	add hl,hl		;11a4	29		)
	jr nc,l11abh		;11a5	30 04		0 .
	set 0,(iy+00eh)		;11a7	fd cb 0e c6	. . . .
l11abh:
	rla			;11ab	17		.
	rl c			;11ac	cb 11		. .
	jr nc,l11b7h		;11ae	30 07		0 .
	add hl,de		;11b0	19		.
	jr nc,l11b7h		;11b1	30 04		0 .
	set 0,(iy+00eh)		;11b3	fd cb 0e c6	. . . .
l11b7h:
	djnz l11a4h		;11b7	10 eb		. .
	ret			;11b9	c9		.
sub_11bah:
	ld a,(0dfe9h)		;11ba	3a e9 df	: . .
	and 0f5h		;11bd	e6 f5		. .
	jr l11c6h		;11bf	18 05		. .
sub_11c1h:
	ld a,(0dfe9h)		;11c1	3a e9 df	: . .
	and 0cfh		;11c4	e6 cf		. .
l11c6h:
	ld (0dfe9h),a		;11c6	32 e9 df	2 . .
	ret			;11c9	c9		.
sub_11cah:
	xor a			;11ca	af		.
	ld bc,RegisterBackup	;11cb	01 99 dd	. . .
l11ceh:
	push af			;11ce	f5		.
	push bc			;11cf	c5		.
	call sub_0fach		;11d0	cd ac 0f	. . .
	pop bc			;11d3	c1		.
	jr c,l11eah		;11d4	38 14		8 .
	jp nz,ExitToError	;11d6	c2 1e 0e	. . .
	ld a,e			;11d9	7b		{
	ld (bc),a		;11da	02		.
	inc bc			;11db	03		.
	ld a,d			;11dc	7a		z
	ld (bc),a		;11dd	02		.
	inc bc			;11de	03		.
	pop af			;11df	f1		.
	inc a			;11e0	3c		<
	cp 006h			;11e1	fe 06		. .
	jr nz,l11ceh		;11e3	20 e9		  .
	ld a,00bh		;11e5	3e 0b		> .
	jp ExitToError		;11e7	c3 1e 0e	. . .
l11eah:
	pop af			;11ea	f1		.
	ld de,(RegisterBackup)	;11eb	ed 5b 99 dd	. [ . .
	ld hl,(0dd9bh)		;11ef	2a 9b dd	* . .
	ret			;11f2	c9		.
sub_11f3h:
	ld hl,0de17h		;11f3	21 17 de	! . .
l11f6h:
	ld a,(hl)		;11f6	7e		~
	cp 020h			;11f7	fe 20		.  
	inc hl			;11f9	23		#
	jr z,l11f6h		;11fa	28 fa		( .
	dec hl			;11fc	2b		+
	cp 00dh			;11fd	fe 0d		. .
	ret			;11ff	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FindKeyword: match (possibly abbreviated) input against the
; command/mnemonic token tables.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FindKeyword:
	ld c,000h		;1200	0e 00		. .
l1202h:
	push hl			;1202	e5		.
l1203h:
	ld a,(de)		;1203	1a		.
	ld b,a			;1204	47		G
	and 07fh		;1205	e6 7f		. .
	cp 061h			;1207	fe 61		. a
	jr c,l120dh		;1209	38 02		8 .
	set 7,c			;120b	cb f9		. .
l120dh:
	ld a,b			;120d	78		x
	call KeyScanTableB_end	;120e	cd 92 12	. . .
	ld b,a			;1211	47		G
	ld a,(hl)		;1212	7e		~
	call KeyScanTableB_end	;1213	cd 92 12	. . .
	cp b			;1216	b8		.
	jr nz,l1227h		;1217	20 0e		  .
	ld a,(de)		;1219	1a		.
	bit 7,a			;121a	cb 7f		. .
	inc hl			;121c	23		#
	inc de			;121d	13		.
	jr z,l1203h		;121e	28 e3		( .
l1220h:
	pop de			;1220	d1		.
	res 7,c			;1221	cb b9		. .
	ld a,c			;1223	79		y
	ld b,000h		;1224	06 00		. .
	ret			;1226	c9		.
l1227h:
	bit 7,c			;1227	cb 79		. y
	jr nz,l1220h		;1229	20 f5		  .
	pop hl			;122b	e1		.
l122ch:
	ld a,(de)		;122c	1a		.
	rlca			;122d	07		.
	inc de			;122e	13		.
	jr nc,l122ch		;122f	30 fb		0 .
	inc c			;1231	0c		.
	ld a,(de)		;1232	1a		.
	or a			;1233	b7		.
	jr nz,l1202h		;1234	20 cc		  .
	ret			;1236	c9		.
sub_1237h:
	push bc			;1237	c5		.
	call FindKeyword	;1238	cd 00 12	. . .
	pop de			;123b	d1		.
	ret z			;123c	c8		.
	push de			;123d	d5		.
	ex (sp),hl		;123e	e3		.
	push af			;123f	f5		.
	add a,l			;1240	85		.
	ld l,a			;1241	6f		o
	jr nc,l1245h		;1242	30 01		0 .
	inc h			;1244	24		$
l1245h:
	pop af			;1245	f1		.
	ld a,(hl)		;1246	7e		~
	ld c,a			;1247	4f		O
	pop hl			;1248	e1		.
	ret			;1249	c9		.
l124ah:
	nop			;124a	00		.
	ld bc,l2302h		;124b	01 02 23	. . #
	ld c,00dh		;124e	0e 0d		. .
	ld (l2120h),hl		;1250	22 20 21	"   !
	rra			;1253	1f		.
	rrca			;1254	0f		.
	djnz KeyScanTableA_end	;1255	10 11		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyScanTableA: first key-translate table of
; the editor - internal key codes in scan
; order, consumed by the matching editor loop;
; continues in KeyScanTableB ($126F).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KeyScanTableA' (start 0x1257 end 0x1268)
KeyScanTableA_start:
	defb 012h		;1257	12		.
	defb 005h		;1258	05		.
	defb 006h		;1259	06		.
	defb 003h		;125a	03		.
	defb 004h		;125b	04		.
	defb 00ah		;125c	0a		.
	defb 00bh		;125d	0b		.
	defb 00ch		;125e	0c		.
	defb 007h		;125f	07		.
	defb 008h		;1260	08		.
	defb 009h		;1261	09		.
	defb 019h		;1262	19		.
	defb 01ah		;1263	1a		.
	defb 01bh		;1264	1b		.
	defb 01ch		;1265	1c		.
	defb 01dh		;1266	1d		.
	defb 01eh		;1267	1e		.
KeyScanTableA_end:
	inc de			;1268	13		.
	inc d			;1269	14		.
	dec d			;126a	15		.
	ld d,017h		;126b	16 17		. .
	jr $+38			;126d	18 24		. $
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyScanTableB: second key-translate table;
; the final $22 byte acts as a sentinel.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KeyScanTableB' (start 0x126f end 0x1292)
KeyScanTableB_start:
	defb 000h		;126f	00		.
	defb 021h		;1270	21		!
	defb 00ch		;1271	0c		.
	defb 00bh		;1272	0b		.
	defb 020h		;1273	20		 
	defb 01eh		;1274	1e		.
	defb 01fh		;1275	1f		.
	defb 01dh		;1276	1d		.
	defb 00dh		;1277	0d		.
	defb 00eh		;1278	0e		.
	defb 00fh		;1279	0f		.
	defb 010h		;127a	10		.
	defb 003h		;127b	03		.
	defb 004h		;127c	04		.
	defb 001h		;127d	01		.
	defb 002h		;127e	02		.
	defb 008h		;127f	08		.
	defb 009h		;1280	09		.
	defb 00ah		;1281	0a		.
	defb 005h		;1282	05		.
	defb 006h		;1283	06		.
	defb 007h		;1284	07		.
	defb 017h		;1285	17		.
	defb 018h		;1286	18		.
	defb 019h		;1287	19		.
	defb 01ah		;1288	1a		.
	defb 01bh		;1289	1b		.
	defb 01ch		;128a	1c		.
	defb 011h		;128b	11		.
	defb 012h		;128c	12		.
	defb 013h		;128d	13		.
	defb 014h		;128e	14		.
	defb 015h		;128f	15		.
	defb 016h		;1290	16		.
	defb 022h		;1291	22		"
KeyScanTableB_end:
	and 07fh		;1292	e6 7f		. .
	cp 040h			;1294	fe 40		. @
	ret c			;1296	d8		.
	and 05fh		;1297	e6 5f		. _
	ret			;1299	c9		.
l129ah:
	ld d,b			;129a	50		P
	jp 0d053h		;129b	c3 53 d0	. S .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegisterNameStrings: pool of short register-name
; strings, the last character of each carrying
; bit 7 ('I','I','H','D','B','AF','A','IX','IX',
; 'IY','IY','P','S','HL','DE','BC' plus single
; letters); indexed by the register display and
; editor.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'RegisterNameStrings' (start 0x129e end 0x12f4)
RegisterNameStrings_start:
	defb 049h		;129e	49		I
	defb 0d8h		;129f	d8		.
	defb 049h		;12a0	49		I
	defb 0d9h		;12a1	d9		.
	defb 048h		;12a2	48		H
	defb 0cch		;12a3	cc		.
	defb 044h		;12a4	44		D
	defb 0c5h		;12a5	c5		.
	defb 042h		;12a6	42		B
	defb 0c3h		;12a7	c3		.
	defb 000h		;12a8	00		.
l12a9h:
	defb 04fh		;12a9	4f		O
	defb 0ceh		;12aa	ce		.
l12abh:
	defb 04fh		;12ab	4f		O
	defb 046h		;12ac	46		F
	defb 0c6h		;12ad	c6		.
l12aeh:
	defb 04dh		;12ae	4d		M
	defb 045h		;12af	45		E
	defb 0cdh		;12b0	cd		.
	defb 0d2h		;12b1	d2		.
	defb 041h		;12b2	41		A
	defb 046h		;12b3	46		F
	defb 0a7h		;12b4	a7		.
	defb 041h		;12b5	41		A
	defb 0c6h		;12b6	c6		.
	defb 041h		;12b7	41		A
	defb 0a7h		;12b8	a7		.
	defb 0c1h		;12b9	c1		.
	defb 046h		;12ba	46		F
	defb 0a7h		;12bb	a7		.
	defb 0c6h		;12bc	c6		.
	defb 049h		;12bd	49		I
	defb 058h		;12be	58		X
	defb 0cch		;12bf	cc		.
	defb 049h		;12c0	49		I
	defb 058h		;12c1	58		X
	defb 0c8h		;12c2	c8		.
	defb 049h		;12c3	49		I
	defb 059h		;12c4	59		Y
	defb 0cch		;12c5	cc		.
	defb 049h		;12c6	49		I
	defb 059h		;12c7	59		Y
	defb 0c8h		;12c8	c8		.
	defb 049h		;12c9	49		I
	defb 0d8h		;12ca	d8		.
	defb 049h		;12cb	49		I
	defb 0d9h		;12cc	d9		.
	defb 050h		;12cd	50		P
	defb 0c3h		;12ce	c3		.
	defb 053h		;12cf	53		S
	defb 0d0h		;12d0	d0		.
	defb 048h		;12d1	48		H
	defb 04ch		;12d2	4c		L
	defb 0a7h		;12d3	a7		.
	defb 044h		;12d4	44		D
	defb 045h		;12d5	45		E
	defb 0a7h		;12d6	a7		.
	defb 042h		;12d7	42		B
	defb 043h		;12d8	43		C
	defb 0a7h		;12d9	a7		.
	defb 048h		;12da	48		H
	defb 0cch		;12db	cc		.
	defb 044h		;12dc	44		D
	defb 0c5h		;12dd	c5		.
	defb 042h		;12de	42		B
	defb 0c3h		;12df	c3		.
	defb 04ch		;12e0	4c		L
	defb 0a7h		;12e1	a7		.
	defb 048h		;12e2	48		H
	defb 0a7h		;12e3	a7		.
	defb 045h		;12e4	45		E
	defb 0a7h		;12e5	a7		.
	defb 044h		;12e6	44		D
	defb 0a7h		;12e7	a7		.
	defb 043h		;12e8	43		C
	defb 0a7h		;12e9	a7		.
	defb 042h		;12ea	42		B
	defb 0a7h		;12eb	a7		.
	defb 0cch		;12ec	cc		.
	defb 0c8h		;12ed	c8		.
	defb 0c5h		;12ee	c5		.
	defb 0c4h		;12ef	c4		.
	defb 0c3h		;12f0	c3		.
	defb 0c2h		;12f1	c2		.
	defb 0c9h		;12f2	c9		.
	defb 000h		;12f3	00		.
RegisterNameStrings_end:
	ex de,hl		;12f4	eb		.
l12f5h:
	dec b			;12f5	05		.
	ret z			;12f6	c8		.
l12f7h:
	ld a,(de)		;12f7	1a		.
	inc de			;12f8	13		.
	and 080h		;12f9	e6 80		. .
	jr nz,l12f5h		;12fb	20 f8		  .
	jr l12f7h		;12fd	18 f8		. .
	ld bc,0fff8h		;12ff	01 f8 ff	. . .
	jr l1319h		;1302	18 15		. .
l1304h:
	ld bc,Rst08Vector	;1304	01 08 00	. . .
	jr l1319h		;1307	18 10		. .
	ld bc,0ffffh		;1309	01 ff ff	. . .
	jr l1319h		;130c	18 0b		. .
	ld bc,l0001h		;130e	01 01 00	. . .
	jr l1319h		;1311	18 06		. .
l1313h:
	ld hl,(0dd69h)		;1313	2a 69 dd	* i .
	call FetchPrefixBytes	;1316	cd a8 1a	. . .
l1319h:
	ld hl,(0dd69h)		;1319	2a 69 dd	* i .
	add hl,bc		;131c	09		.
	ld (0dd69h),hl		;131d	22 69 dd	" i .
	ld a,001h		;1320	3e 01		> .
	bit 0,(iy+00ah)		;1322	fd cb 0a 46	. . . F
	jr z,l133fh		;1326	28 17		( .
	call MemoryDumpWindow	;1328	cd 6f 19	. o .
	xor a			;132b	af		.
	ret			;132c	c9		.
	ld hl,(UserPc)		;132d	2a 6b dd	* k .
	call FetchPrefixBytes	;1330	cd a8 1a	. . .
	ld (UserPc),hl		;1333	22 6b dd	" k .
	ld a,(iy+00ah)		;1336	fd 7e 0a	. ~ .
	and 0b5h		;1339	e6 b5		. .
	ld (iy+00ah),a		;133b	fd 77 0a	. w .
	xor a			;133e	af		.
l133fh:
	push af			;133f	f5		.
	call CommandLoop	;1340	cd d9 17	. . .
	pop af			;1343	f1		.
	ret			;1344	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ReportError - RST 08h backend, error code in B.  Walks the
; message table at (ErrorTextTable) with C as a rotating bit mask
; ($80, sla per entry); bit 7 chain-loads sub-strings.  Codes
; without a table entry fall back to printing the number.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ReportError:
	ld hl,(ErrorTextTable)	;1345	2a d8 dd	* . .
	ld c,080h		;1348	0e 80		. .
	call RegisterNameStrings_end	;134a	cd f4 12	. . .
	ex de,hl		;134d	eb		.
l134eh:
	ld a,(hl)		;134e	7e		~
	and 07fh		;134f	e6 7f		. .
	jr z,l137dh		;1351	28 2a		( *
	ld b,a			;1353	47		G
	push hl			;1354	e5		.
	ld hl,(ErrorTextTable2)	;1355	2a da dd	* . .
	call RegisterNameStrings_end	;1358	cd f4 12	. . .
	pop hl			;135b	e1		.
l135ch:
	ld a,(de)		;135c	1a		.
	sla c			;135d	cb 21		. !
	jr nc,l136eh		;135f	30 0d		0 .
	push af			;1361	f5		.
	and 07fh		;1362	e6 7f		. .
	cp 040h			;1364	fe 40		. @
	jr c,l136dh		;1366	38 05		8 .
	pop af			;1368	f1		.
	and 0dfh		;1369	e6 df		. .
	jr l136eh		;136b	18 01		. .
l136dh:
	pop af			;136d	f1		.
l136eh:
	call PrintCharBit7	;136e	cd 8d 0c	. . .
	rla			;1371	17		.
	inc de			;1372	13		.
	jr nc,l135ch		;1373	30 e7		0 .
	rst 20h			;1375	e7		.
	and b			;1376	a0		.
l1377h:
	bit 7,(hl)		;1377	cb 7e		. ~
	inc hl			;1379	23		#
	jr z,l134eh		;137a	28 d2		( .
	ret			;137c	c9		.
l137dh:
	push hl			;137d	e5		.
	sla c			;137e	cb 21		. !
	push bc			;1380	c5		.
	ld hl,(0dda7h)		;1381	2a a7 dd	* . .
	call PrintMarkerChar	;1384	cd c9 1c	. . .
	pop bc			;1387	c1		.
	pop hl			;1388	e1		.
	jr l1377h		;1389	18 ec		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BootContinue - boot bookkeeping at $C000..$C006: the signature
; must read byte i = i (walked down from $C006).  Broken -> RAM
; re-initialised and control loops back to the cold path; intact
; -> pass counter bumped.  this version adds a ROM checksum walk.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BootContinue:
	ld de,0c006h		;138b	11 06 c0	. . .
	ld a,(de)		;138e	1a		.
	sub e			;138f	93		.
	dec de			;1390	1b		.
	jr z,l139bh		;1391	28 08		( .
	call sub_383eh		;1393	cd 3e 38	. > 8
	jr c,l13a2h		;1396	38 0a		8 .
	ex de,hl		;1398	eb		.
	inc (hl)		;1399	34		4
	ex de,hl		;139a	eb		.
l139bh:
	ld a,(de)		;139b	1a		.
	sbc a,e			;139c	9b		.
	and 007h		;139d	e6 07		. .
	jp z,Filler001B_end	;139f	ca 1e 00	. . .
l13a2h:
	push af			;13a2	f5		.
	ld hl,WriteAnyBankByte	;13a3	21 00 00	! . .
	ld b,h			;13a6	44		D
	ld c,0feh		;13a7	0e fe		. .
	ld (PagingState),hl	;13a9	22 dd df	" . .
	ld h,005h		;13ac	26 05		& .
l13aeh:
	rst 28h			;13ae	ef		.
	add a,b			;13af	80		.
	ld b,a			;13b0	47		G
	inc hl			;13b1	23		#
	ld a,h			;13b2	7c		|
	sub 006h		;13b3	d6 06		. .
	jr nz,l13aeh		;13b5	20 f7		  .
	ld a,(l0d43h)		;13b7	3a 43 0d	: C .
	sub b			;13ba	90		.
	jp nz,Filler001B_end	;13bb	c2 1e 00	. . .
	in a,(c)		;13be	ed 78		. x
	bit 4,a			;13c0	cb 67		. g
	jr z,l1409h		;13c2	28 45		( E
l13c4h:
	call sub_048ch		;13c4	cd 8c 04	. . .
	ld iy,IyWorkBase	;13c7	fd 21 df df	. ! . .
	ld hl,RegWindowDef	;13cb	21 89 e0	! . .
	rst 30h			;13ce	f7		.
	call sub_1561h		;13cf	cd 61 15	. a .
	ld b,059h		;13d2	06 59		. Y
	rst 8			;13d4	cf		.
	pop af			;13d5	f1		.
	jr nc,EnterTrdos	;13d6	30 09		0 .
	call EnterTrdos		;13d8	cd e1 13	. . .
	ld a,0a1h		;13db	3e a1		> .
	ld (0ffd0h),a		;13dd	32 d0 ff	2 . .
	ret			;13e0	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EnterTrdos - boot into TR-DOS.  In v2.95 this lives inline in the
; boot continuation rather than as a separate routine: the paging
; state is cleared ($FFCA=0), a RAM-extension call (RST 18h) hands
; control to the DOS page of this bundle, then SetErrorTables
; installs the v2.95 message addresses below.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EnterTrdos:
	xor a			;13e1	af		.
	ld (0ffcah),a		;13e2	32 ca ff	2 . .
	rst 18h			;13e5	df		.
	call z,0d838h		;13e6	cc 38 d8	. 8 .
	call SetErrorTables	;13e9	cd ac 0d	. . .
	ld b,006h		;13ec	06 06		. .
	rst 8			;13ee	cf		.
	ld hl,0e3f7h		;13ef	21 f7 e3	! . .
	push hl			;13f2	e5		.
	ld bc,l0105h		;13f3	01 05 01	. . .
	ld de,l000dh+2		;13f6	11 0f 00	. . .
	call sub_02d6h		;13f9	cd d6 02	. . .
	pop hl			;13fc	e1		.
	ret c			;13fd	d8		.
	ld a,041h		;13fe	3e 41		> A
	cpi			;1400	ed a1		. .
	ret nz			;1402	c0		.
	ld a,04dh		;1403	3e 4d		> M
	cpi			;1405	ed a1		. .
	ret nz			;1407	c0		.
	jp (hl)			;1408	e9		.
l1409h:
	ld b,c			;1409	41		A
	in a,(c)		;140a	ed 78		. x
	rrca			;140c	0f		.
	jr c,l13c4h		;140d	38 b5		8 .
	ld (0e92ah),hl		;140f	22 2a e9	" * .
	jp l0040h		;1412	c3 40 00	. @ .
l1415h:
	ld a,089h		;1415	3e 89		> .
	call sub_156eh		;1417	cd 6e 15	. n .
	ld b,002h		;141a	06 02		. .
	rst 18h			;141c	df		.
	ld d,03ah		;141d	16 3a		. :
	jr c,l1435h		;141f	38 14		8 .
	call CallTrdos		;1421	cd 84 35	. . 5
	ld hl,ZeroPad3FBE_end	;1424	21 00 40	! . @
	ld bc,PadColumns+1	;1427	01 00 1b	. . .
	call FillMemoryRange	;142a	cd 73 28	. s (
	jr c,l1435h		;142d	38 06		8 .
	rst 18h			;142f	df		.
	and h			;1430	a4		.
	ld a,(0b5d2h)		;1431	3a d2 b5	: . .
	dec d			;1434	15		.
l1435h:
	call ReadNmiPort	;1435	cd a2 34	. . 4
	jp l3000h		;1438	c3 00 30	. . 0
l143bh:
	jp 0eb00h		;143b	c3 00 eb	. . .
	jp 0eb03h		;143e	c3 03 eb	. . .
	ld hl,0eb00h		;1441	21 00 eb	! . .
	jr l1449h		;1444	18 03		. .
	ld hl,0eb03h		;1446	21 03 eb	! . .
l1449h:
	ld a,(hl)		;1449	7e		~
	sub 0c3h		;144a	d6 c3		. .
	jr l148eh		;144c	18 40		. @
	jr l1415h		;144e	18 c5		. .
	xor a			;1450	af		.
	ld (0e932h),a		;1451	32 32 e9	2 2 .
	jp l15b5h		;1454	c3 b5 15	. . .
	ld hl,WriteAnyBankByte	;1457	21 00 00	! . .
	ld (0e019h),hl		;145a	22 19 e0	" . .
	ret			;145d	c9		.
	scf			;145e	37		7
	ld hl,0dff4h		;145f	21 f4 df	! . .
	set 5,(hl)		;1462	cb ee		. .
	jr nc,l1468h		;1464	30 02		0 .
	res 5,(hl)		;1466	cb ae		. .
l1468h:
	ld hl,l15b5h		;1468	21 b5 15	! . .
	ex (sp),hl		;146b	e3		.
	jp l07d2h		;146c	c3 d2 07	. . .
	bit 1,(iy+012h)		;146f	fd cb 12 4e	. . . N
	ret z			;1473	c8		.
	scf			;1474	37		7
	ret			;1475	c9		.
	bit 0,(iy+015h)		;1476	fd cb 15 46	. . . F
	ret nz			;147a	c0		.
	scf			;147b	37		7
	ret			;147c	c9		.
	scf			;147d	37		7
	bit 4,(iy-002h)		;147e	fd cb fe 66	. . . f
	ret z			;1482	c8		.
	ld a,(iy-001h)		;1483	fd 7e ff	. ~ .
	and 013h		;1486	e6 13		. .
	ret z			;1488	c8		.
	scf			;1489	37		7
	ret			;148a	c9		.
	ld a,(0c063h)		;148b	3a 63 c0	: c .
l148eh:
	or a			;148e	b7		.
	ret z			;148f	c8		.
	scf			;1490	37		7
	ret			;1491	c9		.
	ld a,(0dff8h)		;1492	3a f8 df	: . .
	rlca			;1495	07		.
	ccf			;1496	3f		?
	ret			;1497	c9		.
	jp l0e03h		;1498	c3 03 0e	. . .
	ld a,001h		;149b	3e 01		> .
	call sub_14b6h		;149d	cd b6 14	. . .
	call sub_15aah		;14a0	cd aa 15	. . .
	xor a			;14a3	af		.
	bit 2,(iy+012h)		;14a4	fd cb 12 56	. . . V
	jr z,l14abh		;14a8	28 01		( .
	inc a			;14aa	3c		<
l14abh:
	ld (hl),a		;14ab	77		w
	ret			;14ac	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg14AD: separator line of nine < characters
; (bit-7 terminator).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg14AD' (start 0x14ad end 0x14b7)
Msg14AD_start:
	defb 03ch		;14ad	3c		<
	defb 03ch		;14ae	3c		<
	defb 03ch		;14af	3c		<
	defb 03ch		;14b0	3c		<
	defb 03ch		;14b1	3c		<
	defb 03ch		;14b2	3c		<
	defb 03ch		;14b3	3c		<
	defb 03ch		;14b4	3c		<
	defb 03ch		;14b5	3c		<
sub_14b6h:
	defb 0f5h		;14b6	f5		.
Msg14AD_end:
	ld hl,(0dffah)		;14b7	2a fa df	* . .
	ld a,(0ddffh)		;14ba	3a ff dd	: . .
	ld (hl),a		;14bd	77		w
	inc hl			;14be	23		#
	ld (0dffah),hl		;14bf	22 fa df	" . .
	pop af			;14c2	f1		.
	ld (0ddffh),a		;14c3	32 ff dd	2 . .
l14c6h:
	jp l15b5h		;14c6	c3 b5 15	. . .
	ld de,0de08h		;14c9	11 08 de	. . .
	ld hl,(0dffah)		;14cc	2a fa df	* . .
	xor a			;14cf	af		.
	sbc hl,de		;14d0	ed 52		. R
	add hl,de		;14d2	19		.
	jr z,l14dah		;14d3	28 05		( .
	dec hl			;14d5	2b		+
	ld (0dffah),hl		;14d6	22 fa df	" . .
	ld a,(hl)		;14d9	7e		~
l14dah:
	ld (0ddffh),a		;14da	32 ff dd	2 . .
	jr l14c6h		;14dd	18 e7		. .
	jp l0119h		;14df	c3 19 01	. . .
	jp l355ah		;14e2	c3 5a 35	. Z 5
	ld hl,0ffcah		;14e5	21 ca ff	! . .
	ld bc,l0001h+1		;14e8	01 02 00	. . .
	jr l1502h		;14eb	18 15		. .
	ld hl,0ffcch		;14ed	21 cc ff	! . .
	jr l14f5h		;14f0	18 03		. .
	ld hl,0ffd0h		;14f2	21 d0 ff	! . .
l14f5h:
	ld bc,(0ffcah)		;14f5	ed 4b ca ff	. K . .
	ld b,000h		;14f9	06 00		. .
	add hl,bc		;14fb	09		.
	ld a,(hl)		;14fc	7e		~
	and 0e0h		;14fd	e6 e0		. .
	ld b,a			;14ff	47		G
	ld c,004h		;1500	0e 04		. .
l1502h:
	ld a,(hl)		;1502	7e		~
	and 003h		;1503	e6 03		. .
	inc a			;1505	3c		<
	cp c			;1506	b9		.
	jr c,l150ah		;1507	38 01		8 .
	xor a			;1509	af		.
l150ah:
	or b			;150a	b0		.
	ld (hl),a		;150b	77		w
	ret			;150c	c9		.
	scf			;150d	37		7
	ld hl,0dff2h		;150e	21 f2 df	! . .
	rr (hl)			;1511	cb 1e		. .
	call sub_0da8h		;1513	cd a8 0d	. . .
	ld a,00ch		;1516	3e 0c		> .
	or a			;1518	b7		.
	call sub_0dcah		;1519	cd ca 0d	. . .
	call sub_1561h		;151c	cd 61 15	. a .
	jp l32d3h		;151f	c3 d3 32	. . 2
	ld hl,l15b5h		;1522	21 b5 15	! . .
	ex (sp),hl		;1525	e3		.
	jp l319ah		;1526	c3 9a 31	. . 1
	ld a,0ffh		;1529	3e ff		> .
	ld (AyShadowReg),a	;152b	32 d7 df	2 . .
	call sub_174bh		;152e	cd 4b 17	. K .
	ld a,(de)		;1531	1a		.
	xor (hl)		;1532	ae		.
	ld (de),a		;1533	12		.
	ret			;1534	c9		.
	call sub_174bh		;1535	cd 4b 17	. K .
	ld c,(hl)		;1538	4e		N
	inc hl			;1539	23		#
	ld b,(hl)		;153a	46		F
	push de			;153b	d5		.
	push bc			;153c	c5		.
	ld a,087h		;153d	3e 87		> .
l153fh:
	call sub_156eh		;153f	cd 6e 15	. n .
	jp c,l15b5h		;1542	da b5 15	. . .
	call 01108h		;1545	cd 08 11	. . .
	jr nz,l153fh		;1548	20 f5		  .
	ld a,d			;154a	7a		z
	or a			;154b	b7		.
	ld a,00ch		;154c	3e 0c		> .
	jr nz,l153fh		;154e	20 ef		  .
	pop bc			;1550	c1		.
	push bc			;1551	c5		.
	ld a,e			;1552	7b		{
	call CheckRangeInclusive	;1553	cd 95 17	. . .
	ld a,00ch		;1556	3e 0c		> .
	jr c,l153fh		;1558	38 e5		8 .
	pop bc			;155a	c1		.
	ld a,e			;155b	7b		{
	pop de			;155c	d1		.
	ld (de),a		;155d	12		.
	jp l15b5h		;155e	c3 b5 15	. . .
sub_1561h:
	ld hl,l3bach		;1561	21 ac 3b	! . ;
	ld (ErrorTextTable),hl	;1564	22 d8 dd	" . .
	ld hl,KeywordTable_end	;1567	21 45 38	! E 8
	ld (ErrorTextTable2),hl	;156a	22 da dd	" . .
	ret			;156d	c9		.
sub_156eh:
	call sub_0db9h		;156e	cd b9 0d	. . .
	jr nc,l157ah		;1571	30 07		0 .
	cp 007h			;1573	fe 07		. .
	scf			;1575	37		7
	ret z			;1576	c8		.
	xor a			;1577	af		.
	jr sub_156eh		;1578	18 f4		. .
l157ah:
	call sub_11f3h		;157a	cd f3 11	. . .
	scf			;157d	37		7
	ccf			;157e	3f		?
	ret			;157f	c9		.
sub_1580h:
	ld hl,l3d98h		;1580	21 98 3d	! . =
	ld a,(0ddffh)		;1583	3a ff dd	: . .
	add a,a			;1586	87		.
	add a,l			;1587	85		.
	ld l,a			;1588	6f		o
	jr nc,l158ch		;1589	30 01		0 .
	inc h			;158b	24		$
l158ch:
	ld a,(hl)		;158c	7e		~
	inc hl			;158d	23		#
	ld h,(hl)		;158e	66		f
	ld l,a			;158f	6f		o
l1590h:
	ld a,(hl)		;1590	7e		~
	inc hl			;1591	23		#
	ret			;1592	c9		.
sub_1593h:
	call sub_15a0h		;1593	cd a0 15	. . .
sub_1596h:
	and 01fh		;1596	e6 1f		. .
	jr nz,l159dh		;1598	20 03		  .
	ld a,030h		;159a	3e 30		> 0
	ret			;159c	c9		.
l159dh:
	or 040h			;159d	f6 40		. @
	ret			;159f	c9		.
sub_15a0h:
	ld a,c			;15a0	79		y
	add a,a			;15a1	87		.
	add a,a			;15a2	87		.
	add a,l			;15a3	85		.
	ld l,a			;15a4	6f		o
	jr nc,l1590h		;15a5	30 e9		0 .
	inc h			;15a7	24		$
	jr l1590h		;15a8	18 e6		. .
sub_15aah:
	ld hl,0de00h		;15aa	21 00 de	! . .
	ld a,(0ddffh)		;15ad	3a ff dd	: . .
	add a,l			;15b0	85		.
	ld l,a			;15b1	6f		o
	ret nc			;15b2	d0		.
	inc h			;15b3	24		$
	ret			;15b4	c9		.
l15b5h:
	ld sp,0e25eh		;15b5	31 5e e2	1 ^ .
	call ReadPort7FFD	;15b8	cd 11 04	. . .
	call sub_1561h		;15bb	cd 61 15	. a .
	ld hl,RegWindowDef	;15be	21 89 e0	! . .
	rst 30h			;15c1	f7		.
	ld a,(0e0a5h)		;15c2	3a a5 e0	: . .
	ld (ix+006h),a		;15c5	dd 77 06	. w .
	call sub_0c31h		;15c8	cd 31 0c	. 1 .
	ld hl,0e0bah		;15cb	21 ba e0	! . .
	call PrintJustifiedHeader	;15ce	cd 93 1c	. . .
	call RunCommand		;15d1	cd ed 2f	. . /
	inc sp			;15d4	33		3
	jp pe,08921h		;15d5	ea 21 89	. ! .
	ret po			;15d8	e0		.
	rst 30h			;15d9	f7		.
	ld a,(0e0a6h)		;15da	3a a6 e0	: . .
	ld (ix+006h),a		;15dd	dd 77 06	. w .
	ld b,001h		;15e0	06 01		. .
	rst 8			;15e2	cf		.
	call sub_1580h		;15e3	cd 80 15	. . .
	ld b,(hl)		;15e6	46		F
	rst 8			;15e7	cf		.
	inc (ix+000h)		;15e8	dd 34 00	. 4 .
	ld c,000h		;15eb	0e 00		. .
	ld hl,015d6h		;15ed	21 d6 15	! . .
	push hl			;15f0	e5		.
	ld a,(0e0a5h)		;15f1	3a a5 e0	: . .
	ld (ix+006h),a		;15f4	dd 77 06	. w .
l15f7h:
	call sub_1580h		;15f7	cd 80 15	. . .
	inc a			;15fa	3c		<
	cp c			;15fb	b9		.
	jp z,l16a5h		;15fc	ca a5 16	. . .
	inc hl			;15ff	23		#
	call sub_15a0h		;1600	cd a0 15	. . .
	ld b,a			;1603	47		G
	call sub_16f5h		;1604	cd f5 16	. . .
	push hl			;1607	e5		.
	ld a,(0e0a7h)		;1608	3a a7 e0	: . .
	jr c,l161ah		;160b	38 0d		8 .
	ld a,b			;160d	78		x
	rlca			;160e	07		.
	rlca			;160f	07		.
	rlca			;1610	07		.
	and 007h		;1611	e6 07		. .
	ld l,a			;1613	6f		o
	ld a,(0e0a5h)		;1614	3a a5 e0	: . .
	and 0f8h		;1617	e6 f8		. .
	or l			;1619	b5		.
l161ah:
	ld (ix+006h),a		;161a	dd 77 06	. w .
	ld a,b			;161d	78		x
	call sub_1596h		;161e	cd 96 15	. . .
	ld b,a			;1621	47		G
	call sub_15aah		;1622	cd aa 15	. . .
	ld a,(hl)		;1625	7e		~
	cp c			;1626	b9		.
	jr nz,l163ch		;1627	20 13		  .
	ld a,(ix+006h)		;1629	dd 7e 06	. ~ .
	and 007h		;162c	e6 07		. .
	ld (ix+006h),a		;162e	dd 77 06	. w .
	ld a,(0e0a8h)		;1631	3a a8 e0	: . .
	and 038h		;1634	e6 38		. 8
	or (ix+006h)		;1636	dd b6 06	. . .
	ld (ix+006h),a		;1639	dd 77 06	. w .
l163ch:
	pop hl			;163c	e1		.
	call sub_1706h		;163d	cd 06 17	. . .
	ld a,(hl)		;1640	7e		~
	push af			;1641	f5		.
	rlca			;1642	07		.
	rlca			;1643	07		.
	and 003h		;1644	e6 03		. .
	inc hl			;1646	23		#
	add a,(ix+000h)		;1647	dd 86 00	. . .
	ld (ix+000h),a		;164a	dd 77 00	. w .
	ld (ix+001h),009h	;164d	dd 36 01 09	. 6 . .
	rst 20h			;1651	e7		.
	and b			;1652	a0		.
	ld a,b			;1653	78		x
	rst 10h			;1654	d7		.
	rst 20h			;1655	e7		.
	ld l,0a0h		;1656	2e a0		. .
	push bc			;1658	c5		.
	push hl			;1659	e5		.
	ld b,(hl)		;165a	46		F
	rst 8			;165b	cf		.
	rst 20h			;165c	e7		.
	and b			;165d	a0		.
	pop hl			;165e	e1		.
	inc hl			;165f	23		#
	pop bc			;1660	c1		.
	pop af			;1661	f1		.
	and 03fh		;1662	e6 3f		. ?
	jr z,l1699h		;1664	28 33		( 3
	push bc			;1666	c5		.
	call sub_1750h		;1667	cd 50 17	. P .
	and 003h		;166a	e6 03		. .
	cp 001h			;166c	fe 01		. .
	jr nz,l167eh		;166e	20 0e		  .
	ld a,(de)		;1670	1a		.
	and (hl)		;1671	a6		.
	inc hl			;1672	23		#
	ld b,(hl)		;1673	46		F
	inc hl			;1674	23		#
	jr z,l1678h		;1675	28 01		( .
l1677h:
	ld b,(hl)		;1677	46		F
l1678h:
	rst 8			;1678	cf		.
l1679h:
	rst 20h			;1679	e7		.
	and b			;167a	a0		.
	pop bc			;167b	c1		.
	jr l1699h		;167c	18 1b		. .
l167eh:
	cp 002h			;167e	fe 02		. .
	jr nz,l168eh		;1680	20 0c		  .
	ld a,(de)		;1682	1a		.
	ld c,a			;1683	4f		O
	ld b,000h		;1684	06 00		. .
	ld de,l0a00h		;1686	11 00 0a	. . .
	call sub_1ce3h		;1689	cd e3 1c	. . .
	jr l1679h		;168c	18 eb		. .
l168eh:
	ld a,(de)		;168e	1a		.
	and 003h		;168f	e6 03		. .
	jr z,l1677h		;1691	28 e4		( .
	ld b,a			;1693	47		G
l1694h:
	inc hl			;1694	23		#
	djnz l1694h		;1695	10 fd		. .
	jr l1677h		;1697	18 de		. .
l1699h:
	ld a,024h		;1699	3e 24		> $
	call sub_1b03h		;169b	cd 03 1b	. . .
	inc c			;169e	0c		.
	inc (ix+000h)		;169f	dd 34 00	. 4 .
	jp l15f7h		;16a2	c3 f7 15	. . .
l16a5h:
	call AutoRepeatKey	;16a5	cd 0d 1b	. . .
	push af			;16a8	f5		.
	ld a,(0e0a5h)		;16a9	3a a5 e0	: . .
	ld (ix+006h),a		;16ac	dd 77 06	. w .
	ld bc,l10e1h		;16af	01 e1 10	. . .
l16b2h:
	dec bc			;16b2	0b		.
	ld a,b			;16b3	78		x
	or c			;16b4	b1		.
	jr nz,l16b2h		;16b5	20 fb		  .
	pop af			;16b7	f1		.
	call ScanTokens		;16b8	cd ac 17	. . .
	ld a,(bc)		;16bb	0a		.
	ld hl,(l0b17h)		;16bc	2a 17 0b	* . .
	dec sp			;16bf	3b		;
	rla			;16c0	17		.
	dec c			;16c1	0d		.
	dec e			;16c2	1d		.
	rla			;16c3	17		.
	rlca			;16c4	07		.
	ld c,017h		;16c5	0e 17		. .
	rst 38h			;16c7	ff		.
	cp 07bh			;16c8	fe 7b		. {
	jr nc,l16d2h		;16ca	30 06		0 .
	cp 040h			;16cc	fe 40		. @
	jr c,l16d2h		;16ce	38 02		8 .
	and 05fh		;16d0	e6 5f		. _
l16d2h:
	ld b,a			;16d2	47		G
	ld c,0ffh		;16d3	0e ff		. .
l16d5h:
	inc c			;16d5	0c		.
	call sub_1580h		;16d6	cd 80 15	. . .
	inc a			;16d9	3c		<
	cp c			;16da	b9		.
	ret z			;16db	c8		.
	inc hl			;16dc	23		#
	call sub_1593h		;16dd	cd 93 15	. . .
	cp b			;16e0	b8		.
	jr nz,l16d5h		;16e1	20 f2		  .
	push hl			;16e3	e5		.
	call sub_15aah		;16e4	cd aa 15	. . .
	ld (hl),c		;16e7	71		q
	pop hl			;16e8	e1		.
	call sub_16f5h		;16e9	cd f5 16	. . .
	ret c			;16ec	d8		.
	push hl			;16ed	e5		.
	call sub_1706h		;16ee	cd 06 17	. . .
	ld c,l			;16f1	4d		M
	ld b,h			;16f2	44		D
	pop hl			;16f3	e1		.
	inc hl			;16f4	23		#
sub_16f5h:
	ld e,(hl)		;16f5	5e		^
	ld d,000h		;16f6	16 00		. .
	inc hl			;16f8	23		#
	push hl			;16f9	e5		.
	ld hl,l143bh		;16fa	21 3b 14	! ; .
	add hl,de		;16fd	19		.
	ld de,l1704h		;16fe	11 04 17	. . .
	push de			;1701	d5		.
	xor a			;1702	af		.
	jp (hl)			;1703	e9		.
l1704h:
	pop hl			;1704	e1		.
	ret			;1705	c9		.
sub_1706h:
	ld e,(hl)		;1706	5e		^
	ld d,000h		;1707	16 00		. .
	ld hl,l3dach		;1709	21 ac 3d	! . =
	add hl,de		;170c	19		.
	ret			;170d	c9		.
	ld hl,l15b5h		;170e	21 b5 15	! . .
	ex (sp),hl		;1711	e3		.
	xor a			;1712	af		.
	ld (0ddffh),a		;1713	32 ff dd	2 . .
	ld hl,0de08h		;1716	21 08 de	! . .
	ld (0dffah),hl		;1719	22 fa df	" . .
	ret			;171c	c9		.
	call sub_15aah		;171d	cd aa 15	. . .
	ld c,(hl)		;1720	4e		N
	call sub_1580h		;1721	cd 80 15	. . .
	inc hl			;1724	23		#
	call sub_1593h		;1725	cd 93 15	. . .
	jr l16d2h		;1728	18 a8		. .
	call sub_15aah		;172a	cd aa 15	. . .
	push hl			;172d	e5		.
	ld c,(hl)		;172e	4e		N
	inc c			;172f	0c		.
	call sub_1580h		;1730	cd 80 15	. . .
	cp c			;1733	b9		.
	jr nc,l1738h		;1734	30 02		0 .
	ld c,000h		;1736	0e 00		. .
l1738h:
	pop hl			;1738	e1		.
	ld (hl),c		;1739	71		q
	ret			;173a	c9		.
	call sub_15aah		;173b	cd aa 15	. . .
	ld a,(hl)		;173e	7e		~
	or a			;173f	b7		.
	jr z,l1744h		;1740	28 02		( .
	dec (hl)		;1742	35		5
	ret			;1743	c9		.
l1744h:
	push hl			;1744	e5		.
	call sub_1580h		;1745	cd 80 15	. . .
	pop hl			;1748	e1		.
	ld (hl),a		;1749	77		w
	ret			;174a	c9		.
sub_174bh:
	ld l,c			;174b	69		i
	ld h,b			;174c	60		`
	ld a,(bc)		;174d	0a		.
	inc hl			;174e	23		#
	inc hl			;174f	23		#
sub_1750h:
	bit 4,a			;1750	cb 67		. g
	jr z,l1768h		;1752	28 14		( .
	ld e,(hl)		;1754	5e		^
	inc hl			;1755	23		#
	ld d,(hl)		;1756	56		V
	inc hl			;1757	23		#
	ld a,(de)		;1758	1a		.
	and 003h		;1759	e6 03		. .
	inc a			;175b	3c		<
l175ch:
	dec a			;175c	3d		=
	ld b,(hl)		;175d	46		F
	inc hl			;175e	23		#
	jr z,EvalOperand	;175f	28 05		( .
l1761h:
	inc hl			;1761	23		#
	djnz l1761h		;1762	10 fd		. .
	jr l175ch		;1764	18 f6		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EvalOperand: evaluate one command/assembler operand, including
; the bank-extended indirect syntax (addresses may carry a bank
; qualifier routed through PeekPokeAnyBank).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EvalOperand:
	ld a,(hl)		;1766	7e		~
	inc hl			;1767	23		#
l1768h:
	ld e,(hl)		;1768	5e		^
	inc hl			;1769	23		#
	bit 2,a			;176a	cb 57		. W
	jr z,l1772h		;176c	28 04		( .
	ld d,(hl)		;176e	56		V
	inc hl			;176f	23		#
	jr l1779h		;1770	18 07		. .
l1772h:
	bit 7,e			;1772	cb 7b		. {
	ld d,000h		;1774	16 00		. .
	jr z,l1779h		;1776	28 01		( .
	dec d			;1778	15		.
l1779h:
	push iy			;1779	fd e5		. .
	ex (sp),hl		;177b	e3		.
	add hl,de		;177c	19		.
	ex de,hl		;177d	eb		.
	pop hl			;177e	e1		.
	bit 3,a			;177f	cb 5f		. _
	ret z			;1781	c8		.
	push af			;1782	f5		.
	ld b,(hl)		;1783	46		F
	inc hl			;1784	23		#
	ld a,(hl)		;1785	7e		~
	inc hl			;1786	23		#
	push hl			;1787	e5		.
	ld l,b			;1788	68		h
	ld h,a			;1789	67		g
	ld a,(hl)		;178a	7e		~
	and 003h		;178b	e6 03		. .
	ld l,a			;178d	6f		o
	ld h,000h		;178e	26 00		& .
	add hl,de		;1790	19		.
	ex de,hl		;1791	eb		.
	pop hl			;1792	e1		.
	pop af			;1793	f1		.
	ret			;1794	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckRangeInclusive: carry set when A lies within C..B
; (cp c / ret c, then cp b / ccf).  Validating addresses and
; command indices.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckRangeInclusive:
	cp c			;1795	b9		.
	ret c			;1796	d8		.
	cp b			;1797	b8		.
	ccf			;1798	3f		?
	ret			;1799	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SetDefaultWorkspace: HL <- $E05F (WorkBuffer), then falls into
; SetWorkspace.  Convenience entry before window operations.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SetDefaultWorkspace:
	ld hl,WorkBuffer	;179a	21 5f e0	! _ .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SetWorkspace - RST 30h backend: (WorkBufferPtr) <- HL and
; IX <- HL.  Window routines address their descriptors
; IX-relative, so this is how the monitor switches windows.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SetWorkspace:
	ld (WorkBufferPtr),hl	;179d	22 d4 e2	" . .
	push hl			;17a0	e5		.
	pop ix			;17a1	dd e1		. .
	ret			;17a3	c9		.
sub_17a4h:
	rst 18h			;17a4	df		.
	adc a,d			;17a5	8a		.
	scf			;17a6	37		7
	ret			;17a7	c9		.
l17a8h:
	rst 18h			;17a8	df		.
	ld a,036h		;17a9	3e 36		> 6
	ret			;17ab	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScanTokens: compressed table walk - each entry matches one
; character and holds a 2-byte skip to the next node; the
; terminator entry yields an action address executed via
; jp (HL).  The command tokenizer of the monitor.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScanTokens:
	pop hl			;17ac	e1		.
	jr l17b1h		;17ad	18 02		. .
l17afh:
	inc hl			;17af	23		#
	inc hl			;17b0	23		#
l17b1h:
	ld e,(hl)		;17b1	5e		^
	inc e			;17b2	1c		.
	jr z,l17beh		;17b3	28 09		( .
	cp (hl)			;17b5	be		.
	inc hl			;17b6	23		#
	jr nz,l17afh		;17b7	20 f6		  .
	ld e,(hl)		;17b9	5e		^
	inc hl			;17ba	23		#
	ld d,(hl)		;17bb	56		V
	dec de			;17bc	1b		.
	ex de,hl		;17bd	eb		.
l17beh:
	inc hl			;17be	23		#
	jp (hl)			;17bf	e9		.
	rst 20h			;17c0	e7		.
	add a,a			;17c1	87		.
	ret			;17c2	c9		.
sub_17c3h:
	call BuildStepStub	;17c3	cd aa 1e	. . .
	call FindWatchpoint	;17c6	cd 16 2a	. . *
	ld e,0ffh		;17c9	1e ff		. .
	jp nc,l0175h		;17cb	d2 75 01	. u .
l17ceh:
	ld a,(0dff5h)		;17ce	3a f5 df	: . .
	and 07fh		;17d1	e6 7f		. .
	bit 2,a			;17d3	cb 57		. W
	ret nz			;17d5	c0		.
	ld (0dfe9h),a		;17d6	32 e9 df	2 . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CommandLoop - the heart of the monitor: print the prompt in
; the E035 window, read a line through the editor, parse and
; execute it via the command table (RunCommand $2FED), repeat.
; Errors funnel back here through ExitToError.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CommandLoop:
	call sub_049bh		;17d9	cd 9b 04	. . .
	ld hl,PromptWindowDef	;17dc	21 35 e0	! 5 .
	rst 30h			;17df	f7		.
	bit 0,(iy+00ah)		;17e0	fd cb 0a 46	. . . F
	jr nz,$+55		;17e4	20 35		  5
	call sub_0c31h		;17e6	cd 31 0c	. 1 .
	rst 20h			;17e9	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegHeaderLine: header row of the register window
; ('IR  SZ-H-PNC  INT RAM ROM SCR  ZX' with
; embedded control bytes for the column gaps);
; printed above the register dump.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'RegHeaderLine' (start 0x17ea end 0x180e)
RegHeaderLine_start:
	defb 049h		;17ea	49		I
	defb 052h		;17eb	52		R
	defb 01bh		;17ec	1b		.
	defb 008h		;17ed	08		.
	defb 00ah		;17ee	0a		.
	defb 053h		;17ef	53		S
	defb 05ah		;17f0	5a		Z
	defb 02dh		;17f1	2d		-
	defb 048h		;17f2	48		H
	defb 02dh		;17f3	2d		-
	defb 050h		;17f4	50		P
	defb 04eh		;17f5	4e		N
	defb 043h		;17f6	43		C
	defb 00dh		;17f7	0d		.
	defb 00dh		;17f8	0d		.
	defb 049h		;17f9	49		I
	defb 04eh		;17fa	4e		N
	defb 054h		;17fb	54		T
	defb 020h		;17fc	20		 
	defb 052h		;17fd	52		R
	defb 041h		;17fe	41		A
	defb 04dh		;17ff	4d		M
	defb 020h		;1800	20		 
	defb 052h		;1801	52		R
	defb 04fh		;1802	4f		O
	defb 04dh		;1803	4d		M
	defb 020h		;1804	20		 
	defb 053h		;1805	53		S
	defb 043h		;1806	43		C
	defb 052h		;1807	52		R
	defb 01bh		;1808	1b		.
l1809h:
	defb 000h		;1809	00		.
	defb 00bh		;180a	0b		.
	defb 05ah		;180b	5a		Z
	defb 058h		;180c	58		X
	defb 0a0h		;180d	a0		.
RegHeaderLine_end:
	bit 5,(iy-002h)		;180e	fd cb fe 6e	. . . n
	jr z,l1818h		;1812	28 04		( .
	rst 20h			;1814	e7		.
	or h			;1815	b4		.
	jr $+5			;1816	18 03		. .
l1818h:
	rst 20h			;1818	e7		.
	ld sp,0e7b2h		;1819	31 b2 e7	1 . .
	jr c,l1809h		;181c	38 eb		8 .
	ld (ix+000h),000h	;181e	dd 36 00 00	. 6 . .
	ld (ix+001h),004h	;1822	dd 36 01 04	. 6 . .
	ld hl,(0dd84h)		;1826	2a 84 dd	* . .
	call PrintHexWord	;1829	cd 95 1d	. . .
	inc (ix+000h)		;182c	dd 34 00	. 4 .
	ld (ix+001h),000h	;182f	dd 36 01 00	. 6 . .
	call ShowRegisters	;1833	cd a2 18	. . .
	ld (ix+000h),009h	;1836	dd 36 00 09	. 6 . .
	call RegisterNameTable	;183a	cd 28 19	. ( .
	ld hl,0dfe9h		;183d	21 e9 df	! . .
	set 1,(hl)		;1840	cb ce		. .
	set 2,(hl)		;1842	cb d6		. .
	ld (ix+000h),00bh	;1844	dd 36 00 0b	. 6 . .
	ld hl,l12a9h		;1848	21 a9 12	! . .
	ld a,(UserIff)		;184b	3a 83 dd	: . .
	and 004h		;184e	e6 04		. .
	jr nz,l1855h		;1850	20 03		  .
	ld hl,l12abh		;1852	21 ab 12	! . .
l1855h:
	call sub_1c8ah		;1855	cd 8a 1c	. . .
	call PrintSpace		;1858	cd 92 0c	. . .
	ld (ix+001h),005h	;185b	dd 36 01 05	. 6 . .
	ld a,(PagingState)	;185f	3a dd df	: . .
	and 007h		;1862	e6 07		. .
	ld c,a			;1864	4f		O
	ld a,(0dfdeh)		;1865	3a de df	: . .
	rrca			;1868	0f		.
	and 008h		;1869	e6 08		. .
	or c			;186b	b1		.
	call sub_1da3h		;186c	cd a3 1d	. . .
	ld (ix+001h),009h	;186f	dd 36 01 09	. 6 . .
	ld a,(PagingState)	;1873	3a dd df	: . .
	rrca			;1876	0f		.
	rrca			;1877	0f		.
	rrca			;1878	0f		.
	rrca			;1879	0f		.
	and 001h		;187a	e6 01		. .
	call sub_1da3h		;187c	cd a3 1d	. . .
	ld (ix+001h),00dh	;187f	dd 36 01 0d	. 6 . .
	ld c,035h		;1883	0e 35		. 5
	ld a,(PagingState)	;1885	3a dd df	: . .
	and 008h		;1888	e6 08		. .
	jr z,l188eh		;188a	28 02		( .
	ld c,037h		;188c	0e 37		. 7
l188eh:
	ld a,c			;188e	79		y
	rst 10h			;188f	d7		.
	ld (iy+006h),004h	;1890	fd 36 06 04	. 6 . .
	call sub_196ah		;1894	cd 6a 19	. j .
	call sub_1a50h		;1897	cd 50 1a	. P .
	ld hl,0dfe9h		;189a	21 e9 df	! . .
	ld a,(hl)		;189d	7e		~
	or 051h			;189e	f6 51		. Q
	ld (hl),a		;18a0	77		w
	ret			;18a1	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ShowRegisters: dump the saved context - register names are
; inline RST 20h strings at RegisterNameTable ($1928), values
; printed in the current number base; the flag byte comes out
; as eight binary digits.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ShowRegisters:
	bit 1,(iy+00ah)		;18a2	fd cb 0a 4e	. . . N
	ret nz			;18a6	c0		.
	ld hl,l129ah		;18a7	21 9a 12	! . .
	ld c,(iy+00ch)		;18aa	fd 4e 0c	. N .
	ld b,007h		;18ad	06 07		. .
	ld de,UserPc		;18af	11 6b dd	. k .
	jr l18b7h		;18b2	18 03		. .
l18b4h:
	inc (ix+000h)		;18b4	dd 34 00	. 4 .
l18b7h:
	ld a,003h		;18b7	3e 03		> .
	cp b			;18b9	b8		.
	jr nz,l18c5h		;18ba	20 09		  .
	bit 0,(iy+00bh)		;18bc	fd cb 0b 46	. . . F
	jr z,l18c5h		;18c0	28 03		( .
	ld de,0dd79h		;18c2	11 79 dd	. y .
l18c5h:
	sla c			;18c5	cb 21		. !
	jr nc,l18cdh		;18c7	30 04		0 .
	inc hl			;18c9	23		#
	inc hl			;18ca	23		#
	jr l1911h		;18cb	18 44		. D
l18cdh:
	bit 2,(iy+00ah)		;18cd	fd cb 0a 56	. . . V
	jr nz,l18e0h		;18d1	20 0d		  .
	call sub_1c8ah		;18d3	cd 8a 1c	. . .
	ld a,003h		;18d6	3e 03		> .
	cp b			;18d8	b8		.
	jr c,l18e2h		;18d9	38 07		8 .
	call sub_191ah		;18db	cd 1a 19	. . .
	jr l18e2h		;18de	18 02		. .
l18e0h:
	inc hl			;18e0	23		#
	inc hl			;18e1	23		#
l18e2h:
	ld (ix+001h),004h	;18e2	dd 36 01 04	. 6 . .
	push hl			;18e6	e5		.
	push de			;18e7	d5		.
	ex de,hl		;18e8	eb		.
	ld a,(hl)		;18e9	7e		~
	inc hl			;18ea	23		#
	ld h,(hl)		;18eb	66		f
	ld l,a			;18ec	6f		o
	call PrintHexWord	;18ed	cd 95 1d	. . .
	bit 3,(iy+00ah)		;18f0	fd cb 0a 5e	. . . ^
	jr nz,l190fh		;18f4	20 19		  .
	push bc			;18f6	c5		.
	ld bc,l0003h+1		;18f7	01 04 00	. . .
	ld de,0ddach		;18fa	11 ac dd	. . .
	push de			;18fd	d5		.
	call CopyAcrossBanks	;18fe	cd 46 06	. F .
	pop hl			;1901	e1		.
	ld b,004h		;1902	06 04		. .
l1904h:
	inc (ix+001h)		;1904	dd 34 01	. 4 .
	ld a,(hl)		;1907	7e		~
	call PrintHexByte	;1908	cd 9a 1d	. . .
	inc hl			;190b	23		#
	djnz l1904h		;190c	10 f6		. .
	pop bc			;190e	c1		.
l190fh:
	pop de			;190f	d1		.
	pop hl			;1910	e1		.
l1911h:
	inc de			;1911	13		.
	inc de			;1912	13		.
	ld (ix+001h),000h	;1913	dd 36 01 00	. 6 . .
	djnz l18b4h		;1917	10 9b		. .
	ret			;1919	c9		.
sub_191ah:
	bit 0,(iy+00bh)		;191a	fd cb 0b 46	. . . F
sub_191eh:
	ld a,020h		;191e	3e 20		>  
	jp z,PrintChar		;1920	ca 94 0c	. . .
	ld a,027h		;1923	3e 27		> '
	jp PrintChar		;1925	c3 94 0c	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegisterNameTable: inline register-name strings (bit-7
; terminated, printed by RST 20h) for the dump above.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RegisterNameTable:
	ld b,(iy+00ah)		;1928	fd 46 0a	. F .
	bit 1,b			;192b	cb 48		. H
	ret nz			;192d	c0		.
	bit 0,(iy+00ch)		;192e	fd cb 0c 46	. . . F
	ret nz			;1932	c0		.
	bit 2,b			;1933	cb 50		. P
	ld b,(iy+00bh)		;1935	fd 46 0b	. F .
	jr nz,l1942h		;1938	20 08		  .
	rst 20h			;193a	e7		.
	ld b,c			;193b	41		A
	add a,0cbh		;193c	c6 cb		. .
	ld c,b			;193e	48		H
	call sub_191eh		;193f	cd 1e 19	. . .
l1942h:
	ld (ix+001h),004h	;1942	dd 36 01 04	. 6 . .
	bit 1,b			;1946	cb 48		. H
	ld hl,(0dd7fh)		;1948	2a 7f dd	* . .
	jr z,l1950h		;194b	28 03		( .
	ld hl,(0dd81h)		;194d	2a 81 dd	* . .
l1950h:
	call PrintHexWord	;1950	cd 95 1d	. . .
	ld (ix+001h),00ah	;1953	dd 36 01 0a	. 6 . .
sub_1957h:
	ld b,008h		;1957	06 08		. .
l1959h:
	ld a,030h		;1959	3e 30		> 0
	sla l			;195b	cb 25		. %
	jr nc,l1960h		;195d	30 01		0 .
	inc a			;195f	3c		<
l1960h:
	call PrintChar		;1960	cd 94 0c	. . .
	djnz l1959h		;1963	10 f4		. .
	ld (ix+001h),000h	;1965	dd 36 01 00	. 6 . .
	ret			;1969	c9		.
sub_196ah:
	bit 4,(iy+00ah)		;196a	fd cb 0a 66	. . . f
	ret nz			;196e	c0		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MemoryDumpWindow - the D-command view: default work buffer,
; scroll check, prompt-window cursor placement, then eight rows
; of HexDumpRow; the ASCII half re-fetches every byte across
; banks.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MemoryDumpWindow:
	call SetDefaultWorkspace	;196f	cd 9a 17	. . .
	call ScrollWindow	;1972	cd de 19	. . .
	bit 5,(iy+00ah)		;1975	fd cb 0a 6e	. . . n
	jr nz,l198bh		;1979	20 10		  .
	ld hl,PromptWindowDef	;197b	21 35 e0	! 5 .
	rst 30h			;197e	f7		.
	ld (ix+000h),00bh	;197f	dd 36 00 0b	. 6 . .
	ld (ix+001h),015h	;1983	dd 36 01 15	. 6 . .
	ex de,hl		;1987	eb		.
	call DisasmLine		;1988	cd 92 1a	. . .
l198bh:
	call SetDefaultWorkspace	;198b	cd 9a 17	. . .
	call sub_0d1ch		;198e	cd 1c 0d	. . .
	ld hl,(0dd87h)		;1991	2a 87 dd	* . .
	ld b,008h		;1994	06 08		. .
l1996h:
	push bc			;1996	c5		.
	call HexDumpRow		;1997	cd fb 19	. . .
	inc (ix+000h)		;199a	dd 34 00	. 4 .
	pop bc			;199d	c1		.
	djnz l1996h		;199e	10 f6		. .
	ld bc,l3e3ch		;19a0	01 3c 3e	. < >
	ld hl,(0dd69h)		;19a3	2a 69 dd	* i .
l19a6h:
	call PositionCursor	;19a6	cd bf 19	. . .
	ld a,b			;19a9	78		x
	call PrintChar		;19aa	cd 94 0c	. . .
	inc (ix+001h)		;19ad	dd 34 01	. 4 .
	inc (ix+001h)		;19b0	dd 34 01	. 4 .
	ld a,c			;19b3	79		y
	jp PrintChar		;19b4	c3 94 0c	. . .
sub_19b7h:
	ld bc,l201fh+1		;19b7	01 20 20	.    
	ld hl,(0dd89h)		;19ba	2a 89 dd	* . .
	jr l19a6h		;19bd	18 e7		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PositionCursor: translate the dump address (DD87/DD89) into
; window cursor coordinates (IX+0/IX+1).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PositionCursor:
	ld (0dd89h),hl		;19bf	22 89 dd	" . .
	ld de,(0dd87h)		;19c2	ed 5b 87 dd	. [ . .
	or a			;19c6	b7		.
	sbc hl,de		;19c7	ed 52		. R
	ld a,l			;19c9	7d		}
	and 007h		;19ca	e6 07		. .
	ld h,a			;19cc	67		g
	add a,a			;19cd	87		.
	add a,h			;19ce	84		.
	add a,007h		;19cf	c6 07		. .
	ld (ix+001h),a		;19d1	dd 77 01	. w .
	ld a,l			;19d4	7d		}
	and 038h		;19d5	e6 38		. 8
	rra			;19d7	1f		.
	rra			;19d8	1f		.
	rra			;19d9	1f		.
	ld (ix+000h),a		;19da	dd 77 00	. w .
	ret			;19dd	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScrollWindow: scroll the current window when the cursor is
; about to leave the bottom edge.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScrollWindow:
	call sub_19b7h		;19de	cd b7 19	. . .
	ld hl,(0dd87h)		;19e1	2a 87 dd	* . .
	ld de,(0dd69h)		;19e4	ed 5b 69 dd	. [ i .
	ld bc,0003fh		;19e8	01 3f 00	. ? .
	or a			;19eb	b7		.
	sbc hl,de		;19ec	ed 52		. R
	ret z			;19ee	c8		.
	jr nc,l19f3h		;19ef	30 02		0 .
	add hl,bc		;19f1	09		.
	ret c			;19f2	d8		.
l19f3h:
	ld hl,0ffe8h		;19f3	21 e8 ff	! . .
	add hl,de		;19f6	19		.
	ld (0dd87h),hl		;19f7	22 87 dd	" . .
	ret			;19fa	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; HexDumpRow: one dump row - address word, 8 bytes in hex
; (fetched through the bank window), then the same 8 as ASCII.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
HexDumpRow:
	ld (ix+001h),002h	;19fb	dd 36 01 02	. 6 . .
l19ffh:
	call PrintHexWord	;19ff	cd 95 1d	. . .
	call sub_1a42h		;1a02	cd 42 1a	. B .
	call sub_1a42h		;1a05	cd 42 1a	. B .
	ld de,0ddach		;1a08	11 ac dd	. . .
	ld bc,Rst08Vector	;1a0b	01 08 00	. . .
	push de			;1a0e	d5		.
	call CopyAcrossBanks	;1a0f	cd 46 06	. F .
	pop de			;1a12	d1		.
	push de			;1a13	d5		.
	ld b,008h		;1a14	06 08		. .
	call sub_1a27h		;1a16	cd 27 1a	. ' .
	pop de			;1a19	d1		.
	call sub_1a42h		;1a1a	cd 42 1a	. B .
	ld b,008h		;1a1d	06 08		. .
	call sub_1a32h		;1a1f	cd 32 1a	. 2 .
	ld (ix+001h),000h	;1a22	dd 36 01 00	. 6 . .
	ret			;1a26	c9		.
sub_1a27h:
	ld a,(de)		;1a27	1a		.
	inc de			;1a28	13		.
	call PrintHexByte	;1a29	cd 9a 1d	. . .
	call sub_1a42h		;1a2c	cd 42 1a	. B .
	djnz sub_1a27h		;1a2f	10 f6		. .
	ret			;1a31	c9		.
sub_1a32h:
	ld a,(de)		;1a32	1a		.
	inc de			;1a33	13		.
	and 07fh		;1a34	e6 7f		. .
	cp 020h			;1a36	fe 20		.  
	jr nc,l1a3ch		;1a38	30 02		0 .
	ld a,02eh		;1a3a	3e 2e		> .
l1a3ch:
	call PrintChar		;1a3c	cd 94 0c	. . .
	djnz sub_1a32h		;1a3f	10 f1		. .
	ret			;1a41	c9		.
sub_1a42h:
	inc (ix+001h)		;1a42	dd 34 01	. 4 .
	bit 4,(iy+009h)		;1a45	fd cb 09 66	. . . f
	ret z			;1a49	c8		.
	dec (ix+001h)		;1a4a	dd 35 01	. 5 .
	jp PrintSpace		;1a4d	c3 92 0c	. . .
sub_1a50h:
	ld b,(iy+00ah)		;1a50	fd 46 0a	. F .
	bit 6,b			;1a53	cb 70		. p
	ret nz			;1a55	c0		.
	bit 0,b			;1a56	cb 40		. @
	jr nz,l1a67h		;1a58	20 0d		  .
	ld hl,05830h		;1a5a	21 30 58	! 0 X
	ld (hl),030h		;1a5d	36 30		6 0
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DisasmWindow: the disassembly view.  Attributes through the
; $5831 copy, workspace E051; renders 5 lines (1 while
; stepping) from DD87/DD89, advancing by each decoded length.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DisasmWindow:
	ld bc,l000dh+2		;1a5f	01 0f 00	. . .
	ld de,05831h		;1a62	11 31 58	. 1 X
	ldir			;1a65	ed b0		. .
l1a67h:
	ld hl,DisasmWindowDef	;1a67	21 51 e0	! Q .
	rst 30h			;1a6a	f7		.
	bit 0,(iy+00ah)		;1a6b	fd cb 0a 46	. . . F
	call nz,ScrollUp	;1a6f	c4 61 0b	. a .
	ld (ix+000h),001h	;1a72	dd 36 00 01	. 6 . .
	ld (ix+001h),000h	;1a76	dd 36 01 00	. 6 . .
	ld hl,(UserPc)		;1a7a	2a 6b dd	* k .
	ld b,005h		;1a7d	06 05		. .
	bit 2,(iy+005h)		;1a7f	fd cb 05 56	. . . V
	jr z,l1a87h		;1a83	28 02		( .
	ld b,001h		;1a85	06 01		. .
l1a87h:
	push bc			;1a87	c5		.
	call DisasmLine		;1a88	cd 92 1a	. . .
	inc (ix+000h)		;1a8b	dd 34 00	. 4 .
	pop bc			;1a8e	c1		.
	djnz l1a87h		;1a8f	10 f6		. .
	ret			;1a91	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DisasmLine: disassemble one instruction and print it
; (address, opcode bytes, mnemonic via Disassemble).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DisasmLine:
	call PrintHexWord	;1a92	cd 95 1d	. . .
	call FetchPrefixBytes	;1a95	cd a8 1a	. . .
	push hl			;1a98	e5		.
	ld hl,0ddb7h		;1a99	21 b7 dd	! . .
	call Disassemble	;1a9c	cd 8f 1b	. . .
	call PadColumns		;1a9f	cd ff 1a	. . .
	pop hl			;1aa2	e1		.
	ld (ix+001h),000h	;1aa3	dd 36 01 00	. 6 . .
	ret			;1aa7	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FetchPrefixBytes: pull up to 4 opcode bytes for the
; disassembler across the bank window (DD/DDCB/FD/FDCB
; prefixes included).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FetchPrefixBytes:
	ld (0ddaah),hl		;1aa8	22 aa dd	" . .
	push hl			;1aab	e5		.
	ld de,0ddach		;1aac	11 ac dd	. . .
	ld bc,l0003h+1		;1aaf	01 04 00	. . .
	call CopyAcrossBanks	;1ab2	cd 46 06	. F .
	call AnalyzeDataPool_end	;1ab5	cd 49 20	. I  
	pop hl			;1ab8	e1		.
	add hl,bc		;1ab9	09		.
	ld (0ddaah),hl		;1aba	22 aa dd	" . .
	ret			;1abd	c9		.
sub_1abeh:
	bit 0,(iy+005h)		;1abe	fd cb 05 46	. . . F
	push af			;1ac2	f5		.
	call z,PrintHexWord	;1ac3	cc 95 1d	. . .
	pop af			;1ac6	f1		.
	ld c,l			;1ac7	4d		M
	ld b,h			;1ac8	44		D
	ld (iy+008h),020h	;1ac9	fd 36 08 20	. 6 .  
	call nz,sub_1d20h	;1acd	c4 20 1d	.   .
	call FetchPrefixBytes	;1ad0	cd a8 1a	. . .
	push hl			;1ad3	e5		.
	push bc			;1ad4	c5		.
	ld hl,0ddb7h		;1ad5	21 b7 dd	! . .
	call Disassemble	;1ad8	cd 8f 1b	. . .
	pop bc			;1adb	c1		.
	bit 1,(iy+005h)		;1adc	fd cb 05 4e	. . . N
	jr z,l1afdh		;1ae0	28 1b		( .
	ld a,017h		;1ae2	3e 17		> .
	call sub_1b03h		;1ae4	cd 03 1b	. . .
	ld a,03bh		;1ae7	3e 3b		> ;
	rst 10h			;1ae9	d7		.
	ld b,c			;1aea	41		A
	push bc			;1aeb	c5		.
	ld de,0ddach		;1aec	11 ac dd	. . .
	push de			;1aef	d5		.
	call sub_1a27h		;1af0	cd 27 1a	. ' .
	ld a,025h		;1af3	3e 25		> %
	call sub_1b03h		;1af5	cd 03 1b	. . .
	pop de			;1af8	d1		.
	pop bc			;1af9	c1		.
	call sub_1a32h		;1afa	cd 32 1a	. 2 .
l1afdh:
	pop hl			;1afd	e1		.
	ret			;1afe	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PadColumns: pad the listing to its fixed column layout.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PadColumns:
	ld a,(ix+00bh)		;1aff	dd 7e 0b	. ~ .
	dec a			;1b02	3d		=
sub_1b03h:
	ld b,(ix+001h)		;1b03	dd 46 01	. F .
	sub b			;1b06	90		.
	ret z			;1b07	c8		.
	ret c			;1b08	d8		.
	ld b,a			;1b09	47		G
	jp l1cabh		;1b0a	c3 ab 1c	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AutoRepeatKey: keys held down (state at KeyState, E000-E004)
; repeat with a ramp-up delay; feeds editor and dump windows.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AutoRepeatKey:
	call BlinkCursor	;1b0d	cd c5 0a	. . .
	call sub_07aah		;1b10	cd aa 07	. . .
	call sub_1b32h		;1b13	cd 32 1b	. 2 .
	push bc			;1b16	c5		.
	ld bc,(KeyState)	;1b17	ed 4b 00 e0	. K . .
l1b1bh:
	ld b,000h		;1b1b	06 00		. .
l1b1dh:
	djnz l1b1dh		;1b1d	10 fe		. .
	dec c			;1b1f	0d		.
	jr nz,l1b1bh		;1b20	20 f9		  .
	pop bc			;1b22	c1		.
	jr nc,AutoRepeatKey	;1b23	30 e8		0 .
	cp 0c5h			;1b25	fe c5		. .
	jr nz,l1b2bh		;1b27	20 02		  .
	ld a,05dh		;1b29	3e 5d		> ]
l1b2bh:
	cp 0c6h			;1b2b	fe c6		. .
	jr nz,l1b31h		;1b2d	20 02		  .
	ld a,05bh		;1b2f	3e 5b		> [
l1b31h:
	ret			;1b31	c9		.
sub_1b32h:
	jr c,l1b39h		;1b32	38 05		8 .
l1b34h:
	xor a			;1b34	af		.
	ld (0e004h),a		;1b35	32 04 e0	2 . .
	ret			;1b38	c9		.
l1b39h:
	push af			;1b39	f5		.
	call sub_07aah		;1b3a	cd aa 07	. . .
	pop hl			;1b3d	e1		.
	cp h			;1b3e	bc		.
	jr nz,l1b34h		;1b3f	20 f3		  .
	ld hl,0e004h		;1b41	21 04 e0	! . .
	cp (hl)			;1b44	be		.
	jr z,l1b53h		;1b45	28 0c		( .
	ld (hl),a		;1b47	77		w
	ld a,(0e001h)		;1b48	3a 01 e0	: . .
l1b4bh:
	ld (0e003h),a		;1b4b	32 03 e0	2 . .
	ld a,(0e004h)		;1b4e	3a 04 e0	: . .
	scf			;1b51	37		7
	ret			;1b52	c9		.
l1b53h:
	ld hl,0e003h		;1b53	21 03 e0	! . .
	dec (hl)		;1b56	35		5
	ret nz			;1b57	c0		.
	ld a,(0e002h)		;1b58	3a 02 e0	: . .
	jr l1b4bh		;1b5b	18 ee		. .
	xor a			;1b5d	af		.
	jr l1b62h		;1b5e	18 02		. .
	ld a,080h		;1b60	3e 80		> .
l1b62h:
	bit 1,(iy+012h)		;1b62	fd cb 12 4e	. . . N
	jr z,l1b6ah		;1b66	28 02		( .
	or 001h			;1b68	f6 01		. .
l1b6ah:
	ld (0dff5h),a		;1b6a	32 f5 df	2 . .
	call sub_17c3h		;1b6d	cd c3 17	. . .
	ld a,081h		;1b70	3e 81		> .
	ret			;1b72	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CharClasses: character-class predicates (hex digit,
; separator, ...) used by parsers and the disassembler.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CharClasses:
	cp 07eh			;1b73	fe 7e		. ~
	ret c			;1b75	d8		.
	cp 0c4h			;1b76	fe c4		. .
	ccf			;1b78	3f		?
	ret			;1b79	c9		.
sub_1b7ah:
	cp 02ch			;1b7a	fe 2c		. ,
	ret c			;1b7c	d8		.
	cp 081h			;1b7d	fe 81		. .
	ccf			;1b7f	3f		?
	ret			;1b80	c9		.
sub_1b81h:
	cp 02ch			;1b81	fe 2c		. ,
	ret c			;1b83	d8		.
	cp 0c4h			;1b84	fe c4		. .
	ccf			;1b86	3f		?
	ret			;1b87	c9		.
sub_1b88h:
	cp 001h			;1b88	fe 01		. .
	ret c			;1b8a	d8		.
	cp 02ch			;1b8b	fe 2c		. ,
	ccf			;1b8d	3f		?
	ret			;1b8e	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Disassemble - the mnemonic printer core.  Mnemonic strings
; sit in MnemonicTable ($2409) with high-bit placeholder
; characters marking where operands splice in; this walk
; decodes operands (16-bit, 8-bit, indexed, condition codes)
; and emits the line via RST 10h.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Disassemble:
	res 6,(iy+00eh)		;1b8f	fd cb 0e b6	. . . .
	call PrintSpace		;1b93	cd 92 0c	. . .
	ld a,(hl)		;1b96	7e		~
	call CharClasses	;1b97	cd 73 1b	. s .
	jr c,l1bb5h		;1b9a	38 19		8 .
	push hl			;1b9c	e5		.
	sub 07eh		;1b9d	d6 7e		. ~
	add a,a			;1b9f	87		.
	ld hl,MnemonicTable	;1ba0	21 09 24	! . $
	ld e,a			;1ba3	5f		_
	ld d,000h		;1ba4	16 00		. .
	add hl,de		;1ba6	19		.
	ld a,(hl)		;1ba7	7e		~
	call sub_1bedh		;1ba8	cd ed 1b	. . .
	ld a,(hl)		;1bab	7e		~
	and 07fh		;1bac	e6 7f		. .
	pop hl			;1bae	e1		.
	inc hl			;1baf	23		#
	call sub_1bfch		;1bb0	cd fc 1b	. . .
	jr l1bc0h		;1bb3	18 0b		. .
l1bb5h:
	call sub_1b7ah		;1bb5	cd 7a 1b	. z .
	jr c,l1bc5h		;1bb8	38 0b		8 .
	call sub_1bedh		;1bba	cd ed 1b	. . .
l1bbdh:
	call sub_1c16h		;1bbd	cd 16 1c	. . .
l1bc0h:
	ld a,(hl)		;1bc0	7e		~
	call sub_1b81h		;1bc1	cd 81 1b	. . .
	ret nc			;1bc4	d0		.
l1bc5h:
	cp 0f1h			;1bc5	fe f1		. .
	ret z			;1bc7	c8		.
	cp 0f7h			;1bc8	fe f7		. .
	ret z			;1bca	c8		.
	cp 0eeh			;1bcb	fe ee		. .
	jr nz,l1bdeh		;1bcd	20 0f		  .
	call PrintSpace		;1bcf	cd 92 0c	. . .
	ld a,03bh		;1bd2	3e 3b		> ;
	rst 10h			;1bd4	d7		.
	inc hl			;1bd5	23		#
l1bd6h:
	ld a,(hl)		;1bd6	7e		~
	inc hl			;1bd7	23		#
	cp 0f0h			;1bd8	fe f0		. .
	ret z			;1bda	c8		.
	rst 10h			;1bdb	d7		.
	jr l1bd6h		;1bdc	18 f8		. .
l1bdeh:
	bit 6,(iy+00eh)		;1bde	fd cb 0e 76	. . . v
	set 6,(iy+00eh)		;1be2	fd cb 0e f6	. . . .
	jr nz,l1bbdh		;1be6	20 d5		  .
	ld a,02ch		;1be8	3e 2c		> ,
	rst 10h			;1bea	d7		.
	jr l1bbdh		;1beb	18 d0		. .
sub_1bedh:
	ld c,(iy+006h)		;1bed	fd 4e 06	. N .
	sub 02ch		;1bf0	d6 2c		. ,
	ld de,l24f7h		;1bf2	11 f7 24	. . $
	call sub_1cb4h		;1bf5	cd b4 1c	. . .
	inc hl			;1bf8	23		#
	jp PrintSpace		;1bf9	c3 92 0c	. . .
sub_1bfch:
	call sub_1b88h		;1bfc	cd 88 1b	. . .
	jr c,l1c1ah		;1bff	38 19		8 .
	push af			;1c01	f5		.
	sub 001h		;1c02	d6 01		. .
	ld de,l2496h		;1c04	11 96 24	. . $
	call sub_1cb2h		;1c07	cd b2 1c	. . .
	pop af			;1c0a	f1		.
	cp 027h			;1c0b	fe 27		. '
	jr c,l1c54h		;1c0d	38 45		8 E
	call sub_1c16h		;1c0f	cd 16 1c	. . .
	ld a,029h		;1c12	3e 29		> )
	rst 10h			;1c14	d7		.
	ret			;1c15	c9		.
sub_1c16h:
	ld a,(hl)		;1c16	7e		~
	inc hl			;1c17	23		#
	jr sub_1bfch		;1c18	18 e2		. .
l1c1ah:
	cp 0f8h			;1c1a	fe f8		. .
	jr c,l1c23h		;1c1c	38 05		8 .
	call PickNumberBase	;1c1e	cd dd 1c	. . .
	jr l1c54h		;1c21	18 31		. 1
l1c23h:
	cp 0ech			;1c23	fe ec		. .
	jr nz,l1c2ch		;1c25	20 05		  .
	call PrintJustifiedHeader	;1c27	cd 93 1c	. . .
	jr l1c54h		;1c2a	18 28		. (
l1c2ch:
	cp 0e9h			;1c2c	fe e9		. .
	jr nz,l1c35h		;1c2e	20 05		  .
	ld a,024h		;1c30	3e 24		> $
	rst 10h			;1c32	d7		.
	jr l1c54h		;1c33	18 1f		. .
l1c35h:
	cp 0ebh			;1c35	fe eb		. .
	jr nz,l1c41h		;1c37	20 08		  .
	ld a,022h		;1c39	3e 22		> "
	rst 10h			;1c3b	d7		.
	call ScanQuotedString	;1c3c	cd 56 1c	. V .
	jr l1c51h		;1c3f	18 10		. .
l1c41h:
	cp 0edh			;1c41	fe ed		. .
	dec hl			;1c43	2b		+
	ret nz			;1c44	c0		.
	inc hl			;1c45	23		#
	ld a,022h		;1c46	3e 22		> "
	rst 10h			;1c48	d7		.
l1c49h:
	call ScanQuotedString	;1c49	cd 56 1c	. V .
	ld a,(hl)		;1c4c	7e		~
	or a			;1c4d	b7		.
	jr nz,l1c49h		;1c4e	20 f9		  .
	inc hl			;1c50	23		#
l1c51h:
	ld a,022h		;1c51	3e 22		> "
	rst 10h			;1c53	d7		.
l1c54h:
	ld a,(hl)		;1c54	7e		~
	ret			;1c55	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScanQuotedString: assembler string literals - backslash
; escapes and quotes handled while copying to the work buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScanQuotedString:
	ld a,(hl)		;1c56	7e		~
	ld c,a			;1c57	4f		O
	inc hl			;1c58	23		#
	cp 05ch			;1c59	fe 5c		. \
	jr z,l1c68h		;1c5b	28 0b		( .
	cp 022h			;1c5d	fe 22		. "
	jr z,l1c68h		;1c5f	28 07		( .
	cp 0f8h			;1c61	fe f8		. .
	jr nc,l1c6fh		;1c63	30 0a		0 .
	jp PrintChar		;1c65	c3 94 0c	. . .
l1c68h:
	ld a,05ch		;1c68	3e 5c		> \
	rst 10h			;1c6a	d7		.
	ld a,c			;1c6b	79		y
	jp PrintChar		;1c6c	c3 94 0c	. . .
l1c6fh:
	ld a,05ch		;1c6f	3e 5c		> \
	rst 10h			;1c71	d7		.
	ld a,c			;1c72	79		y
	jp PickNumberBase	;1c73	c3 dd 1c	. . .
l1c76h:
	ld a,(hl)		;1c76	7e		~
	rst 10h			;1c77	d7		.
	inc hl			;1c78	23		#
	djnz l1c76h		;1c79	10 fb		. .
	ret			;1c7b	c9		.
l1c7ch:
	ld a,(hl)		;1c7c	7e		~
	cp 020h			;1c7d	fe 20		.  
	jr nc,l1c83h		;1c7f	30 02		0 .
	ld a,03fh		;1c81	3e 3f		> ?
l1c83h:
	call PrintCharBit7	;1c83	cd 8d 0c	. . .
	inc hl			;1c86	23		#
	djnz l1c7ch		;1c87	10 f3		. .
	ret			;1c89	c9		.
sub_1c8ah:
	ld a,(hl)		;1c8a	7e		~
	call PrintCharBit7	;1c8b	cd 8d 0c	. . .
	inc hl			;1c8e	23		#
	rlca			;1c8f	07		.
	ret c			;1c90	d8		.
	jr sub_1c8ah		;1c91	18 f7		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintJustifiedHeader: window headers padded to window width.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintJustifiedHeader:
	ld b,001h		;1c93	06 01		. .
sub_1c95h:
	or a			;1c95	b7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintBit7Chars: print a string whose last character carries
; bit 7 (the RST 20h message convention).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintBit7Chars:
	ld a,(hl)		;1c96	7e		~
	bit 7,a			;1c97	cb 7f		. .
	call PrintCharBit7	;1c99	cd 8d 0c	. . .
	inc hl			;1c9c	23		#
	jr z,l1ca4h		;1c9d	28 05		( .
	ret c			;1c9f	d8		.
	ld a,020h		;1ca0	3e 20		>  
	jr l1caeh		;1ca2	18 0a		. .
l1ca4h:
	djnz PrintBit7Chars	;1ca4	10 f0		. .
	scf			;1ca6	37		7
	ld b,000h		;1ca7	06 00		. .
	jr PrintBit7Chars	;1ca9	18 eb		. .
l1cabh:
	ld a,020h		;1cab	3e 20		>  
l1cadh:
	rst 10h			;1cad	d7		.
l1caeh:
	djnz l1cadh		;1cae	10 fd		. .
	or a			;1cb0	b7		.
	ret			;1cb1	c9		.
sub_1cb2h:
	ld c,001h		;1cb2	0e 01		. .
sub_1cb4h:
	ex de,hl		;1cb4	eb		.
	call sub_1cbeh		;1cb5	cd be 1c	. . .
	ld b,c			;1cb8	41		A
	call sub_1c95h		;1cb9	cd 95 1c	. . .
	ex de,hl		;1cbc	eb		.
	ret			;1cbd	c9		.
sub_1cbeh:
	or a			;1cbe	b7		.
	ret z			;1cbf	c8		.
	ld b,a			;1cc0	47		G
l1cc1h:
	bit 7,(hl)		;1cc1	cb 7e		. ~
	inc hl			;1cc3	23		#
	jr z,l1cc1h		;1cc4	28 fb		( .
	djnz l1cc1h		;1cc6	10 f9		. .
	ret			;1cc8	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintMarkerChar: the listing marker column - "#" or a
; character derived from the address value (IY+05 bit 0
; selects the mode).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintMarkerChar:
	ld c,l			;1cc9	4d		M
	ld b,h			;1cca	44		D
	push de			;1ccb	d5		.
	bit 0,(iy+005h)		;1ccc	fd cb 05 46	. . . F
	ld de,l0a00h		;1cd0	11 00 0a	. . .
	jr nz,l1cd8h		;1cd3	20 03		  .
	ld de,l1023h		;1cd5	11 23 10	. # .
l1cd8h:
	call sub_1ce3h		;1cd8	cd e3 1c	. . .
	pop de			;1cdb	d1		.
	ret			;1cdc	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PickNumberBase: choose hex/decimal/binary presentation from
; the monitor flags before printing numbers.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PickNumberBase:
	call sub_1cefh		;1cdd	cd ef 1c	. . .
sub_1ce0h:
	call sub_1cf9h		;1ce0	cd f9 1c	. . .
sub_1ce3h:
	ld a,d			;1ce3	7a		z
	ld (0dfefh),a		;1ce4	32 ef df	2 . .
	ld a,e			;1ce7	7b		{
	rst 10h			;1ce8	d7		.
	ld (iy+007h),001h	;1ce9	fd 36 07 01	. 6 . .
	jr l1d2ah		;1ced	18 3b		. ;
sub_1cefh:
	ld b,000h		;1cef	06 00		. .
	ld c,(hl)		;1cf1	4e		N
	inc hl			;1cf2	23		#
	bit 0,a			;1cf3	cb 47		. G
	ret nz			;1cf5	c0		.
	ld b,(hl)		;1cf6	46		F
	inc hl			;1cf7	23		#
	ret			;1cf8	c9		.
sub_1cf9h:
	and 006h		;1cf9	e6 06		. .
	ld de,00225h		;1cfb	11 25 02	. % .
	cp 002h			;1cfe	fe 02		. .
	ret c			;1d00	d8		.
	ld de,l083eh+2		;1d01	11 40 08	. @ .
	ret z			;1d04	c8		.
	cp 006h			;1d05	fe 06		. .
	ld de,l0a00h		;1d07	11 00 0a	. . .
	ret c			;1d0a	d8		.
	ld de,l1023h		;1d0b	11 23 10	. # .
	ret			;1d0e	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintDecimal: recursive divider method - DE=$000A, divide,
; print remainders (recursion state at IY+07).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintDecimal:
	ld c,a			;1d0f	4f		O
	ld a,b			;1d10	78		x
	ld b,000h		;1d11	06 00		. .
	jr l1d22h		;1d13	18 0d		. .
sub_1d15h:
	scf			;1d15	37		7
	jr l1d19h		;1d16	18 01		. .
sub_1d18h:
	or a			;1d18	b7		.
l1d19h:
	ld a,020h		;1d19	3e 20		>  
	rst 10h			;1d1b	d7		.
	ld a,001h		;1d1c	3e 01		> .
	jr c,l1d22h		;1d1e	38 02		8 .
sub_1d20h:
	ld a,005h		;1d20	3e 05		> .
l1d22h:
	ld (iy+007h),a		;1d22	fd 77 07	. w .
sub_1d25h:
	ld de,Rst08Vector+2	;1d25	11 0a 00	. . .
	jr l1d2eh		;1d28	18 04		. .
l1d2ah:
	ld de,(0dfefh)		;1d2a	ed 5b ef df	. [ . .
l1d2eh:
	push hl			;1d2e	e5		.
	call sub_1d38h		;1d2f	cd 38 1d	. 8 .
	pop hl			;1d32	e1		.
	ret			;1d33	c9		.
sub_1d34h:
	ld a,b			;1d34	78		x
	or c			;1d35	b1		.
	jr z,l1d55h		;1d36	28 1d		( .
sub_1d38h:
	dec (iy+007h)		;1d38	fd 35 07	. 5 .
	ld a,b			;1d3b	78		x
	call Divide16		;1d3c	cd 61 1d	. a .
	push hl			;1d3f	e5		.
	call sub_1d34h		;1d40	cd 34 1d	. 4 .
	pop hl			;1d43	e1		.
	ld a,l			;1d44	7d		}
	add a,090h		;1d45	c6 90		. .
	daa			;1d47	27		'
	adc a,040h		;1d48	ce 40		. @
	daa			;1d4a	27		'
	and 07fh		;1d4b	e6 7f		. .
	cp 020h			;1d4d	fe 20		.  
	jr nc,l1d53h		;1d4f	30 02		0 .
	ld a,020h		;1d51	3e 20		>  
l1d53h:
	rst 10h			;1d53	d7		.
	ret			;1d54	c9		.
l1d55h:
	dec (iy+007h)		;1d55	fd 35 07	. 5 .
	ret m			;1d58	f8		.
	call sub_1d34h		;1d59	cd 34 1d	. 4 .
	ld a,(iy+008h)		;1d5c	fd 7e 08	. ~ .
	rst 10h			;1d5f	d7		.
	ret			;1d60	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Divide16: 16-bit division helper behind all number output.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Divide16:
	ld hl,WriteAnyBankByte	;1d61	21 00 00	! . .
	ld b,010h		;1d64	06 10		. .
l1d66h:
	rl c			;1d66	cb 11		. .
	rla			;1d68	17		.
	adc hl,hl		;1d69	ed 6a		. j
	sbc hl,de		;1d6b	ed 52		. R
	ccf			;1d6d	3f		?
	jr nc,l1d7fh		;1d6e	30 0f		0 .
l1d70h:
	djnz l1d66h		;1d70	10 f4		. .
	rl c			;1d72	cb 11		. .
	rla			;1d74	17		.
	ld b,a			;1d75	47		G
	ret			;1d76	c9		.
l1d77h:
	rl c			;1d77	cb 11		. .
	rla			;1d79	17		.
	adc hl,hl		;1d7a	ed 6a		. j
	add hl,de		;1d7c	19		.
	jr c,l1d70h		;1d7d	38 f1		8 .
l1d7fh:
	djnz l1d77h		;1d7f	10 f6		. .
	rl c			;1d81	cb 11		. .
	rla			;1d83	17		.
	add hl,de		;1d84	19		.
	ld b,a			;1d85	47		G
	ret			;1d86	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Rst20Handler - print inline message: EX (SP),HL swaps the
; return address into HL (pointing at the inline text), prints
; until a bit-7 character, then stacks the corrected resume
; address - the classic ZX-ROM inline-print trick, on RST 20h.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst20Handler:
	ex (sp),hl		;1d87	e3		.
	push af			;1d88	f5		.
l1d89h:
	ld a,(hl)		;1d89	7e		~
	call PrintCharBit7	;1d8a	cd 8d 0c	. . .
	and 080h		;1d8d	e6 80		. .
	inc hl			;1d8f	23		#
	jr z,l1d89h		;1d90	28 f7		( .
	pop af			;1d92	f1		.
	ex (sp),hl		;1d93	e3		.
	ret			;1d94	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintHexWord / PrintHexByte ($1D9A): nibble conversion via
; the DAA trick (add $90, daa, adc $40, daa per nibble).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintHexWord:
	ld a,h			;1d95	7c		|
	call PrintHexByte	;1d96	cd 9a 1d	. . .
	ld a,l			;1d99	7d		}
PrintHexByte:
	push af			;1d9a	f5		.
	rlca			;1d9b	07		.
	rlca			;1d9c	07		.
	rlca			;1d9d	07		.
	rlca			;1d9e	07		.
	call sub_1da3h		;1d9f	cd a3 1d	. . .
	pop af			;1da2	f1		.
sub_1da3h:
	and 00fh		;1da3	e6 0f		. .
	add a,090h		;1da5	c6 90		. .
	daa			;1da7	27		'
	adc a,040h		;1da8	ce 40		. @
	daa			;1daa	27		'
	rst 10h			;1dab	d7		.
	ret			;1dac	c9		.
sub_1dadh:
	ld l,(iy+002h)		;1dad	fd 6e 02	. n .
	jr l1dfbh		;1db0	18 49		. I
sub_1db2h:
	ld l,001h		;1db2	2e 01		. .
	jr l1dfbh		;1db4	18 45		. E
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintExtMenu: walk the extension menu table at ExtMenuTable
; ($E7F7) calling the RAM hook at $EB06 per entry - the
; documented way for RAM extensions to add monitor commands.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintExtMenu:
	bit 4,(iy+014h)		;1db6	fd cb 14 66	. . . f
	ret z			;1dba	c8		.
	ld hl,ExtMenuTable	;1dbb	21 f7 e7	! . .
	xor a			;1dbe	af		.
l1dbfh:
	push af			;1dbf	f5		.
	ld e,(hl)		;1dc0	5e		^
	inc hl			;1dc1	23		#
	ld d,(hl)		;1dc2	56		V
	inc hl			;1dc3	23		#
	push hl			;1dc4	e5		.
	ld a,d			;1dc5	7a		z
	or e			;1dc6	b3		.
	call nz,0eb06h		;1dc7	c4 06 eb	. . .
	pop hl			;1dca	e1		.
	pop af			;1dcb	f1		.
	inc a			;1dcc	3c		<
	cp 00ah			;1dcd	fe 0a		. .
	jr nz,l1dbfh		;1dcf	20 ee		  .
	ret			;1dd1	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrepareStep: swap the register file with the step context at
; StepContext (E0AA), park the monitor SP at $E2B5, arm the
; trace enable (StepFlags bit 5), then let the class patchers
; below adjust the step context for the next instruction.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrepareStep:
	ld hl,(UserPc)		;1dd2	2a 6b dd	* k .
	push hl			;1dd5	e5		.
	ld hl,(StepContext)	;1dd6	2a aa e0	* . .
	ld (UserPc),hl		;1dd9	22 6b dd	" k .
	ld hl,l1df1h		;1ddc	21 f1 1d	! . .
	push hl			;1ddf	e5		.
	ld (MonitorStack),sp	;1de0	ed 73 b5 e2	. s . .
	ld hl,StepFlags		;1de4	21 f1 df	! . .
	set 5,(hl)		;1de7	cb ee		. .
	bit 3,(hl)		;1de9	cb 5e		. ^
	call nz,sub_04cch	;1deb	c4 cc 04	. . .
	jp l0122h		;1dee	c3 22 01	. " .
l1df1h:
	res 5,(iy+012h)		;1df1	fd cb 12 ae	. . . .
	pop hl			;1df5	e1		.
	ld (UserPc),hl		;1df6	22 6b dd	" k .
sub_1df9h:
	ld l,0ffh		;1df9	2e ff		. .
l1dfbh:
	ld a,(0dd84h)		;1dfb	3a 84 dd	: . .
	ld h,a			;1dfe	67		g
	add a,l			;1dff	85		.
	rlca			;1e00	07		.
	sla h			;1e01	cb 24		. $
	rra			;1e03	1f		.
	ld (0dd84h),a		;1e04	32 84 dd	2 . .
	ret			;1e07	c9		.
sub_1e08h:
	ld hl,(StepContext)	;1e08	2a aa e0	* . .
	inc hl			;1e0b	23		#
	inc hl			;1e0c	23		#
	ld (hl),003h		;1e0d	36 03		6 .
	inc hl			;1e0f	23		#
	ld (hl),000h		;1e10	36 00		6 .
	jr l1e3ch		;1e12	18 28		. (
sub_1e14h:
	ld hl,(0ddaah)		;1e14	2a aa dd	* . .
	rst 28h			;1e17	ef		.
	ld e,a			;1e18	5f		_
	inc hl			;1e19	23		#
	rst 28h			;1e1a	ef		.
	ld d,a			;1e1b	57		W
	inc hl			;1e1c	23		#
	ld (0ddaah),hl		;1e1d	22 aa dd	" . .
	ld hl,(StepContext)	;1e20	2a aa e0	* . .
	inc hl			;1e23	23		#
	inc hl			;1e24	23		#
	ld (hl),e		;1e25	73		s
	inc hl			;1e26	23		#
	ld (hl),d		;1e27	72		r
	inc hl			;1e28	23		#
	ld (hl),0cfh		;1e29	36 cf		6 .
	jr PrepareStep		;1e2b	18 a5		. .
sub_1e2dh:
	ld hl,(StepContext)	;1e2d	2a aa e0	* . .
	inc hl			;1e30	23		#
	inc hl			;1e31	23		#
	push hl			;1e32	e5		.
	ld de,l0003h+1		;1e33	11 04 00	. . .
	add hl,de		;1e36	19		.
	ex de,hl		;1e37	eb		.
	pop hl			;1e38	e1		.
	ld (hl),e		;1e39	73		s
	inc hl			;1e3a	23		#
	ld (hl),d		;1e3b	72		r
l1e3ch:
	inc hl			;1e3c	23		#
	ld (hl),0b7h		;1e3d	36 b7		6 .
	inc hl			;1e3f	23		#
	ld (hl),0cfh		;1e40	36 cf		6 .
	inc hl			;1e42	23		#
	ld (hl),037h		;1e43	36 37		6 7
	inc hl			;1e45	23		#
	ld (hl),0cfh		;1e46	36 cf		6 .
	ld a,(0dd7fh)		;1e48	3a 7f dd	: . .
	push af			;1e4b	f5		.
	call PrepareStep	;1e4c	cd d2 1d	. . .
	call sub_1df9h		;1e4f	cd f9 1d	. . .
	ld a,(0dd7fh)		;1e52	3a 7f dd	: . .
	rrca			;1e55	0f		.
	pop hl			;1e56	e1		.
	ld a,h			;1e57	7c		|
	ld (0dd7fh),a		;1e58	32 7f dd	2 . .
	ret			;1e5b	c9		.
SwapAltRegs:
	ld hl,RegisterFile	;1e5c	21 73 dd	! s .
	ld de,0dd79h		;1e5f	11 79 dd	. y .
	ld b,006h		;1e62	06 06		. .
l1e64h:
	ld a,(de)		;1e64	1a		.
	ld c,(hl)		;1e65	4e		N
	ex de,hl		;1e66	eb		.
	ld (de),a		;1e67	12		.
	ld (hl),c		;1e68	71		q
	inc de			;1e69	13		.
	inc hl			;1e6a	23		#
	djnz l1e64h		;1e6b	10 f7		. .
	ret			;1e6d	c9		.
sub_1e6eh:
	ld hl,0dd7fh		;1e6e	21 7f dd	! . .
	ld de,0dd81h		;1e71	11 81 dd	. . .
	ld b,002h		;1e74	06 02		. .
	jr l1e64h		;1e76	18 ec		. .
l1e78h:
	ld de,Rst08Vector	;1e78	11 08 00	. . .
	ld hl,(StepPc)		;1e7b	2a d0 dd	* . .
	or a			;1e7e	b7		.
	sbc hl,de		;1e7f	ed 52		. R
	jr nz,l1ec1h		;1e81	20 3e		  >
	ld hl,(UserPc)		;1e83	2a 6b dd	* k .
	ld e,0ffh		;1e86	1e ff		. .
	jp l01cdh		;1e88	c3 cd 01	. . .
l1e8bh:
	ld a,(UserIff)		;1e8b	3a 83 dd	: . .
	bit 2,a			;1e8e	cb 57		. W
	jr nz,l1ea4h		;1e90	20 12		  .
	ld a,0abh		;1e92	3e ab		> .
	jp l0df8h		;1e94	c3 f8 0d	. . .
ToggleAltDisplay:
	ld a,(0ddb4h)		;1e97	3a b4 dd	: . .
	cp 008h			;1e9a	fe 08		. .
	push af			;1e9c	f5		.
	call z,sub_1e6eh	;1e9d	cc 6e 1e	. n .
	pop af			;1ea0	f1		.
	call nz,SwapAltRegs	;1ea1	c4 5c 1e	. \ .
l1ea4h:
	call sub_1db2h		;1ea4	cd b2 1d	. . .
	jp l1f8eh		;1ea7	c3 8e 1f	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BuildStepStub: build the one-instruction trampoline at
; StepStub (E2B7) - DI (or EI, per the saved user IFF, DD83
; bit 2), the instruction bytes fetched across banks, then $CF
; (RST 08) as the trap.  Running the stub executes exactly one
; user instruction with interrupts in their saved state.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BuildStepStub:
	ld hl,(UserPc)		;1eaa	2a 6b dd	* k .
	call sub_1fefh		;1ead	cd ef 1f	. . .
	jp nc,l1fc2h		;1eb0	d2 c2 1f	. . .
	call FetchPrefixBytes	;1eb3	cd a8 1a	. . .
	ld a,(0ddb7h)		;1eb6	3a b7 dd	: . .
	cp 032h			;1eb9	fe 32		. 2
	jr z,l1e8bh		;1ebb	28 ce		( .
	cp 072h			;1ebd	fe 72		. r
	jr z,l1e78h		;1ebf	28 b7		( .
l1ec1h:
	ld hl,(StepContext)	;1ec1	2a aa e0	* . .
	push hl			;1ec4	e5		.
	ld de,StepStub		;1ec5	11 b7 e2	. . .
	ld bc,Rst08Vector	;1ec8	01 08 00	. . .
	ldir			;1ecb	ed b0		. .
	pop de			;1ecd	d1		.
	ld a,(UserIff)		;1ece	3a 83 dd	: . .
	bit 2,a			;1ed1	cb 57		. W
	ld a,0f3h		;1ed3	3e f3		> .
	jr z,l1ed9h		;1ed5	28 02		( .
	ld a,0fbh		;1ed7	3e fb		> .
l1ed9h:
	ld (de),a		;1ed9	12		.
	inc de			;1eda	13		.
	ld hl,(UserPc)		;1edb	2a 6b dd	* k .
	ld c,(iy+002h)		;1ede	fd 4e 02	. N .
	ld b,000h		;1ee1	06 00		. .
	call CopyAcrossBanks	;1ee3	cd 46 06	. F .
	ld a,0cfh		;1ee6	3e cf		> .
	ld (de),a		;1ee8	12		.
	ld a,(iy+003h)		;1ee9	fd 7e 03	. ~ .
	or a			;1eec	b7		.
	jp z,l1f8bh		;1eed	ca 8b 1f	. . .
	bit 7,a			;1ef0	cb 7f		. .
	jr nz,ToggleAltDisplay	;1ef2	20 a3		  .
	bit 1,a			;1ef4	cb 4f		. O
	jr z,StepDispatch	;1ef6	28 1e		( .
	bit 5,a			;1ef8	cb 6f		. o
	jr nz,l1f0dh		;1efa	20 11		  .
	ld hl,(StepContext)	;1efc	2a aa e0	* . .
	inc hl			;1eff	23		#
	ld a,(hl)		;1f00	7e		~
	and 038h		;1f01	e6 38		. 8
	or 0c2h			;1f03	f6 c2		. .
	ld (hl),a		;1f05	77		w
	call sub_1e2dh		;1f06	cd 2d 1e	. - .
	jr nc,l1f1dh		;1f09	30 12		0 .
	jr l1f10h		;1f0b	18 03		. .
l1f0dh:
	call sub_1dadh		;1f0d	cd ad 1d	. . .
l1f10h:
	call sub_2e0eh		;1f10	cd 0e 2e	. . .
	ex de,hl		;1f13	eb		.
	jr l1f91h		;1f14	18 7b		. {
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; StepDispatch: after the trap - classify the executed
; instruction from the AnalyzeInstruction results (DDB4 area):
; HALT-like, jump taken, CALL entered, RET - and fix the saved
; user PC accordingly.  SetTempBreakpoint below makes CALLs
; stop at their callee.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
StepDispatch:
	bit 2,a			;1f16	cb 57		. W
	jr z,l1f21h		;1f18	28 07		( .
	call sub_1e08h		;1f1a	cd 08 1e	. . .
l1f1dh:
	jr nc,l1f8eh		;1f1d	30 6f		0 o
	jr l1f64h		;1f1f	18 43		. C
l1f21h:
	bit 0,a			;1f21	cb 47		. G
	jr z,l1f51h		;1f23	28 2c		( ,
	bit 7,(iy+016h)		;1f25	fd cb 16 7e	. . . ~
	jr nz,l1fa6h		;1f29	20 7b		  {
	call CheckTrace		;1f2b	cd e0 1f	. . .
	jr c,l1fabh		;1f2e	38 7b		8 {
l1f30h:
	ld hl,(StepContext)	;1f30	2a aa e0	* . .
	inc hl			;1f33	23		#
	ld a,(hl)		;1f34	7e		~
	bit 0,a			;1f35	cb 47		. G
	jr nz,l1f45h		;1f37	20 0c		  .
	and 038h		;1f39	e6 38		. 8
	or 0c2h			;1f3b	f6 c2		. .
	ld (hl),a		;1f3d	77		w
	call sub_1e2dh		;1f3e	cd 2d 1e	. - .
	jr nc,l1f8eh		;1f41	30 4b		0 K
	jr l1f48h		;1f43	18 03		. .
l1f45h:
	call sub_1db2h		;1f45	cd b2 1d	. . .
l1f48h:
	ld de,(0ddaah)		;1f48	ed 5b aa dd	. [ . .
	call sub_3577h		;1f4c	cd 77 35	. w 5
	jr l1f64h		;1f4f	18 13		. .
l1f51h:
	bit 3,a			;1f51	cb 5f		. _
	jr z,l1f69h		;1f53	28 14		( .
	call sub_1e2dh		;1f55	cd 2d 1e	. - .
	jr nc,l1f8eh		;1f58	30 34		0 4
	call CheckTrace		;1f5a	cd e0 1f	. . .
	push af			;1f5d	f5		.
	call c,sub_1df9h	;1f5e	dc f9 1d	. . .
	pop af			;1f61	f1		.
	jr c,SetTempBreakpoint	;1f62	38 61		8 a
l1f64h:
	ld hl,(StepPc)		;1f64	2a d0 dd	* . .
	jr l1f91h		;1f67	18 28		. (
l1f69h:
	bit 4,a			;1f69	cb 67		. g
	jr z,l1f86h		;1f6b	28 19		( .
	call sub_1db2h		;1f6d	cd b2 1d	. . .
	ld hl,(RegisterFile)	;1f70	2a 73 dd	* s .
	ld a,(iy+001h)		;1f73	fd 7e 01	. ~ .
	bit 1,a			;1f76	cb 4f		. O
	jr z,l1f91h		;1f78	28 17		( .
	ld hl,(0dd6fh)		;1f7a	2a 6f dd	* o .
	bit 0,a			;1f7d	cb 47		. G
	jr z,l1f91h		;1f7f	28 10		( .
	ld hl,(0dd71h)		;1f81	2a 71 dd	* q .
	jr l1f91h		;1f84	18 0b		. .
l1f86h:
	ld a,0aah		;1f86	3e aa		> .
	jp l0df8h		;1f88	c3 f8 0d	. . .
l1f8bh:
	call PrepareStep	;1f8b	cd d2 1d	. . .
l1f8eh:
	ld hl,(0ddaah)		;1f8e	2a aa dd	* . .
l1f91h:
	ld (UserPc),hl		;1f91	22 6b dd	" k .
	push hl			;1f94	e5		.
	ld hl,StepStub		;1f95	21 b7 e2	! . .
	ld de,(StepContext)	;1f98	ed 5b aa e0	. [ . .
	ld bc,Rst08Vector	;1f9c	01 08 00	. . .
	ldir			;1f9f	ed b0		. .
	call PrintExtMenu	;1fa1	cd b6 1d	. . .
	pop hl			;1fa4	e1		.
	ret			;1fa5	c9		.
l1fa6h:
	call sub_1fech		;1fa6	cd ec 1f	. . .
	jr nc,l1f30h		;1fa9	30 85		0 .
l1fabh:
	ld hl,(StepPc)		;1fab	2a d0 dd	* . .
	ld de,ReadAnyBankByte	;1fae	11 28 00	. ( .
	or a			;1fb1	b7		.
	sbc hl,de		;1fb2	ed 52		. R
	jr nz,l1f8bh		;1fb4	20 d5		  .
	bit 4,(iy-002h)		;1fb6	fd cb fe 66	. . . f
	jp nz,l1f30h		;1fba	c2 30 1f	. 0 .
	call sub_1e14h		;1fbd	cd 14 1e	. . .
	jr l1f8eh		;1fc0	18 cc		. .
l1fc2h:
	ld (StepPc),hl		;1fc2	22 d0 dd	" . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SetTempBreakpoint: plant CALL nnnn + RST 08 into the shadow
; context so stepping over a CALL breaks at the target.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SetTempBreakpoint:
	ld hl,(StepContext)	;1fc5	2a aa e0	* . .
	inc hl			;1fc8	23		#
	ld (hl),0cdh		;1fc9	36 cd		6 .
	inc hl			;1fcb	23		#
	ld de,(StepPc)		;1fcc	ed 5b d0 dd	. [ . .
	ld (hl),e		;1fd0	73		s
	inc hl			;1fd1	23		#
	ld (hl),d		;1fd2	72		r
	inc hl			;1fd3	23		#
	ld (hl),0cfh		;1fd4	36 cf		6 .
	call sub_2e0eh		;1fd6	cd 0e 2e	. . .
	push de			;1fd9	d5		.
	call PrepareStep	;1fda	cd d2 1d	. . .
	pop hl			;1fdd	e1		.
	jr l1f91h		;1fde	18 b1		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckTrace: trace-mode gate evaluated after every step.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckTrace:
	or a			;1fe0	b7		.
	bit 3,(iy+005h)		;1fe1	fd cb 05 5e	. . . ^
	ret z			;1fe5	c8		.
	ld a,(0ddd1h)		;1fe6	3a d1 dd	: . .
	cp 040h			;1fe9	fe 40		. @
	ret nc			;1feb	d0		.
sub_1fech:
	ld hl,(StepPc)		;1fec	2a d0 dd	* . .
sub_1fefh:
	bit 4,(iy-002h)		;1fef	fd cb fe 66	. . . f
	scf			;1ff3	37		7
	ret z			;1ff4	c8		.
	ld de,l3d00h		;1ff5	11 00 3d	. . =
	or a			;1ff8	b7		.
	sbc hl,de		;1ff9	ed 52		. R
	add hl,de		;1ffb	19		.
	ret c			;1ffc	d8		.
l1ffdh:
	ld de,l3e00h		;1ffd	11 00 3e	. . >
	sbc hl,de		;2000	ed 52		. R
	add hl,de		;2002	19		.
	ccf			;2003	3f		?
	ret			;2004	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AnalyzeInstruction - the static analyser: opcode class (EB/
; DD/FD/76/CB handled), length and control-flow effect from
; OpcodeClassTable ($25CE); results returned through the stack
; frame (SP=$DDCB on return).  Feeds StepDispatch and the
; step-over/breakpoint logic.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AnalyzeInstruction:
	ld (ix+000h),0f7h	;2005	dd 36 00 f7	. 6 . .
	pop ix			;2009	dd e1		. .
	ret			;200b	c9		.
l200ch:
	ld (ix+000h),0aah	;200c	dd 36 00 aa	. 6 . .
	inc ix			;2010	dd 23		. #
	ld (ix+000h),007h	;2012	dd 36 00 07	. 6 . .
l2016h:
	inc ix			;2016	dd 23		. #
l2018h:
	ld (iy+002h),001h	;2018	fd 36 02 01	. 6 . .
	jp l2142h		;201c	c3 42 21	. B !
l201fh:
	ld (ix+000h),032h	;201f	dd 36 00 32	. 6 . 2
	ld a,(hl)		;2023	7e		~
	or a			;2024	b7		.
	jr z,l2016h		;2025	28 ef		( .
l2027h:
	ld hl,AnalyzeDataPool_start	;2027	21 41 20	! A  
	ld de,0ddb7h		;202a	11 b7 dd	. . .
	ld bc,Rst08Vector	;202d	01 08 00	. . .
	ldir			;2030	ed b0		. .
	push de			;2032	d5		.
	pop ix			;2033	dd e1		. .
	set 6,(iy+003h)		;2035	fd cb 03 f6	. . . .
	ld a,(0ddach)		;2039	3a ac dd	: . .
	ld (0ddb9h),a		;203c	32 b9 dd	2 . .
	jr l2018h		;203f	18 d7		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AnalyzeDataPool: four scratch bytes followed by
; the 'BAD' text ($F0-terminated) shown for invalid
; opcodes by the disassembler path around it.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'AnalyzeDataPool' (start 0x2041 end 0x2049)
AnalyzeDataPool_start:
	defb 073h		;2041	73		s
	defb 0ffh		;2042	ff		.
	defb 000h		;2043	00		.
	defb 0eeh		;2044	ee		.
	defb 042h		;2045	42		B
	defb 041h		;2046	41		A
	defb 044h		;2047	44		D
	defb 0f0h		;2048	f0		.
AnalyzeDataPool_end:
	push ix			;2049	dd e5		. .
	ld ix,0ddb7h		;204b	dd 21 b7 dd	. ! . .
	ld de,(0ddaah)		;204f	ed 5b aa dd	. [ . .
	ld hl,AnalyzeInstruction	;2053	21 05 20	! .  
	push hl			;2056	e5		.
	ld (0ddcbh),sp		;2057	ed 73 cb dd	. s . .
	bit 4,(iy+000h)		;205b	fd cb 00 66	. . . f
	jr z,l2069h		;205f	28 08		( .
	call FindBreakpoint	;2061	cd ec 23	. . #
	ld b,0f1h		;2064	06 f1		. .
	call c,WordToHexBuf	;2066	dc c1 23	. . #
l2069h:
	ld e,009h		;2069	1e 09		. .
	ld hl,0dfe0h		;206b	21 e0 df	! . .
	xor a			;206e	af		.
	ld d,a			;206f	57		W
	ld (hl),a		;2070	77		w
	ld (0ddcdh),a		;2071	32 cd dd	2 . .
	ld (0dfe2h),a		;2074	32 e2 df	2 . .
	ld (0dfe3h),a		;2077	32 e3 df	2 . .
	ld bc,0ddach		;207a	01 ac dd	. . .
	ld a,(bc)		;207d	0a		.
	cp 0ebh			;207e	fe eb		. .
	jr z,l200ch		;2080	28 8a		( .
	cp 0ddh			;2082	fe dd		. .
	jr nz,l208ch		;2084	20 06		  .
	ld (hl),002h		;2086	36 02		6 .
	inc bc			;2088	03		.
	inc d			;2089	14		.
	jr l2094h		;208a	18 08		. .
l208ch:
	cp 0fdh			;208c	fe fd		. .
	jr nz,l2094h		;208e	20 04		  .
	ld (hl),003h		;2090	36 03		6 .
	inc bc			;2092	03		.
	inc d			;2093	14		.
l2094h:
	ld a,(bc)		;2094	0a		.
	cp 076h			;2095	fe 76		. v
	jr z,l201fh		;2097	28 86		( .
	cp 0cbh			;2099	fe cb		. .
	jr nz,l20a5h		;209b	20 08		  .
	set 3,(hl)		;209d	cb de		. .
	ld e,016h		;209f	1e 16		. .
	inc bc			;20a1	03		.
	inc d			;20a2	14		.
	jr l20b4h		;20a3	18 0f		. .
l20a5h:
	cp 0edh			;20a5	fe ed		. .
	jr nz,l20b4h		;20a7	20 0b		  .
	set 2,(hl)		;20a9	cb d6		. .
	bit 1,(hl)		;20ab	cb 4e		. N
	jp nz,l2027h		;20ad	c2 27 20	. '  
	inc bc			;20b0	03		.
	ld e,018h		;20b1	1e 18		. .
	inc d			;20b3	14		.
l20b4h:
	inc d			;20b4	14		.
	ld (iy+002h),d		;20b5	fd 72 02	. r .
	push de			;20b8	d5		.
	ld de,0ddb4h		;20b9	11 b4 dd	. . .
	ld h,b			;20bc	60		`
	ld l,c			;20bd	69		i
	ld bc,l0003h		;20be	01 03 00	. . .
	ldir			;20c1	ed b0		. .
	pop de			;20c3	d1		.
	ld a,(0dfe0h)		;20c4	3a e0 df	: . .
	and 00ah		;20c7	e6 0a		. .
	cp 00ah			;20c9	fe 0a		. .
	jr nz,l20d3h		;20cb	20 06		  .
	ld a,(0ddb5h)		;20cd	3a b5 dd	: . .
	ld (0ddb4h),a		;20d0	32 b4 dd	2 . .
l20d3h:
	xor a			;20d3	af		.
	push af			;20d4	f5		.
	push af			;20d5	f5		.
	ld a,e			;20d6	7b		{
l20d7h:
	add a,a			;20d7	87		.
	add a,a			;20d8	87		.
	ld b,000h		;20d9	06 00		. .
	ld c,a			;20db	4f		O
	ld hl,MnemonicTable_end	;20dc	21 ce 25	! . %
	add hl,bc		;20df	09		.
	ld c,(hl)		;20e0	4e		N
	inc hl			;20e1	23		#
	ld d,(hl)		;20e2	56		V
	inc hl			;20e3	23		#
	ld a,(hl)		;20e4	7e		~
	inc hl			;20e5	23		#
	ld h,(hl)		;20e6	66		f
	ld l,a			;20e7	6f		o
	ld a,(0ddb4h)		;20e8	3a b4 dd	: . .
	ld b,000h		;20eb	06 00		. .
	and c			;20ed	a1		.
l20eeh:
	inc b			;20ee	04		.
	srl c			;20ef	cb 39		. 9
	jr nc,l20eeh		;20f1	30 fb		0 .
	or a			;20f3	b7		.
l20f4h:
	dec b			;20f4	05		.
	jr z,l20fah		;20f5	28 03		( .
	rrca			;20f7	0f		.
	jr l20f4h		;20f8	18 fa		. .
l20fah:
	bit 0,d			;20fa	cb 42		. B
	jr nz,l20ffh		;20fc	20 01		  .
	add a,a			;20fe	87		.
l20ffh:
	ld b,000h		;20ff	06 00		. .
	ld c,a			;2101	4f		O
	add hl,bc		;2102	09		.
l2103h:
	ld a,(hl)		;2103	7e		~
	inc hl			;2104	23		#
	dec d			;2105	15		.
	or a			;2106	b7		.
	jr z,l212ch		;2107	28 23		( #
	cp 0eah			;2109	fe ea		. .
	jp z,l2027h		;210b	ca 27 20	. '  
	cp 0c7h			;210e	fe c7		. .
	jr c,l215eh		;2110	38 4c		8 L
	cp 0eah			;2112	fe ea		. .
	jr nc,l215eh		;2114	30 48		0 H
	sub 0c6h		;2116	d6 c6		. .
	cp 017h			;2118	fe 17		. .
	jr nz,l2120h		;211a	20 04		  .
	set 6,(iy+004h)		;211c	fd cb 04 f6	. . . .
l2120h:
	bit 0,d			;2120	cb 42		. B
	jr z,l20d7h		;2122	28 b3		( .
	push de			;2124	d5		.
	push hl			;2125	e5		.
	jr l20d7h		;2126	18 af		. .
l2128h:
	ld a,d			;2128	7a		z
	or a			;2129	b7		.
	jr nz,l2103h		;212a	20 d7		  .
l212ch:
	pop hl			;212c	e1		.
	pop de			;212d	d1		.
	ld a,d			;212e	7a		z
	or a			;212f	b7		.
	jr nz,l2103h		;2130	20 d1		  .
	ld hl,0dfe0h		;2132	21 e0 df	! . .
	bit 1,(hl)		;2135	cb 4e		. N
	jr z,l2142h		;2137	28 09		( .
	bit 4,(hl)		;2139	cb 66		. f
	jr nz,l2142h		;213b	20 05		  .
	bit 7,(hl)		;213d	cb 7e		. ~
	jp z,l2027h		;213f	ca 27 20	. '  
l2142h:
	ld bc,(0dfe1h)		;2142	ed 4b e1 df	. K . .
	ld b,000h		;2146	06 00		. .
	ld sp,(0ddcbh)		;2148	ed 7b cb dd	. { . .
	ret			;214c	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CollectAsmText: capture assembler source from the editor
; into the work buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CollectAsmText:
	cp 020h			;214d	fe 20		.  
	jr nc,EditorKeyLoop	;214f	30 20		0  
	ld (ix+000h),a		;2151	dd 77 00	. w .
	inc ix			;2154	dd 23		. #
	set 5,(iy+001h)		;2156	fd cb 01 ee	. . . .
	ld a,022h		;215a	3e 22		> "
	jr EditorKeyLoop	;215c	18 13		. .
l215eh:
	push de			;215e	d5		.
	push hl			;215f	e5		.
	bit 1,(iy+001h)		;2160	fd cb 01 4e	. . . N
	jr z,EditorKeyLoop	;2164	28 0b		( .
	ld hl,0dfe3h		;2166	21 e3 df	! . .
	bit 7,(hl)		;2169	cb 7e		. ~
	jr nz,CollectAsmText	;216b	20 e0		  .
	bit 6,(hl)		;216d	cb 76		. v
	jr nz,CollectAsmText	;216f	20 dc		  .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EditorKeyLoop: the line editor dispatcher - EditorKeyTable
; ($27B2, CPIR-searched, 0x24 entries) maps keys to cursor
; moves, insert/delete and history actions.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EditorKeyLoop:
	push af			;2171	f5		.
	ld hl,OpcodeClassTable_end	;2172	21 b2 27	! . '
	ld bc,l0024h		;2175	01 24 00	. $ .
	cpir			;2178	ed b1		. .
	ld b,a			;217a	47		G
	jp nz,EditorRegKeys	;217b	c2 61 22	. a "
	ld a,c			;217e	79		y
	cp 01eh			;217f	fe 1e		. .
	jr nc,l219fh		;2181	30 1c		0 .
	cp 01ch			;2183	fe 1c		. .
	jr nc,l21d4h		;2185	30 4d		0 M
	cp 00bh			;2187	fe 0b		. .
	jp c,l22e0h		;2189	da e0 22	. . "
	cp 014h			;218c	fe 14		. .
	ld hl,0dfe2h		;218e	21 e2 df	! . .
	jr nc,l21dah		;2191	30 47		0 G
	cp 011h			;2193	fe 11		. .
	jr c,l21e2h		;2195	38 4b		8 K
	ld (hl),022h		;2197	36 22		6 "
	jp EditorRegKeys	;2199	c3 61 22	. a "
l219ch:
	dec b			;219c	05		.
	jr l21cbh		;219d	18 2c		. ,
l219fh:
	ld hl,0dfe0h		;219f	21 e0 df	! . .
	cp 023h			;21a2	fe 23		. #
	jr z,l219ch		;21a4	28 f6		( .
	cp 022h			;21a6	fe 22		. "
	jr z,l21cbh		;21a8	28 21		( !
	bit 1,(hl)		;21aa	cb 4e		. N
	jr z,l21cdh		;21ac	28 1f		( .
	bit 3,(hl)		;21ae	cb 5e		. ^
	jr nz,l21cdh		;21b0	20 1b		  .
	sub 01dh		;21b2	d6 1d		. .
	cp 003h			;21b4	fe 03		. .
	jr nc,l21d0h		;21b6	30 18		0 .
	bit 7,(hl)		;21b8	cb 7e		. ~
	jr nz,l21cdh		;21ba	20 11		  .
	bit 5,(iy+004h)		;21bc	fd cb 04 6e	. . . n
	jr nz,l21cdh		;21c0	20 0b		  .
l21c2h:
	bit 0,(hl)		;21c2	cb 46		. F
	jr z,l21c8h		;21c4	28 02		( .
	add a,002h		;21c6	c6 02		. .
l21c8h:
	ld b,a			;21c8	47		G
	set 4,(hl)		;21c9	cb e6		. .
l21cbh:
	set 5,(hl)		;21cb	cb ee		. .
l21cdh:
	jp EditorRegKeys	;21cd	c3 61 22	. a "
l21d0h:
	add a,07bh		;21d0	c6 7b		. {
	jr l21c2h		;21d2	18 ee		. .
l21d4h:
	set 7,(iy+003h)		;21d4	fd cb 03 fe	. . . .
	jr l21cdh		;21d8	18 f3		. .
l21dah:
	bit 1,(hl)		;21da	cb 4e		. N
	jr z,l21cdh		;21dc	28 ef		( .
	ld (hl),002h		;21de	36 02		6 .
	jr l21cdh		;21e0	18 eb		. .
l21e2h:
	ld hl,(RegisterFile)	;21e2	2a 73 dd	* s .
	ld (0ddceh),hl		;21e5	22 ce dd	" . .
	cp 00eh			;21e8	fe 0e		. .
	jr nz,l21f0h		;21ea	20 04		  .
	set 4,(iy+003h)		;21ec	fd cb 03 e6	. . . .
l21f0h:
	ld hl,0dfe0h		;21f0	21 e0 df	! . .
	ld a,(hl)		;21f3	7e		~
	bit 1,a			;21f4	cb 4f		. O
	jr z,l21cdh		;21f6	28 d5		( .
	set 7,(hl)		;21f8	cb fe		. .
	ld e,c			;21fa	59		Y
	ld hl,l27cbh		;21fb	21 cb 27	! . '
	bit 0,a			;21fe	cb 47		. G
	ld b,000h		;2200	06 00		. .
	jr z,l2208h		;2202	28 04		( .
	ld a,c			;2204	79		y
	add a,006h		;2205	c6 06		. .
	ld c,a			;2207	4f		O
l2208h:
	add hl,bc		;2208	09		.
	ld b,(hl)		;2209	46		F
	ld (ix+000h),b		;220a	dd 70 00	. p .
	inc ix			;220d	dd 23		. #
	ld a,e			;220f	7b		{
	cp 00dh			;2210	fe 0d		. .
	jr nc,l2266h		;2212	30 52		0 R
	cp 00ch			;2214	fe 0c		. .
	jr nz,l2230h		;2216	20 18		  .
	bit 4,(iy+001h)		;2218	fd cb 01 66	. . . f
	jr z,l2230h		;221c	28 12		( .
	ld a,001h		;221e	3e 01		> .
	and (ix-002h)		;2220	dd a6 fe	. . .
	or 0bah			;2223	f6 ba		. .
	ld (ix-002h),a		;2225	dd 77 fe	. w .
	res 4,(iy+001h)		;2228	fd cb 01 a6	. . . .
	res 5,(iy+001h)		;222c	fd cb 01 ae	. . . .
l2230h:
	inc (iy+002h)		;2230	fd 34 02	. 4 .
	ld a,(0ddaeh)		;2233	3a ae dd	: . .
	or a			;2236	b7		.
	jp p,l223fh		;2237	f2 3f 22	. ? "
	inc (ix-001h)		;223a	dd 34 ff	. 4 .
	neg			;223d	ed 44		. D
l223fh:
	ld (ix+000h),0fdh	;223f	dd 36 00 fd	. 6 . .
	inc ix			;2243	dd 23		. #
	ld b,a			;2245	47		G
	ld hl,(0dd6fh)		;2246	2a 6f dd	* o .
	bit 0,(iy+001h)		;2249	fd cb 01 46	. . . F
	jr z,l2252h		;224d	28 03		( .
	ld hl,(0dd71h)		;224f	2a 71 dd	* q .
l2252h:
	ld de,(0ddaeh)		;2252	ed 5b ae dd	. [ . .
	ld d,000h		;2256	16 00		. .
	bit 7,e			;2258	cb 7b		. {
	jr z,l225dh		;225a	28 01		( .
	dec d			;225c	15		.
l225dh:
	add hl,de		;225d	19		.
	ld (0ddceh),hl		;225e	22 ce dd	" . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EditorRegKeys: register-edit keys (RegisterKeyTable $27E2)
; - hex entry into the highlighted register of the dump.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EditorRegKeys:
	ld (ix+000h),b		;2261	dd 70 00	. p .
	inc ix			;2264	dd 23		. #
l2266h:
	ld hl,EditorKeyTable_end	;2266	21 e2 27	! . '
	ld bc,Rst08Vector+2	;2269	01 0a 00	. . .
	cpir			;226c	ed b1		. .
	jr nz,l22c6h		;226e	20 56		  V
	ld a,c			;2270	79		y
	cp 008h			;2271	fe 08		. .
	jr nc,l22cfh		;2273	30 5a		0 Z
	and 003h		;2275	e6 03		. .
	jr z,l228eh		;2277	28 15		( .
	dec a			;2279	3d		=
	jr z,l2284h		;227a	28 08		( .
	dec a			;227c	3d		=
	jr z,l2289h		;227d	28 0a		( .
	ld hl,(StepPc)		;227f	2a d0 dd	* . .
	jr l2291h		;2282	18 0d		. .
l2284h:
	ld hl,(0dd77h)		;2284	2a 77 dd	* w .
	jr l2291h		;2287	18 08		. .
l2289h:
	ld hl,(0dd75h)		;2289	2a 75 dd	* u .
	jr l2291h		;228c	18 03		. .
l228eh:
	ld hl,(0ddceh)		;228e	2a ce dd	* . .
l2291h:
	ld (0ddd2h),hl		;2291	22 d2 dd	" . .
	ld a,c			;2294	79		y
	cp 004h			;2295	fe 04		. .
	ld b,002h		;2297	06 02		. .
	jr nc,l22c3h		;2299	30 28		0 (
	dec b			;229b	05		.
	cp 003h			;229c	fe 03		. .
	jr nz,l22a9h		;229e	20 09		  .
	ld a,(0ddcdh)		;22a0	3a cd dd	: . .
	cp 0a1h			;22a3	fe a1		. .
	jr nz,l22c3h		;22a5	20 1c		  .
	jr l22c6h		;22a7	18 1d		. .
l22a9h:
	or a			;22a9	b7		.
	jr nz,l22c3h		;22aa	20 17		  .
	ld a,(0ddcdh)		;22ac	3a cd dd	: . .
	ld hl,l27ech		;22af	21 ec 27	! . '
	ld bc,Rst08Vector+1	;22b2	01 09 00	. . .
	cpir			;22b5	ed b1		. .
	jr z,l22c1h		;22b7	28 08		( .
	bit 7,(iy+004h)		;22b9	fd cb 04 7e	. . . ~
	ld b,001h		;22bd	06 01		. .
	jr z,l22c3h		;22bf	28 02		( .
l22c1h:
	ld b,003h		;22c1	06 03		. .
l22c3h:
	ld (iy+004h),b		;22c3	fd 70 04	. p .
l22c6h:
	pop af			;22c6	f1		.
	ld (0ddcdh),a		;22c7	32 cd dd	2 . .
	pop hl			;22ca	e1		.
	pop de			;22cb	d1		.
	jp l2128h		;22cc	c3 28 21	. ( !
l22cfh:
	ld b,003h		;22cf	06 03		. .
	ld hl,(RegisterFile)	;22d1	2a 73 dd	* s .
	ld (0ddd2h),hl		;22d4	22 d2 dd	" . .
	jr l22c3h		;22d7	18 ea		. .
l22d9h:
	ld b,0ech		;22d9	06 ec		. .
	call WordToHexBuf	;22db	cd c1 23	. . #
	jr l2266h		;22de	18 86		. .
l22e0h:
	cp 003h			;22e0	fe 03		. .
	jr c,l22e9h		;22e2	38 05		8 .
	ld (ix+000h),b		;22e4	dd 70 00	. p .
	inc ix			;22e7	dd 23		. #
l22e9h:
	ld d,000h		;22e9	16 00		. .
	ld hl,0dfe2h		;22eb	21 e2 df	! . .
	cp 001h			;22ee	fe 01		. .
	jr c,l236fh		;22f0	38 7d		8 }
	jr z,l2312h		;22f2	28 1e		( .
	cp 004h			;22f4	fe 04		. .
	jr c,l233fh		;22f6	38 47		8 G
	cp 006h			;22f8	fe 06		. .
	jr c,l2312h		;22fa	38 16		8 .
	cp 008h			;22fc	fe 08		. .
	jr c,l235ah		;22fe	38 5a		8 Z
	jr z,l235eh		;2300	28 5c		( \
l2302h:
	cp 00ah			;2302	fe 0a		. .
	jp z,l23aeh		;2304	ca ae 23	. . #
	ld a,(0ddb4h)		;2307	3a b4 dd	: . .
	and 038h		;230a	e6 38		. 8
	set 0,(hl)		;230c	cb c6		. .
	ld e,a			;230e	5f		_
	jp l2399h		;230f	c3 99 23	. . #
l2312h:
	ld a,(0ddcdh)		;2312	3a cd dd	: . .
	cp 0a1h			;2315	fe a1		. .
	jr z,l233fh		;2317	28 26		( &
	cp 055h			;2319	fe 55		. U
	jr z,l2331h		;231b	28 14		( .
	cp 053h			;231d	fe 53		. S
	jr z,l2329h		;231f	28 08		( .
	cp 091h			;2321	fe 91		. .
	jr nc,l232dh		;2323	30 08		0 .
	cp 082h			;2325	fe 82		. .
	jr c,l2333h		;2327	38 0a		8 .
l2329h:
	set 3,(hl)		;2329	cb de		. .
	jr l2333h		;232b	18 06		. .
l232dh:
	cp 099h			;232d	fe 99		. .
	jr nc,l2333h		;232f	30 02		0 .
l2331h:
	set 0,(hl)		;2331	cb c6		. .
l2333h:
	ld de,(0ddb5h)		;2333	ed 5b b5 dd	. [ . .
	inc (iy+002h)		;2337	fd 34 02	. 4 .
	inc (iy+002h)		;233a	fd 34 02	. 4 .
	jr l2387h		;233d	18 48		. H
l233fh:
	inc (iy+002h)		;233f	fd 34 02	. 4 .
	ld hl,0dfe0h		;2342	21 e0 df	! . .
	bit 1,(hl)		;2345	cb 4e		. N
	jr z,l2352h		;2347	28 09		( .
	bit 4,(hl)		;2349	cb 66		. f
	jr nz,l2352h		;234b	20 05		  .
	ld a,(0ddb6h)		;234d	3a b6 dd	: . .
	jr l2355h		;2350	18 03		. .
l2352h:
	ld a,(0ddb5h)		;2352	3a b5 dd	: . .
l2355h:
	ld e,a			;2355	5f		_
	ld d,000h		;2356	16 00		. .
	jr l2391h		;2358	18 37		. 7
l235ah:
	set 7,(iy+004h)		;235a	fd cb 04 fe	. . . .
l235eh:
	ld a,(0ddb4h)		;235e	3a b4 dd	: . .
	set 5,(iy+004h)		;2361	fd cb 04 ee	. . . .
	and 038h		;2365	e6 38		. 8
	rrca			;2367	0f		.
	rrca			;2368	0f		.
	rrca			;2369	0f		.
	ld e,a			;236a	5f		_
	ld b,0fch		;236b	06 fc		. .
	jr l239bh		;236d	18 2c		. ,
l236fh:
	inc (iy+002h)		;236f	fd 34 02	. 4 .
	set 2,(hl)		;2372	cb d6		. .
	ld a,(0ddb5h)		;2374	3a b5 dd	: . .
	ld d,000h		;2377	16 00		. .
	or a			;2379	b7		.
	jp p,l237eh		;237a	f2 7e 23	. ~ #
	dec d			;237d	15		.
l237eh:
	ld e,a			;237e	5f		_
	ld hl,(0ddaah)		;237f	2a aa dd	* . .
	inc hl			;2382	23		#
	inc hl			;2383	23		#
	add hl,de		;2384	19		.
	ld e,l			;2385	5d		]
	ld d,h			;2386	54		T
l2387h:
	set 6,(iy+001h)		;2387	fd cb 01 f6	. . . .
	call FindBreakpoint	;238b	cd ec 23	. . #
	jp c,l22d9h		;238e	da d9 22	. . "
l2391h:
	ld b,0fch		;2391	06 fc		. .
	bit 0,(iy+005h)		;2393	fd cb 05 46	. . . F
	jr nz,l239bh		;2397	20 02		  .
l2399h:
	ld b,0feh		;2399	06 fe		. .
l239bh:
	ld (ix+000h),b		;239b	dd 70 00	. p .
	inc ix			;239e	dd 23		. #
	ld b,e			;23a0	43		C
	ld (ix+000h),b		;23a1	dd 70 00	. p .
	inc ix			;23a4	dd 23		. #
	ld b,d			;23a6	42		B
	ld (StepPc),de		;23a7	ed 53 d0 dd	. S . .
	jp EditorRegKeys	;23ab	c3 61 22	. a "
l23aeh:
	ld e,000h		;23ae	1e 00		. .
	ld b,0fch		;23b0	06 fc		. .
	ld a,(0ddb4h)		;23b2	3a b4 dd	: . .
	cp 046h			;23b5	fe 46		. F
	jr z,l239bh		;23b7	28 e2		( .
	inc e			;23b9	1c		.
	cp 056h			;23ba	fe 56		. V
	jr z,l239bh		;23bc	28 dd		( .
	inc e			;23be	1c		.
	jr l239bh		;23bf	18 da		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WordToHexBuf: format HL as 4 hex characters (bit-7
; terminated) in the work buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WordToHexBuf:
	ld (ix+000h),b		;23c1	dd 70 00	. p .
	ld (ix+001h),04ch	;23c4	dd 36 01 4c	. 6 . L
	inc ix			;23c8	dd 23		. #
	inc ix			;23ca	dd 23		. #
	ld c,004h		;23cc	0e 04		. .
l23ceh:
	ld l,004h		;23ce	2e 04		. .
	xor a			;23d0	af		.
l23d1h:
	sla e			;23d1	cb 23		. #
	rl d			;23d3	cb 12		. .
	rla			;23d5	17		.
	dec l			;23d6	2d		-
	jr nz,l23d1h		;23d7	20 f8		  .
	add a,090h		;23d9	c6 90		. .
	daa			;23db	27		'
	adc a,040h		;23dc	ce 40		. @
	daa			;23de	27		'
	ld (ix+000h),a		;23df	dd 77 00	. w .
	inc ix			;23e2	dd 23		. #
	dec c			;23e4	0d		.
	jr nz,l23ceh		;23e5	20 e7		  .
	set 7,(ix-001h)		;23e7	dd cb ff fe	. . . .
	ret			;23eb	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FindBreakpoint: search BreakTable (DDD6, count at
; BreakCount DDD4) for an address.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FindBreakpoint:
	or a			;23ec	b7		.
	bit 4,(iy+000h)		;23ed	fd cb 00 66	. . . f
	ret z			;23f1	c8		.
	ld bc,(BreakCount)	;23f2	ed 4b d4 dd	. K . .
	ld hl,(BreakTable)	;23f6	2a d6 dd	* . .
l23f9h:
	ld a,b			;23f9	78		x
	or c			;23fa	b1		.
	ret z			;23fb	c8		.
	ld a,(hl)		;23fc	7e		~
	inc hl			;23fd	23		#
	cp e			;23fe	bb		.
	jr nz,l2405h		;23ff	20 04		  .
	ld a,(hl)		;2401	7e		~
	cp d			;2402	ba		.
	scf			;2403	37		7
	ret z			;2404	c8		.
l2405h:
	inc hl			;2405	23		#
	dec bc			;2406	0b		.
	jr l23f9h		;2407	18 f0		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MnemonicTable: packed mnemonic strings; placeholder
; characters carry bit 7 and mark operand splice points.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MnemonicTable:

; BLOCK 'MnemonicTable' (start 0x2409 end 0x25ce)
	defb 05ch		;2409	5c		\
	defb 081h		;240a	81		.
	defb 05ch		;240b	5c		\
	defb 082h		;240c	82		.
	defb 05ch		;240d	5c		\
	defb 083h		;240e	83		.
	defb 05ch		;240f	5c		\
	defb 084h		;2410	84		.
	defb 053h		;2411	53		S
	defb 095h		;2412	95		.
	defb 053h		;2413	53		S
	defb 094h		;2414	94		.
	defb 053h		;2415	53		S
	defb 08dh		;2416	8d		.
	defb 053h		;2417	53		S
	defb 096h		;2418	96		.
	defb 053h		;2419	53		S
	defb 097h		;241a	97		.
	defb 053h		;241b	53		S
	defb 09ah		;241c	9a		.
	defb 053h		;241d	53		S
	defb 099h		;241e	99		.
	defb 053h		;241f	53		S
	defb 098h		;2420	98		.
	defb 053h		;2421	53		S
	defb 0a2h		;2422	a2		.
	defb 053h		;2423	53		S
	defb 0a3h		;2424	a3		.
	defb 053h		;2425	53		S
	defb 0a4h		;2426	a4		.
	defb 054h		;2427	54		T
	defb 095h		;2428	95		.
	defb 054h		;2429	54		T
	defb 094h		;242a	94		.
	defb 054h		;242b	54		T
	defb 08dh		;242c	8d		.
	defb 054h		;242d	54		T
	defb 096h		;242e	96		.
	defb 055h		;242f	55		U
	defb 095h		;2430	95		.
	defb 055h		;2431	55		U
	defb 094h		;2432	94		.
	defb 055h		;2433	55		U
	defb 08dh		;2434	8d		.
	defb 055h		;2435	55		U
	defb 096h		;2436	96		.
	defb 055h		;2437	55		U
	defb 097h		;2438	97		.
	defb 055h		;2439	55		U
	defb 09ah		;243a	9a		.
	defb 055h		;243b	55		U
	defb 099h		;243c	99		.
	defb 055h		;243d	55		U
	defb 098h		;243e	98		.
	defb 056h		;243f	56		V
	defb 08bh		;2440	8b		.
	defb 056h		;2441	56		V
	defb 087h		;2442	87		.
	defb 057h		;2443	57		W
	defb 08bh		;2444	8b		.
	defb 057h		;2445	57		W
	defb 087h		;2446	87		.
	defb 057h		;2447	57		W
	defb 092h		;2448	92		.
	defb 057h		;2449	57		W
	defb 093h		;244a	93		.
	defb 058h		;244b	58		X
	defb 08bh		;244c	8b		.
	defb 058h		;244d	58		X
	defb 087h		;244e	87		.
	defb 059h		;244f	59		Y
	defb 08bh		;2450	8b		.
	defb 059h		;2451	59		Y
	defb 08ch		;2452	8c		.
	defb 059h		;2453	59		Y
	defb 08dh		;2454	8d		.
	defb 059h		;2455	59		Y
	defb 08eh		;2456	8e		.
	defb 059h		;2457	59		Y
	defb 08fh		;2458	8f		.
	defb 059h		;2459	59		Y
	defb 090h		;245a	90		.
	defb 059h		;245b	59		Y
	defb 091h		;245c	91		.
	defb 05ah		;245d	5a		Z
	defb 0a6h		;245e	a6		.
	defb 05ah		;245f	5a		Z
	defb 0abh		;2460	ab		.
	defb 05bh		;2461	5b		[
	defb 086h		;2462	86		.
	defb 05bh		;2463	5b		[
	defb 08ah		;2464	8a		.
	defb 05bh		;2465	5b		[
	defb 0a5h		;2466	a5		.
	defb 05ch		;2467	5c		\
	defb 0a0h		;2468	a0		.
	defb 05ch		;2469	5c		\
	defb 0a1h		;246a	a1		.
	defb 05ch		;246b	5c		\
	defb 0a2h		;246c	a2		.
	defb 05ch		;246d	5c		\
	defb 0a9h		;246e	a9		.
	defb 05ch		;246f	5c		\
	defb 0aah		;2470	aa		.
	defb 05ch		;2471	5c		\
	defb 0a7h		;2472	a7		.
	defb 05ch		;2473	5c		\
	defb 0a8h		;2474	a8		.
	defb 05ch		;2475	5c		\
	defb 0abh		;2476	ab		.
	defb 05ch		;2477	5c		\
	defb 08bh		;2478	8b		.
	defb 05ch		;2479	5c		\
	defb 08ch		;247a	8c		.
	defb 05ch		;247b	5c		\
	defb 08dh		;247c	8d		.
	defb 05ch		;247d	5c		\
	defb 08eh		;247e	8e		.
	defb 05ch		;247f	5c		\
	defb 08fh		;2480	8f		.
	defb 05ch		;2481	5c		\
	defb 090h		;2482	90		.
	defb 05ch		;2483	5c		\
	defb 091h		;2484	91		.
	defb 05ch		;2485	5c		\
	defb 085h		;2486	85		.
	defb 05ch		;2487	5c		\
	defb 086h		;2488	86		.
	defb 05ch		;2489	5c		\
	defb 087h		;248a	87		.
	defb 05ch		;248b	5c		\
	defb 093h		;248c	93		.
	defb 05ch		;248d	5c		\
	defb 092h		;248e	92		.
	defb 05ch		;248f	5c		\
	defb 09bh		;2490	9b		.
	defb 05ch		;2491	5c		\
	defb 09ch		;2492	9c		.
	defb 05ch		;2493	5c		\
	defb 088h		;2494	88		.
	defb 000h		;2495	00		.
l2496h:
	defb 049h		;2496	49		I
	defb 058h		;2497	58		X
	defb 0c8h		;2498	c8		.
	defb 049h		;2499	49		I
	defb 058h		;249a	58		X
	defb 0cch		;249b	cc		.
	defb 049h		;249c	49		I
	defb 059h		;249d	59		Y
	defb 0c8h		;249e	c8		.
	defb 049h		;249f	49		I
	defb 059h		;24a0	59		Y
	defb 0cch		;24a1	cc		.
	defb 042h		;24a2	42		B
	defb 0c3h		;24a3	c3		.
	defb 044h		;24a4	44		D
	defb 0c5h		;24a5	c5		.
	defb 048h		;24a6	48		H
	defb 0cch		;24a7	cc		.
	defb 053h		;24a8	53		S
	defb 0d0h		;24a9	d0		.
	defb 041h		;24aa	41		A
	defb 046h		;24ab	46		F
	defb 0a7h		;24ac	a7		.
	defb 041h		;24ad	41		A
	defb 0c6h		;24ae	c6		.
	defb 0c1h		;24af	c1		.
	defb 0c2h		;24b0	c2		.
	defb 0c3h		;24b1	c3		.
	defb 0c4h		;24b2	c4		.
	defb 0c5h		;24b3	c5		.
	defb 0c8h		;24b4	c8		.
	defb 0cch		;24b5	cc		.
	defb 049h		;24b6	49		I
	defb 0d8h		;24b7	d8		.
	defb 049h		;24b8	49		I
	defb 0d9h		;24b9	d9		.
	defb 04eh		;24ba	4e		N
	defb 0dah		;24bb	da		.
	defb 0dah		;24bc	da		.
	defb 04eh		;24bd	4e		N
	defb 0c3h		;24be	c3		.
	defb 0cdh		;24bf	cd		.
	defb 050h		;24c0	50		P
	defb 0cfh		;24c1	cf		.
	defb 050h		;24c2	50		P
	defb 0c5h		;24c3	c5		.
	defb 0d0h		;24c4	d0		.
	defb 0c9h		;24c5	c9		.
	defb 0d2h		;24c6	d2		.
	defb 0c6h		;24c7	c6		.
	defb 080h		;24c8	80		.
	defb 080h		;24c9	80		.
	defb 028h		;24ca	28		(
	defb 042h		;24cb	42		B
	defb 043h		;24cc	43		C
	defb 0a9h		;24cd	a9		.
	defb 028h		;24ce	28		(
	defb 044h		;24cf	44		D
	defb 045h		;24d0	45		E
	defb 0a9h		;24d1	a9		.
	defb 028h		;24d2	28		(
	defb 048h		;24d3	48		H
	defb 04ch		;24d4	4c		L
	defb 0a9h		;24d5	a9		.
	defb 028h		;24d6	28		(
	defb 049h		;24d7	49		I
	defb 058h		;24d8	58		X
	defb 0a9h		;24d9	a9		.
	defb 028h		;24da	28		(
	defb 049h		;24db	49		I
	defb 059h		;24dc	59		Y
	defb 0a9h		;24dd	a9		.
	defb 028h		;24de	28		(
	defb 053h		;24df	53		S
	defb 050h		;24e0	50		P
	defb 0a9h		;24e1	a9		.
	defb 028h		;24e2	28		(
	defb 043h		;24e3	43		C
	defb 0a9h		;24e4	a9		.
	defb 028h		;24e5	28		(
	defb 049h		;24e6	49		I
	defb 058h		;24e7	58		X
	defb 0abh		;24e8	ab		.
	defb 028h		;24e9	28		(
	defb 049h		;24ea	49		I
	defb 058h		;24eb	58		X
	defb 0adh		;24ec	ad		.
	defb 028h		;24ed	28		(
	defb 049h		;24ee	49		I
	defb 059h		;24ef	59		Y
	defb 0abh		;24f0	ab		.
	defb 028h		;24f1	28		(
	defb 049h		;24f2	49		I
	defb 059h		;24f3	59		Y
	defb 0adh		;24f4	ad		.
	defb 0a8h		;24f5	a8		.
	defb 000h		;24f6	00		.
l24f7h:
	defb 043h		;24f7	43		C
	defb 043h		;24f8	43		C
	defb 0c6h		;24f9	c6		.
	defb 043h		;24fa	43		C
	defb 050h		;24fb	50		P
	defb 0cch		;24fc	cc		.
	defb 044h		;24fd	44		D
	defb 041h		;24fe	41		A
	defb 0c1h		;24ff	c1		.
	defb 044h		;2500	44		D
	defb 0c9h		;2501	c9		.
	defb 045h		;2502	45		E
	defb 0c9h		;2503	c9		.
	defb 045h		;2504	45		E
	defb 058h		;2505	58		X
	defb 0d8h		;2506	d8		.
	defb 048h		;2507	48		H
	defb 041h		;2508	41		A
	defb 04ch		;2509	4c		L
	defb 0d4h		;250a	d4		.
	defb 04eh		;250b	4e		N
	defb 04fh		;250c	4f		O
	defb 0d0h		;250d	d0		.
	defb 052h		;250e	52		R
	defb 04ch		;250f	4c		L
	defb 0c1h		;2510	c1		.
	defb 052h		;2511	52		R
	defb 04ch		;2512	4c		L
	defb 043h		;2513	43		C
	defb 0c1h		;2514	c1		.
	defb 052h		;2515	52		R
	defb 052h		;2516	52		R
	defb 0c1h		;2517	c1		.
	defb 052h		;2518	52		R
	defb 052h		;2519	52		R
	defb 043h		;251a	43		C
	defb 0c1h		;251b	c1		.
	defb 053h		;251c	53		S
	defb 043h		;251d	43		C
	defb 0c6h		;251e	c6		.
	defb 04ch		;251f	4c		L
	defb 044h		;2520	44		D
	defb 049h		;2521	49		I
	defb 0d2h		;2522	d2		.
	defb 04ch		;2523	4c		L
	defb 044h		;2524	44		D
	defb 044h		;2525	44		D
	defb 0d2h		;2526	d2		.
	defb 043h		;2527	43		C
	defb 050h		;2528	50		P
	defb 049h		;2529	49		I
	defb 0d2h		;252a	d2		.
	defb 043h		;252b	43		C
	defb 050h		;252c	50		P
	defb 044h		;252d	44		D
	defb 0d2h		;252e	d2		.
	defb 049h		;252f	49		I
	defb 04eh		;2530	4e		N
	defb 049h		;2531	49		I
	defb 0d2h		;2532	d2		.
	defb 049h		;2533	49		I
	defb 04eh		;2534	4e		N
	defb 044h		;2535	44		D
	defb 0d2h		;2536	d2		.
	defb 04fh		;2537	4f		O
	defb 054h		;2538	54		T
	defb 049h		;2539	49		I
	defb 0d2h		;253a	d2		.
	defb 04fh		;253b	4f		O
	defb 054h		;253c	54		T
	defb 044h		;253d	44		D
	defb 0d2h		;253e	d2		.
	defb 04ch		;253f	4c		L
	defb 044h		;2540	44		D
	defb 0c9h		;2541	c9		.
	defb 04ch		;2542	4c		L
	defb 044h		;2543	44		D
	defb 0c4h		;2544	c4		.
	defb 043h		;2545	43		C
	defb 050h		;2546	50		P
	defb 0c9h		;2547	c9		.
	defb 043h		;2548	43		C
	defb 050h		;2549	50		P
	defb 0c4h		;254a	c4		.
	defb 049h		;254b	49		I
	defb 04eh		;254c	4e		N
	defb 0c9h		;254d	c9		.
	defb 049h		;254e	49		I
	defb 04eh		;254f	4e		N
	defb 0c4h		;2550	c4		.
	defb 04fh		;2551	4f		O
	defb 055h		;2552	55		U
	defb 054h		;2553	54		T
	defb 0c9h		;2554	c9		.
	defb 04fh		;2555	4f		O
	defb 055h		;2556	55		U
	defb 054h		;2557	54		T
	defb 0c4h		;2558	c4		.
	defb 052h		;2559	52		R
	defb 04ch		;255a	4c		L
	defb 0c4h		;255b	c4		.
	defb 052h		;255c	52		R
	defb 052h		;255d	52		R
	defb 0c4h		;255e	c4		.
	defb 052h		;255f	52		R
	defb 045h		;2560	45		E
	defb 054h		;2561	54		T
	defb 0c9h		;2562	c9		.
	defb 052h		;2563	52		R
	defb 045h		;2564	45		E
	defb 054h		;2565	54		T
	defb 0ceh		;2566	ce		.
	defb 04eh		;2567	4e		N
	defb 045h		;2568	45		E
	defb 0c7h		;2569	c7		.
	defb 080h		;256a	80		.
	defb 080h		;256b	80		.
	defb 080h		;256c	80		.
	defb 044h		;256d	44		D
	defb 045h		;256e	45		E
	defb 0c3h		;256f	c3		.
	defb 049h		;2570	49		I
	defb 04eh		;2571	4e		N
	defb 0c3h		;2572	c3		.
	defb 04ah		;2573	4a		J
	defb 0d0h		;2574	d0		.
	defb 04ah		;2575	4a		J
	defb 0d2h		;2576	d2		.
	defb 043h		;2577	43		C
	defb 041h		;2578	41		A
	defb 04ch		;2579	4c		L
	defb 0cch		;257a	cc		.
	defb 041h		;257b	41		A
	defb 044h		;257c	44		D
	defb 0c3h		;257d	c3		.
	defb 041h		;257e	41		A
	defb 044h		;257f	44		D
	defb 0c4h		;2580	c4		.
	defb 053h		;2581	53		S
	defb 042h		;2582	42		B
	defb 0c3h		;2583	c3		.
	defb 049h		;2584	49		I
	defb 0ceh		;2585	ce		.
	defb 04fh		;2586	4f		O
	defb 055h		;2587	55		U
	defb 0d4h		;2588	d4		.
	defb 045h		;2589	45		E
	defb 0d8h		;258a	d8		.
	defb 04ch		;258b	4c		L
	defb 0c4h		;258c	c4		.
	defb 04fh		;258d	4f		O
	defb 052h		;258e	52		R
	defb 0c7h		;258f	c7		.
	defb 041h		;2590	41		A
	defb 04eh		;2591	4e		N
	defb 0c4h		;2592	c4		.
	defb 04fh		;2593	4f		O
	defb 0d2h		;2594	d2		.
	defb 058h		;2595	58		X
	defb 04fh		;2596	4f		O
	defb 0d2h		;2597	d2		.
	defb 053h		;2598	53		S
	defb 055h		;2599	55		U
	defb 0c2h		;259a	c2		.
	defb 043h		;259b	43		C
	defb 0d0h		;259c	d0		.
	defb 050h		;259d	50		P
	defb 055h		;259e	55		U
	defb 053h		;259f	53		S
	defb 0c8h		;25a0	c8		.
	defb 050h		;25a1	50		P
	defb 04fh		;25a2	4f		O
	defb 0d0h		;25a3	d0		.
	defb 044h		;25a4	44		D
	defb 04ah		;25a5	4a		J
	defb 04eh		;25a6	4e		N
	defb 0dah		;25a7	da		.
	defb 052h		;25a8	52		R
	defb 045h		;25a9	45		E
	defb 0d4h		;25aa	d4		.
	defb 042h		;25ab	42		B
	defb 049h		;25ac	49		I
	defb 0d4h		;25ad	d4		.
	defb 053h		;25ae	53		S
	defb 045h		;25af	45		E
	defb 0d4h		;25b0	d4		.
	defb 052h		;25b1	52		R
	defb 045h		;25b2	45		E
	defb 0d3h		;25b3	d3		.
	defb 052h		;25b4	52		R
	defb 04ch		;25b5	4c		L
	defb 0c3h		;25b6	c3		.
	defb 052h		;25b7	52		R
	defb 0cch		;25b8	cc		.
	defb 052h		;25b9	52		R
	defb 052h		;25ba	52		R
	defb 0c3h		;25bb	c3		.
	defb 052h		;25bc	52		R
	defb 0d2h		;25bd	d2		.
	defb 053h		;25be	53		S
	defb 04ch		;25bf	4c		L
	defb 0c1h		;25c0	c1		.
	defb 053h		;25c1	53		S
	defb 052h		;25c2	52		R
	defb 0c1h		;25c3	c1		.
	defb 053h		;25c4	53		S
	defb 052h		;25c5	52		R
	defb 0cch		;25c6	cc		.
	defb 049h		;25c7	49		I
	defb 0cdh		;25c8	cd		.
	defb 052h		;25c9	52		R
	defb 053h		;25ca	53		S
	defb 0d4h		;25cb	d4		.
	defb 044h		;25cc	44		D
	defb 0c2h		;25cd	c2		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; OpcodeClassTable: class/length/control-flow bytes for every
; opcode (including the xxCB pages) - input to
; AnalyzeInstruction.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MnemonicTable_end:
OpcodeClassTable:

; BLOCK 'OpcodeClassTable' (start 0x25ce end 0x27b2)
OpcodeClassTable_start:
	defb 053h		;25ce	53		S
	defb 04ch		;25cf	4c		L
	defb 0c9h		;25d0	c9		.
	defb 000h		;25d1	00		.
	defb 007h		;25d2	07		.
	defb 001h		;25d3	01		.
	defb 05eh		;25d4	5e		^
	defb 026h		;25d5	26		&
	defb 038h		;25d6	38		8
	defb 001h		;25d7	01		.
	defb 05eh		;25d8	5e		^
	defb 026h		;25d9	26		&
	defb 038h		;25da	38		8
	defb 001h		;25db	01		.
	defb 066h		;25dc	66		f
	defb 026h		;25dd	26		&
	defb 030h		;25de	30		0
	defb 001h		;25df	01		.
	defb 06eh		;25e0	6e		n
	defb 026h		;25e1	26		&
	defb 030h		;25e2	30		0
	defb 001h		;25e3	01		.
	defb 072h		;25e4	72		r
	defb 026h		;25e5	26		&
	defb 038h		;25e6	38		8
	defb 001h		;25e7	01		.
	defb 076h		;25e8	76		v
	defb 026h		;25e9	26		&
	defb 038h		;25ea	38		8
	defb 001h		;25eb	01		.
	defb 07eh		;25ec	7e		~
	defb 026h		;25ed	26		&
	defb 038h		;25ee	38		8
	defb 001h		;25ef	01		.
	defb 086h		;25f0	86		.
	defb 026h		;25f1	26		&
	defb 0c0h		;25f2	c0		.
	defb 002h		;25f3	02		.
	defb 08eh		;25f4	8e		.
	defb 026h		;25f5	26		&
	defb 038h		;25f6	38		8
	defb 001h		;25f7	01		.
	defb 096h		;25f8	96		.
	defb 026h		;25f9	26		&
	defb 038h		;25fa	38		8
	defb 001h		;25fb	01		.
	defb 09eh		;25fc	9e		.
	defb 026h		;25fd	26		&
	defb 00fh		;25fe	0f		.
	defb 002h		;25ff	02		.
	defb 0a6h		;2600	a6		.
	defb 026h		;2601	26		&
	defb 038h		;2602	38		8
	defb 002h		;2603	02		.
	defb 0c6h		;2604	c6		.
	defb 026h		;2605	26		&
	defb 038h		;2606	38		8
	defb 001h		;2607	01		.
	defb 0d6h		;2608	d6		.
	defb 026h		;2609	26		&
	defb 030h		;260a	30		0
	defb 001h		;260b	01		.
	defb 0deh		;260c	de		.
	defb 026h		;260d	26		&
	defb 030h		;260e	30		0
	defb 002h		;260f	02		.
	defb 0e2h		;2610	e2		.
	defb 026h		;2611	26		&
	defb 030h		;2612	30		0
	defb 002h		;2613	02		.
	defb 0eah		;2614	ea		.
	defb 026h		;2615	26		&
	defb 00fh		;2616	0f		.
	defb 002h		;2617	02		.
	defb 0f2h		;2618	f2		.
	defb 026h		;2619	26		&
	defb 038h		;261a	38		8
	defb 002h		;261b	02		.
	defb 012h		;261c	12		.
	defb 027h		;261d	27		'
	defb 030h		;261e	30		0
	defb 002h		;261f	02		.
	defb 022h		;2620	22		"
	defb 027h		;2621	27		'
	defb 030h		;2622	30		0
	defb 002h		;2623	02		.
	defb 02ah		;2624	2a		*
	defb 027h		;2625	27		'
	defb 0c0h		;2626	c0		.
	defb 002h		;2627	02		.
	defb 032h		;2628	32		2
	defb 027h		;2629	27		'
	defb 038h		;262a	38		8
	defb 001h		;262b	01		.
	defb 03ah		;262c	3a		:
	defb 027h		;262d	27		'
	defb 0e0h		;262e	e0		.
	defb 001h		;262f	01		.
	defb 042h		;2630	42		B
	defb 027h		;2631	27		'
	defb 00fh		;2632	0f		.
	defb 002h		;2633	02		.
	defb 04ah		;2634	4a		J
	defb 027h		;2635	27		'
	defb 007h		;2636	07		.
	defb 001h		;2637	01		.
	defb 06ah		;2638	6a		j
	defb 027h		;2639	27		'
	defb 038h		;263a	38		8
	defb 001h		;263b	01		.
	defb 072h		;263c	72		r
	defb 027h		;263d	27		'
	defb 038h		;263e	38		8
	defb 001h		;263f	01		.
	defb 07ah		;2640	7a		z
	defb 027h		;2641	27		'
	defb 038h		;2642	38		8
	defb 001h		;2643	01		.
	defb 082h		;2644	82		.
	defb 027h		;2645	27		'
	defb 038h		;2646	38		8
	defb 001h		;2647	01		.
	defb 08ah		;2648	8a		.
	defb 027h		;2649	27		'
	defb 038h		;264a	38		8
	defb 002h		;264b	02		.
	defb 092h		;264c	92		.
	defb 027h		;264d	27		'
	defb 018h		;264e	18		.
	defb 001h		;264f	01		.
	defb 0a2h		;2650	a2		.
	defb 027h		;2651	27		'
	defb 018h		;2652	18		.
	defb 001h		;2653	01		.
	defb 0a6h		;2654	a6		.
	defb 027h		;2655	27		'
	defb 018h		;2656	18		.
	defb 001h		;2657	01		.
	defb 0aah		;2658	aa		.
	defb 027h		;2659	27		'
	defb 018h		;265a	18		.
	defb 001h		;265b	01		.
	defb 0aeh		;265c	ae		.
	defb 027h		;265d	27		'
	defb 00ch		;265e	0c		.
	defb 00dh		;265f	0d		.
	defb 00eh		;2660	0e		.
	defb 00fh		;2661	0f		.
	defb 010h		;2662	10		.
	defb 011h		;2663	11		.
	defb 022h		;2664	22		"
	defb 00bh		;2665	0b		.
	defb 00ch		;2666	0c		.
	defb 00dh		;2667	0d		.
	defb 00eh		;2668	0e		.
	defb 00fh		;2669	0f		.
	defb 010h		;266a	10		.
	defb 011h		;266b	11		.
	defb 0eah		;266c	ea		.
	defb 00bh		;266d	0b		.
	defb 005h		;266e	05		.
	defb 006h		;266f	06		.
	defb 007h		;2670	07		.
	defb 008h		;2671	08		.
	defb 005h		;2672	05		.
	defb 006h		;2673	06		.
	defb 007h		;2674	07		.
	defb 00ah		;2675	0a		.
	defb 014h		;2676	14		.
	defb 015h		;2677	15		.
	defb 016h		;2678	16		.
	defb 00dh		;2679	0d		.
	defb 018h		;267a	18		.
	defb 019h		;267b	19		.
	defb 01ah		;267c	1a		.
	defb 017h		;267d	17		.
	defb 083h		;267e	83		.
	defb 082h		;267f	82		.
	defb 085h		;2680	85		.
	defb 084h		;2681	84		.
	defb 089h		;2682	89		.
	defb 088h		;2683	88		.
	defb 087h		;2684	87		.
	defb 086h		;2685	86		.
	defb 092h		;2686	92		.
	defb 091h		;2687	91		.
	defb 094h		;2688	94		.
	defb 093h		;2689	93		.
	defb 098h		;268a	98		.
	defb 097h		;268b	97		.
	defb 096h		;268c	96		.
	defb 095h		;268d	95		.
	defb 0d2h		;268e	d2		.
	defb 000h		;268f	00		.
	defb 0d0h		;2690	d0		.
	defb 0c7h		;2691	c7		.
	defb 0d1h		;2692	d1		.
	defb 0c7h		;2693	c7		.
	defb 0d8h		;2694	d8		.
	defb 000h		;2695	00		.
	defb 0b6h		;2696	b6		.
	defb 0b7h		;2697	b7		.
	defb 0b8h		;2698	b8		.
	defb 0b9h		;2699	b9		.
	defb 0bah		;269a	ba		.
	defb 0bbh		;269b	bb		.
	defb 0afh		;269c	af		.
	defb 0b5h		;269d	b5		.
	defb 09bh		;269e	9b		.
	defb 099h		;269f	99		.
	defb 061h		;26a0	61		a
	defb 09fh		;26a1	9f		.
	defb 05eh		;26a2	5e		^
	defb 060h		;26a3	60		`
	defb 05fh		;26a4	5f		_
	defb 062h		;26a5	62		b
	defb 0d3h		;26a6	d3		.
	defb 000h		;26a7	00		.
	defb 0d5h		;26a8	d5		.
	defb 0ebh		;26a9	eb		.
	defb 0d6h		;26aa	d6		.
	defb 000h		;26ab	00		.
	defb 052h		;26ac	52		R
	defb 0cah		;26ad	ca		.
	defb 052h		;26ae	52		R
	defb 0c8h		;26af	c8		.
	defb 051h		;26b0	51		Q
	defb 0c8h		;26b1	c8		.
	defb 0d0h		;26b2	d0		.
	defb 0ech		;26b3	ec		.
	defb 0d4h		;26b4	d4		.
	defb 000h		;26b5	00		.
	defb 0d3h		;26b6	d3		.
	defb 000h		;26b7	00		.
	defb 09ch		;26b8	9c		.
	defb 0cah		;26b9	ca		.
	defb 0d7h		;26ba	d7		.
	defb 000h		;26bb	00		.
	defb 051h		;26bc	51		Q
	defb 0cah		;26bd	ca		.
	defb 052h		;26be	52		R
	defb 0c8h		;26bf	c8		.
	defb 051h		;26c0	51		Q
	defb 0c8h		;26c1	c8		.
	defb 0d0h		;26c2	d0		.
	defb 0ech		;26c3	ec		.
	defb 0d4h		;26c4	d4		.
	defb 000h		;26c5	00		.
	defb 033h		;26c6	33		3
	defb 000h		;26c7	00		.
	defb 0abh		;26c8	ab		.
	defb 009h		;26c9	09		.
	defb 065h		;26ca	65		e
	defb 0edh		;26cb	ed		.
	defb 054h		;26cc	54		T
	defb 0edh		;26cd	ed		.
	defb 08eh		;26ce	8e		.
	defb 0edh		;26cf	ed		.
	defb 08dh		;26d0	8d		.
	defb 0edh		;26d1	ed		.
	defb 090h		;26d2	90		.
	defb 0edh		;26d3	ed		.
	defb 08fh		;26d4	8f		.
	defb 0edh		;26d5	ed		.
	defb 035h		;26d6	35		5
	defb 037h		;26d7	37		7
	defb 034h		;26d8	34		4
	defb 036h		;26d9	36		6
	defb 02eh		;26da	2e		.
	defb 02dh		;26db	2d		-
	defb 038h		;26dc	38		8
	defb 02ch		;26dd	2c		,
	defb 0bch		;26de	bc		.
	defb 0bdh		;26df	bd		.
	defb 0beh		;26e0	be		.
	defb 0c3h		;26e1	c3		.
	defb 0adh		;26e2	ad		.
	defb 00bh		;26e3	0b		.
	defb 0aeh		;26e4	ae		.
	defb 00bh		;26e5	0b		.
	defb 0b4h		;26e6	b4		.
	defb 007h		;26e7	07		.
	defb 0b4h		;26e8	b4		.
	defb 00bh		;26e9	0b		.
	defb 0b5h		;26ea	b5		.
	defb 020h		;26eb	20		 
	defb 0b5h		;26ec	b5		.
	defb 021h		;26ed	21		!
	defb 0beh		;26ee	be		.
	defb 02bh		;26ef	2b		+
	defb 0b5h		;26f0	b5		.
	defb 02bh		;26f1	2b		+
	defb 066h		;26f2	66		f
	defb 0cch		;26f3	cc		.
	defb 064h		;26f4	64		d
	defb 0cbh		;26f5	cb		.
	defb 0cdh		;26f6	cd		.
	defb 0ebh		;26f7	eb		.
	defb 0d9h		;26f8	d9		.
	defb 000h		;26f9	00		.
	defb 0ceh		;26fa	ce		.
	defb 0ebh		;26fb	eb		.
	defb 063h		;26fc	63		c
	defb 0cbh		;26fd	cb		.
	defb 0d1h		;26fe	d1		.
	defb 0ech		;26ff	ec		.
	defb 072h		;2700	72		r
	defb 000h		;2701	00		.
	defb 066h		;2702	66		f
	defb 0cch		;2703	cc		.
	defb 0dah		;2704	da		.
	defb 000h		;2705	00		.
	defb 0cdh		;2706	cd		.
	defb 0ebh		;2707	eb		.
	defb 0d9h		;2708	d9		.
	defb 000h		;2709	00		.
	defb 0ceh		;270a	ce		.
	defb 0ebh		;270b	eb		.
	defb 0dbh		;270c	db		.
	defb 000h		;270d	00		.
	defb 0d1h		;270e	d1		.
	defb 0ech		;270f	ec		.
	defb 072h		;2710	72		r
	defb 000h		;2711	00		.
	defb 053h		;2712	53		S
	defb 0ebh		;2713	eb		.
	defb 0eah		;2714	ea		.
	defb 000h		;2715	00		.
	defb 0a9h		;2716	a9		.
	defb 00bh		;2717	0b		.
	defb 0a1h		;2718	a1		.
	defb 02bh		;2719	2b		+
	defb 0ach		;271a	ac		.
	defb 007h		;271b	07		.
	defb 0eah		;271c	ea		.
	defb 000h		;271d	00		.
	defb 02fh		;271e	2f		/
	defb 000h		;271f	00		.
	defb 030h		;2720	30		0
	defb 000h		;2721	00		.
	defb 066h		;2722	66		f
	defb 000h		;2723	00		.
	defb 031h		;2724	31		1
	defb 000h		;2725	00		.
	defb 08ah		;2726	8a		.
	defb 000h		;2727	00		.
	defb 0c3h		;2728	c3		.
	defb 007h		;2729	07		.
	defb 055h		;272a	55		U
	defb 0ebh		;272b	eb		.
	defb 0eah		;272c	ea		.
	defb 000h		;272d	00		.
	defb 0eah		;272e	ea		.
	defb 000h		;272f	00		.
	defb 0eah		;2730	ea		.
	defb 000h		;2731	00		.
	defb 0ddh		;2732	dd		.
	defb 0c7h		;2733	c7		.
	defb 067h		;2734	67		g
	defb 0c7h		;2735	c7		.
	defb 069h		;2736	69		i
	defb 0c7h		;2737	c7		.
	defb 068h		;2738	68		h
	defb 0c7h		;2739	c7		.
	defb 06ah		;273a	6a		j
	defb 06ch		;273b	6c		l
	defb 06bh		;273c	6b		k
	defb 06dh		;273d	6d		m
	defb 06eh		;273e	6e		n
	defb 06fh		;273f	6f		o
	defb 074h		;2740	74		t
	defb 070h		;2741	70		p
	defb 0eah		;2742	ea		.
	defb 0eah		;2743	ea		.
	defb 0dfh		;2744	df		.
	defb 0dfh		;2745	df		.
	defb 0eah		;2746	ea		.
	defb 0e0h		;2747	e0		.
	defb 0eah		;2748	ea		.
	defb 0eah		;2749	ea		.
	defb 0e1h		;274a	e1		.
	defb 026h		;274b	26		&
	defb 0a8h		;274c	a8		.
	defb 0c9h		;274d	c9		.
	defb 0a0h		;274e	a0		.
	defb 0cah		;274f	ca		.
	defb 0b4h		;2750	b4		.
	defb 0cah		;2751	ca		.
	defb 0e2h		;2752	e2		.
	defb 000h		;2753	00		.
	defb 0e3h		;2754	e3		.
	defb 000h		;2755	00		.
	defb 0e4h		;2756	e4		.
	defb 000h		;2757	00		.
	defb 0e5h		;2758	e5		.
	defb 000h		;2759	00		.
	defb 0e1h		;275a	e1		.
	defb 026h		;275b	26		&
	defb 0a8h		;275c	a8		.
	defb 0c9h		;275d	c9		.
	defb 09ah		;275e	9a		.
	defb 0cah		;275f	ca		.
	defb 0d5h		;2760	d5		.
	defb 02bh		;2761	2b		+
	defb 0e2h		;2762	e2		.
	defb 000h		;2763	00		.
	defb 0e3h		;2764	e3		.
	defb 000h		;2765	00		.
	defb 0e4h		;2766	e4		.
	defb 000h		;2767	00		.
	defb 0e5h		;2768	e5		.
	defb 000h		;2769	00		.
	defb 0e6h		;276a	e6		.
	defb 0e7h		;276b	e7		.
	defb 0e8h		;276c	e8		.
	defb 0e9h		;276d	e9		.
	defb 0eah		;276e	ea		.
	defb 0eah		;276f	ea		.
	defb 0eah		;2770	ea		.
	defb 0eah		;2771	ea		.
	defb 0a2h		;2772	a2		.
	defb 0a3h		;2773	a3		.
	defb 0a4h		;2774	a4		.
	defb 0a5h		;2775	a5		.
	defb 0a6h		;2776	a6		.
	defb 0a7h		;2777	a7		.
	defb 0eah		;2778	ea		.
	defb 0a1h		;2779	a1		.
	defb 04dh		;277a	4d		M
	defb 04eh		;277b	4e		N
	defb 04eh		;277c	4e		N
	defb 04eh		;277d	4e		N
	defb 04eh		;277e	4e		N
	defb 04eh		;277f	4e		N
	defb 04eh		;2780	4e		N
	defb 04eh		;2781	4e		N
	defb 04ch		;2782	4c		L
	defb 04bh		;2783	4b		K
	defb 0eah		;2784	ea		.
	defb 0eah		;2785	ea		.
	defb 0eah		;2786	ea		.
	defb 0eah		;2787	ea		.
	defb 0eah		;2788	ea		.
	defb 0eah		;2789	ea		.
	defb 071h		;278a	71		q
	defb 0eah		;278b	ea		.
	defb 071h		;278c	71		q
	defb 071h		;278d	71		q
	defb 0eah		;278e	ea		.
	defb 0eah		;278f	ea		.
	defb 0eah		;2790	ea		.
	defb 0eah		;2791	ea		.
	defb 0c1h		;2792	c1		.
	defb 00bh		;2793	0b		.
	defb 0c2h		;2794	c2		.
	defb 00bh		;2795	0b		.
	defb 0b5h		;2796	b5		.
	defb 01bh		;2797	1b		.
	defb 0b5h		;2798	b5		.
	defb 01ch		;2799	1c		.
	defb 04ah		;279a	4a		J
	defb 000h		;279b	00		.
	defb 049h		;279c	49		I
	defb 000h		;279d	00		.
	defb 0eah		;279e	ea		.
	defb 000h		;279f	00		.
	defb 0eah		;27a0	ea		.
	defb 000h		;27a1	00		.
	defb 041h		;27a2	41		A
	defb 042h		;27a3	42		B
	defb 039h		;27a4	39		9
	defb 03ah		;27a5	3a		:
	defb 043h		;27a6	43		C
	defb 02ch		;27a7	2c		,
	defb 03bh		;27a8	3b		;
	defb 03ch		;27a9	3c		<
	defb 045h		;27aa	45		E
	defb 046h		;27ab	46		F
	defb 03dh		;27ac	3d		=
	defb 03eh		;27ad	3e		>
	defb 047h		;27ae	47		G
	defb 048h		;27af	48		H
	defb 03fh		;27b0	3f		?
	defb 040h		;27b1	40		@
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EditorKeyTable: key code -> editor action (see
; EditorKeyLoop $2171).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
OpcodeClassTable_end:
EditorKeyTable:

; BLOCK 'EditorKeyTable' (start 0x27b2 end 0x27e2)
EditorKeyTable_start:
	defb 04eh		;27b2	4e		N
	defb 074h		;27b3	74		t
	defb 0bbh		;27b4	bb		.
	defb 0bah		;27b5	ba		.
	defb 011h		;27b6	11		.
	defb 010h		;27b7	10		.
	defb 031h		;27b8	31		1
	defb 0abh		;27b9	ab		.
	defb 014h		;27ba	14		.
	defb 015h		;27bb	15		.
	defb 00dh		;27bc	0d		.
	defb 016h		;27bd	16		.
	defb 01ah		;27be	1a		.
	defb 017h		;27bf	17		.
	defb 018h		;27c0	18		.
	defb 019h		;27c1	19		.
	defb 066h		;27c2	66		f
	defb 04ch		;27c3	4c		L
	defb 04bh		;27c4	4b		K
	defb 007h		;27c5	07		.
	defb 0beh		;27c6	be		.
	defb 08ah		;27c7	8a		.
	defb 09ch		;27c8	9c		.
	defb 022h		;27c9	22		"
	defb 0afh		;27ca	af		.
l27cbh:
	defb 071h		;27cb	71		q
	defb 072h		;27cc	72		r
	defb 067h		;27cd	67		g
	defb 068h		;27ce	68		h
	defb 069h		;27cf	69		i
	defb 02bh		;27d0	2b		+
	defb 0b4h		;27d1	b4		.
	defb 0a9h		;27d2	a9		.
	defb 0ech		;27d3	ec		.
	defb 0ebh		;27d4	eb		.
	defb 0edh		;27d5	ed		.
	defb 0b2h		;27d6	b2		.
	defb 027h		;27d7	27		'
	defb 09dh		;27d8	9d		.
	defb 08bh		;27d9	8b		.
	defb 0c0h		;27da	c0		.
	defb 012h		;27db	12		.
	defb 0b0h		;27dc	b0		.
	defb 029h		;27dd	29		)
	defb 09eh		;27de	9e		.
	defb 08ch		;27df	8c		.
	defb 0bfh		;27e0	bf		.
	defb 013h		;27e1	13		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegisterKeyTable: register-edit keys (see EditorRegKeys
; $2261).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EditorKeyTable_end:
RegisterKeyTable:

; BLOCK 'RegisterKeyTable' (start 0x27e2 end 0x2818)
RegisterKeyTable_start:
	defb 049h		;27e2	49		I
	defb 04ah		;27e3	4a		J
	defb 0b4h		;27e4	b4		.
	defb 0aeh		;27e5	ae		.
	defb 0adh		;27e6	ad		.
	defb 0afh		;27e7	af		.
	defb 02bh		;27e8	2b		+
	defb 021h		;27e9	21		!
	defb 020h		;27ea	20		 
	defb 022h		;27eb	22		"
l27ech:
	defb 052h		;27ec	52		R
	defb 051h		;27ed	51		Q
	defb 06ah		;27ee	6a		j
	defb 06ch		;27ef	6c		l
	defb 06bh		;27f0	6b		k
	defb 06dh		;27f1	6d		m
	defb 06eh		;27f2	6e		n
	defb 06fh		;27f3	6f		o
	defb 070h		;27f4	70		p
sub_27f5h:
	defb 0cdh		;27f5	cd		.
	defb 0f6h		;27f6	f6		.
	defb 011h		;27f7	11		.
	defb 0fdh		;27f8	fd		.
	defb 036h		;27f9	36		6
	defb 00ah		;27fa	0a		.
	defb 000h		;27fb	00		.
	defb 011h		;27fc	11		.
	defb 0f7h		;27fd	f7		.
	defb 0e6h		;27fe	e6		.
	defb 0ddh		;27ff	dd		.
	defb 021h		;2800	21		!
	defb 0f7h		;2801	f7		.
	defb 0e5h		;2802	e5		.
	defb 0c9h		;2803	c9		.
	defb 0cdh		;2804	cd		.
	defb 0f5h		;2805	f5		.
	defb 027h		;2806	27		'
	defb 0dfh		;2807	df		.
	defb 051h		;2808	51		Q
	defb 03ch		;2809	3c		<
	defb 0d2h		;280a	d2		.
	defb 013h		;280b	13		.
	defb 00eh		;280c	0e		.
l280dh:
	defb 0cdh		;280d	cd		.
	defb 0a2h		;280e	a2		.
	defb 034h		;280f	34		4
l2810h:
	defb 0c3h		;2810	c3		.
	defb 01eh		;2811	1e		.
	defb 00eh		;2812	0e		.
	defb 006h		;2813	06		.
	defb 002h		;2814	02		.
	defb 0cdh		;2815	cd		.
	defb 02ch		;2816	2c		,
	defb 028h		;2817	28		(
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; HelpCode2818: evaluator/help code that an earlier
; sweep had mistyped as data - it calls
; FillMemoryRange, evaluates expressions and prints
; inline strings (Hel+$F0 prints Help) between
; RegisterKeyTable and the message blocks.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RegisterKeyTable_end:

; BLOCK 'HelpCode2818' (start 0x2818 end 0x298c)
HelpCode2818_start:
	call FillMemoryRange	;2818	cd 73 28	. s (
l281bh:
	jr c,l280dh		;281b	38 f0		8 .
	rst 18h			;281d	df		.
	and h			;281e	a4		.
	ld a,(0e818h)		;281f	3a 18 e8	: . .
	ld b,001h		;2822	06 01		. .
	call sub_282ch		;2824	cd 2c 28	. , (
l2827h:
	call sub_2863h		;2827	cd 63 28	. c (
	jr l281bh		;282a	18 ef		. .
sub_282ch:
	call sub_27f5h		;282c	cd f5 27	. . '
	rst 18h			;282f	df		.
	add a,b			;2830	80		.
	ld a,(0d938h)		;2831	3a 38 d9	: 8 .
	call sub_11cah		;2834	cd ca 11	. . .
	cp 002h			;2837	fe 02		. .
	jr z,l2847h		;2839	28 0c		( .
	jr nc,l285fh		;283b	30 22		0 "
	bit 0,(ix+017h)		;283d	dd cb 17 46	. . . F
	jr nz,l2847h		;2841	20 04		  .
	ld a,00ah		;2843	3e 0a		> .
	jr l2810h		;2845	18 c9		. .
l2847h:
	push hl			;2847	e5		.
	push de			;2848	d5		.
	push af			;2849	f5		.
	rst 18h			;284a	df		.
	rra			;284b	1f		.
	ld a,(0be38h)		;284c	3a 38 be	: 8 .
	ld e,c			;284f	59		Y
	ld d,b			;2850	50		P
	pop af			;2851	f1		.
	pop hl			;2852	e1		.
	pop bc			;2853	c1		.
	cp 001h			;2854	fe 01		. .
	jr z,l285ch		;2856	28 04		( .
	ret nc			;2858	d0		.
	ld hl,(0e600h)		;2859	2a 00 e6	* . .
l285ch:
	ld b,d			;285c	42		B
	ld c,e			;285d	4b		K
	ret			;285e	c9		.
l285fh:
	ld a,00bh		;285f	3e 0b		> .
	jr l2810h		;2861	18 ad		. .
sub_2863h:
	push hl			;2863	e5		.
	push bc			;2864	c5		.
	rst 18h			;2865	df		.
	dec c			;2866	0d		.
	dec sp			;2867	3b		;
	pop bc			;2868	c1		.
	pop hl			;2869	e1		.
	ret c			;286a	d8		.
	rst 0			;286b	c7		.
	inc hl			;286c	23		#
	dec bc			;286d	0b		.
	ld a,b			;286e	78		x
	or c			;286f	b1		.
	jr nz,sub_2863h		;2870	20 f1		  .
	ret			;2872	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FillMemoryRange: fill through the E600 pointer with a
; pattern.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FillMemoryRange:
	ld (0e600h),hl		;2873	22 00 e6	" . .
l2876h:
	push hl			;2876	e5		.
	push bc			;2877	c5		.
	rst 28h			;2878	ef		.
	rst 18h			;2879	df		.
	ld e,c			;287a	59		Y
	dec sp			;287b	3b		;
	pop bc			;287c	c1		.
	pop hl			;287d	e1		.
	ret c			;287e	d8		.
	inc hl			;287f	23		#
	dec bc			;2880	0b		.
	ld a,b			;2881	78		x
	or c			;2882	b1		.
	jr nz,l2876h		;2883	20 f1		  .
	ret			;2885	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ChecksumLoop: running sum over a range - also used to
; calibrate AY timing.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ChecksumLoop:
	xor a			;2886	af		.
	ret			;2887	c9		.
	ld l,a			;2888	6f		o
	ld h,a			;2889	67		g
	ld c,a			;288a	4f		O
	ld b,a			;288b	47		G
	ex af,af'		;288c	08		.
	ld de,l00f6h		;288d	11 f6 00	. . .
	set 6,b			;2890	cb f0		. .
	dec bc			;2892	0b		.
l2893h:
	ex af,af'		;2893	08		.
	add a,(hl)		;2894	86		.
	ex af,af'		;2895	08		.
	inc hl			;2896	23		#
	or a			;2897	b7		.
	sbc hl,de		;2898	ed 52		. R
	add hl,de		;289a	19		.
	jr nz,l289eh		;289b	20 01		  .
	inc hl			;289d	23		#
l289eh:
	dec bc			;289e	0b		.
	ld a,c			;289f	79		y
	or b			;28a0	b0		.
	jr nz,l2893h		;28a1	20 f0		  .
	ex af,af'		;28a3	08		.
	ex de,hl		;28a4	eb		.
	cp (hl)			;28a5	be		.
	ret z			;28a6	c8		.
	ld bc,l00feh		;28a7	01 fe 00	. . .
	ld hl,0c000h		;28aa	21 00 c0	! . .
	inc (hl)		;28ad	34		4
	ld l,(hl)		;28ae	6e		n
	out (c),l		;28af	ed 69		. i
	jr ChecksumLoop		;28b1	18 d3		. .
	ld c,b			;28b3	48		H
	ld h,l			;28b4	65		e
	ld l,h			;28b5	6c		l
	ret p			;28b6	f0		.
	ld c,c			;28b7	49		I
	call sub_00bfh		;28b8	cd bf 00	. . .
	add hl,hl		;28bb	29		)
	ld (hl),000h		;28bc	36 00		6 .
	adc a,d			;28be	8a		.
	ld (hl),001h		;28bf	36 01		6 .
	ld e,e			;28c1	5b		[
	dec hl			;28c2	2b		+
	ld bc,07a00h		;28c3	01 00 7a	. . z
	or a			;28c6	b7		.
	ld a,02ch		;28c7	3e 2c		> ,
	ret nz			;28c9	c0		.
	ld (iy+016h),e		;28ca	fd 73 16	. s .
	ld hl,(UserPc)		;28cd	2a 6b dd	* k .
	call l1f91h		;28d0	cd 91 1f	. . .
l28d3h:
	call sub_17c3h		;28d3	cd c3 17	. . .
	call sub_2f6ch		;28d6	cd 6c 2f	. l /
	jr l28d3h		;28d9	18 f8		. .
	ld hl,WatchTable	;28db	21 94 e3	! . .
	ld de,l2827h		;28de	11 27 28	. ' (
	ld bc,l2912h		;28e1	01 12 29	. . )
	ld a,009h		;28e4	3e 09		> .
	push bc			;28e6	c5		.
	push af			;28e7	f5		.
	push hl			;28e8	e5		.
	push de			;28e9	d5		.
	call InitErrorWindow	;28ea	cd f8 35	. . 5
	ld b,e			;28ed	43		C
	rst 8			;28ee	cf		.
	rst 20h			;28ef	e7		.
	dec c			;28f0	0d		.
	adc a,l			;28f1	8d		.
	pop bc			;28f2	c1		.
	rst 8			;28f3	cf		.
	rst 20h			;28f4	e7		.
	dec c			;28f5	0d		.
	adc a,l			;28f6	8d		.
	pop hl			;28f7	e1		.
	pop de			;28f8	d1		.
	ld e,001h		;28f9	1e 01		. .
	pop bc			;28fb	c1		.
l28fch:
	push de			;28fc	d5		.
	push bc			;28fd	c5		.
	call sub_35f6h		;28fe	cd f6 35	. . 5
	jr c,l290fh		;2901	38 0c		8 .
	call sub_2f6ch		;2903	cd 6c 2f	. l /
	rst 20h			;2906	e7		.
	adc a,l			;2907	8d		.
	pop bc			;2908	c1		.
	pop de			;2909	d1		.
	inc e			;290a	1c		.
	ld a,d			;290b	7a		z
	cp e			;290c	bb		.
	jr nz,l28fch		;290d	20 ed		  .
l290fh:
	ld a,081h		;290f	3e 81		> .
	ret			;2911	c9		.
l2912h:
	ld c,e			;2912	4b		K
	ld b,000h		;2913	06 00		. .
	push de			;2915	d5		.
	ld (iy+008h),020h	;2916	fd 36 08 20	. 6 .  
	ld (iy+007h),002h	;291a	fd 36 07 02	. 6 . .
	call sub_1d25h		;291e	cd 25 1d	. % .
	pop de			;2921	d1		.
	rst 20h			;2922	e7		.
	and b			;2923	a0		.
	push hl			;2924	e5		.
	bit 6,(hl)		;2925	cb 76		. v
	jr nz,l292eh		;2927	20 05		  .
	ld b,029h		;2929	06 29		. )
	rst 8			;292b	cf		.
	jr l2985h		;292c	18 57		. W
l292eh:
	push de			;292e	d5		.
	ld b,002h		;292f	06 02		. .
	call l1cabh		;2931	cd ab 1c	. . .
	rst 20h			;2934	e7		.
	and e			;2935	a3		.
	push hl			;2936	e5		.
	inc hl			;2937	23		#
	ld e,(hl)		;2938	5e		^
	push de			;2939	d5		.
	inc hl			;293a	23		#
	ld e,(hl)		;293b	5e		^
	inc hl			;293c	23		#
	ld d,(hl)		;293d	56		V
	ex de,hl		;293e	eb		.
	call PrintHexWord	;293f	cd 95 1d	. . .
	pop de			;2942	d1		.
	pop hl			;2943	e1		.
	ld b,004h		;2944	06 04		. .
	call l1cabh		;2946	cd ab 1c	. . .
	ld a,e			;2949	7b		{
	call sub_1da3h		;294a	cd a3 1d	. . .
	ld b,004h		;294d	06 04		. .
	call l1cabh		;294f	cd ab 1c	. . .
	ld a,(hl)		;2952	7e		~
	and 003h		;2953	e6 03		. .
	call sub_1da3h		;2955	cd a3 1d	. . .
	ld b,003h		;2958	06 03		. .
	call l1cabh		;295a	cd ab 1c	. . .
	bit 7,(hl)		;295d	cb 7e		. ~
	call HelpCode2818_end	;295f	cd 8c 29	. . )
	ld de,l0003h+2		;2962	11 05 00	. . .
	add hl,de		;2965	19		.
	ld c,(hl)		;2966	4e		N
	inc hl			;2967	23		#
	ld b,(hl)		;2968	46		F
	inc hl			;2969	23		#
	call sub_1d20h		;296a	cd 20 1d	.   .
	ld c,(hl)		;296d	4e		N
	inc hl			;296e	23		#
	ld b,(hl)		;296f	46		F
	inc hl			;2970	23		#
	call sub_1d18h		;2971	cd 18 1d	. . .
	ld b,002h		;2974	06 02		. .
	call l1cabh		;2976	cd ab 1c	. . .
	pop de			;2979	d1		.
	ld a,e			;297a	7b		{
	call LookupCommand	;297b	cd 9b 29	. . )
	ld a,02dh		;297e	3e 2d		> -
	jr z,l2984h		;2980	28 02		( .
	ld a,02bh		;2982	3e 2b		> +
l2984h:
	rst 10h			;2984	d7		.
l2985h:
	pop hl			;2985	e1		.
	ld de,l000bh		;2986	11 0b 00	. . .
	add hl,de		;2989	19		.
	or a			;298a	b7		.
	ret			;298b	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintOkError: "Ok" or the pending error after a command.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
HelpCode2818_end:
PrintOkError:
	push hl			;298c	e5		.
	jr nz,l2995h		;298d	20 06		  .
	rst 20h			;298f	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgOff: inline RST 20h string OF+$C6 which prints
; OFF - the terminating byte is emitted with bit 7
; stripped; used by the watch/breakpoint listing
; routine above.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgOff' (start 0x2990 end 0x2993)
MsgOff_start:
	defb 04fh		;2990	4f		O
	defb 046h		;2991	46		F
	defb 0c6h		;2992	c6		.
MsgOff_end:
	pop hl			;2993	e1		.
	ret			;2994	c9		.
l2995h:
	rst 20h			;2995	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgOn: ON+$A0, printing ON - companion of MsgOff
; ($2990).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgOn' (start 0x2996 end 0x2999)
MsgOn_start:
	defb 04fh		;2996	4f		O
	defb 04eh		;2997	4e		N
	defb 0a0h		;2998	a0		.
MsgOn_end:
	pop hl			;2999	e1		.
	ret			;299a	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; LookupCommand: walk CommandTable (E80B, 2-byte entries)
; matching the entered verb.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
LookupCommand:
	ld de,CommandTable	;299b	11 0b e8	. . .
	ld l,a			;299e	6f		o
	ld h,000h		;299f	26 00		& .
	add hl,hl		;29a1	29		)
	add hl,de		;29a2	19		.
	ld e,(hl)		;29a3	5e		^
	push hl			;29a4	e5		.
	inc hl			;29a5	23		#
	ld d,(hl)		;29a6	56		V
	ld l,a			;29a7	6f		o
	ld a,d			;29a8	7a		z
	or e			;29a9	b3		.
	ld a,l			;29aa	7d		}
	pop hl			;29ab	e1		.
	ret			;29ac	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckWatchpoints: after every step compare the 8 entries of
; WatchTable (E394, 11 bytes each: address, length, value...)
; against the visible banks; a hit re-enters the monitor.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckWatchpoints:
	res 7,(iy+00bh)		;29ad	fd cb 0b be	. . . .
	jr l29b7h		;29b1	18 04		. .
sub_29b3h:
	set 7,(iy+00bh)		;29b3	fd cb 0b fe	. . . .
l29b7h:
	call InitWatchTable	;29b7	cd f7 29	. . )
l29bah:
	call sub_2a01h		;29ba	cd 01 2a	. . *
	jr nc,l29f2h		;29bd	30 33		0 3
	call SavePagingShadow	;29bf	cd ae 2d	. . -
	exx			;29c2	d9		.
	ld a,h			;29c3	7c		|
	exx			;29c4	d9		.
	and 0c0h		;29c5	e6 c0		. .
	jr z,l29dbh		;29c7	28 12		( .
	bit 5,(iy-002h)		;29c9	fd cb fe 6e	. . . n
	jr z,l29d6h		;29cd	28 07		( .
	call sub_2ebah		;29cf	cd ba 2e	. . .
	exx			;29d2	d9		.
	jr nz,l29efh		;29d3	20 1a		  .
	exx			;29d5	d9		.
l29d6h:
	push bc			;29d6	c5		.
	call sub_2ee5h		;29d7	cd e5 2e	. . .
	pop bc			;29da	c1		.
l29dbh:
	exx			;29db	d9		.
	bit 7,(iy+00bh)		;29dc	fd cb 0b 7e	. . . ~
	jr z,l29ebh		;29e0	28 09		( .
	rst 28h			;29e2	ef		.
	ld (ix+004h),a		;29e3	dd 77 04	. w .
	ld a,0cfh		;29e6	3e cf		> .
	rst 0			;29e8	c7		.
	jr l29efh		;29e9	18 04		. .
l29ebh:
	ld a,(ix+004h)		;29eb	dd 7e 04	. ~ .
	rst 0			;29ee	c7		.
l29efh:
	call RestorePaging	;29ef	cd b1 35	. . 5
l29f2h:
	add ix,de		;29f2	dd 19		. .
	djnz l29bah		;29f4	10 c4		. .
	ret			;29f6	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; InitWatchTable: clear the watchpoint table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
InitWatchTable:
	ld ix,WatchTable	;29f7	dd 21 94 e3	. ! . .
	ld b,008h		;29fb	06 08		. .
	ld de,l000bh		;29fd	11 0b 00	. . .
	ret			;2a00	c9		.
sub_2a01h:
	ld a,(ix+000h)		;2a01	dd 7e 00	. ~ .
	rlca			;2a04	07		.
	ret nc			;2a05	d0		.
	ccf			;2a06	3f		?
	bit 1,a			;2a07	cb 4f		. O
	ret nz			;2a09	c0		.
	ld l,(ix+002h)		;2a0a	dd 6e 02	. n .
	ld h,(ix+003h)		;2a0d	dd 66 03	. f .
	exx			;2a10	d9		.
	ld l,(ix+001h)		;2a11	dd 6e 01	. n .
	ccf			;2a14	3f		?
	ret			;2a15	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FindWatchpoint: locate a free/owning slot in WatchTable.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FindWatchpoint:
	call InitWatchTable	;2a16	cd f7 29	. . )
l2a19h:
	ld a,(ix+000h)		;2a19	dd 7e 00	. ~ .
	rlca			;2a1c	07		.
	jr nc,l2a34h		;2a1d	30 15		0 .
	exx			;2a1f	d9		.
	ld l,(ix+001h)		;2a20	dd 6e 01	. n .
	call sub_2ebah		;2a23	cd ba 2e	. . .
	exx			;2a26	d9		.
	jr nz,l2a34h		;2a27	20 0b		  .
	ld a,(ix+002h)		;2a29	dd 7e 02	. ~ .
	cp l			;2a2c	bd		.
	jr nz,l2a34h		;2a2d	20 05		  .
	ld a,(ix+003h)		;2a2f	dd 7e 03	. ~ .
	cp h			;2a32	bc		.
	ret z			;2a33	c8		.
l2a34h:
	add ix,de		;2a34	dd 19		. .
	djnz l2a19h		;2a36	10 e1		. .
	scf			;2a38	37		7
	ret			;2a39	c9		.
	call sub_2a4dh		;2a3a	cd 4d 2a	. M *
	ld (ix+000h),000h	;2a3d	dd 36 00 00	. 6 . .
	ld a,(RegisterBackup)	;2a41	3a 99 dd	: . .
	call LookupCommand	;2a44	cd 9b 29	. . )
	xor a			;2a47	af		.
	ld (hl),a		;2a48	77		w
	inc hl			;2a49	23		#
	ld (hl),a		;2a4a	77		w
	jr l2a86h		;2a4b	18 39		. 9
sub_2a4dh:
	push af			;2a4d	f5		.
	dec e			;2a4e	1d		.
	ld a,e			;2a4f	7b		{
	and 0f8h		;2a50	e6 f8		. .
	or d			;2a52	b2		.
	ld a,022h		;2a53	3e 22		> "
	jr nz,l2a8bh		;2a55	20 34		  4
	push hl			;2a57	e5		.
	ld l,e			;2a58	6b		k
	ld h,d			;2a59	62		b
	add hl,hl		;2a5a	29		)
	add hl,hl		;2a5b	29		)
	add hl,de		;2a5c	19		.
	add hl,hl		;2a5d	29		)
	add hl,de		;2a5e	19		.
	ld de,WatchTable	;2a5f	11 94 e3	. . .
	add hl,de		;2a62	19		.
	push hl			;2a63	e5		.
	pop ix			;2a64	dd e1		. .
	pop hl			;2a66	e1		.
	pop af			;2a67	f1		.
	bit 6,(ix+000h)		;2a68	dd cb 00 76	. . . v
	ret			;2a6c	c9		.
	call sub_2a4dh		;2a6d	cd 4d 2a	. M *
	ld a,026h		;2a70	3e 26		> &
	jr z,l2a8bh		;2a72	28 17		( .
	ld a,l			;2a74	7d		}
	and 0feh		;2a75	e6 fe		. .
	or h			;2a77	b4		.
	ld a,023h		;2a78	3e 23		> #
	jr nz,l2a8bh		;2a7a	20 0f		  .
sub_2a7ch:
	sla (ix+000h)		;2a7c	dd cb 00 26	. . . &
	srl l			;2a80	cb 3d		. =
	rr (ix+000h)		;2a82	dd cb 00 1e	. . . .
l2a86h:
	ld a,081h		;2a86	3e 81		> .
	ret			;2a88	c9		.
l2a89h:
	ld a,00ah		;2a89	3e 0a		> .
l2a8bh:
	jp ExitToError		;2a8b	c3 1e 0e	. . .
sub_2a8eh:
	call sub_2a4dh		;2a8e	cd 4d 2a	. M *
	push ix			;2a91	dd e5		. .
	ld de,0e3edh		;2a93	11 ed e3	. . .
	ex de,hl		;2a96	eb		.
	push af			;2a97	f5		.
	ld a,e			;2a98	7b		{
	and 0fch		;2a99	e6 fc		. .
	or d			;2a9b	b2		.
	ld a,024h		;2a9c	3e 24		> $
	jr nz,l2a8bh		;2a9e	20 eb		  .
	pop af			;2aa0	f1		.
	cp 004h			;2aa1	fe 04		. .
	jr z,l2ac0h		;2aa3	28 1b		( .
	jr c,l2a89h		;2aa5	38 e2		8 .
	cp 006h			;2aa7	fe 06		. .
	ld a,00bh		;2aa9	3e 0b		> .
	jr nc,l2a8bh		;2aab	30 de		0 .
	push hl			;2aad	e5		.
	ld hl,(0dda1h)		;2aae	2a a1 dd	* . .
	ld a,l			;2ab1	7d		}
	and 0f0h		;2ab2	e6 f0		. .
	or h			;2ab4	b4		.
	ld a,018h		;2ab5	3e 18		> .
	jr nz,l2a8bh		;2ab7	20 d2		  .
	ld a,l			;2ab9	7d		}
	pop hl			;2aba	e1		.
	ld (hl),a		;2abb	77		w
	set 5,e			;2abc	cb eb		. .
	jr l2ac4h		;2abe	18 04		. .
l2ac0h:
	call ScreenBankCheck	;2ac0	cd ac 2e	. . .
	ld (hl),a		;2ac3	77		w
l2ac4h:
	bit 0,e			;2ac4	cb 43		. C
	inc hl			;2ac6	23		#
	jr nz,l2ad0h		;2ac7	20 07		  .
	ld a,b			;2ac9	78		x
	and 0c0h		;2aca	e6 c0		. .
	ld a,025h		;2acc	3e 25		> %
	jr z,l2a8bh		;2ace	28 bb		( .
l2ad0h:
	ld (hl),c		;2ad0	71		q
	inc hl			;2ad1	23		#
	ld (hl),b		;2ad2	70		p
	inc hl			;2ad3	23		#
	inc hl			;2ad4	23		#
	ld bc,(0dd9fh)		;2ad5	ed 4b 9f dd	. K . .
	ld (hl),c		;2ad9	71		q
	inc hl			;2ada	23		#
	ld (hl),b		;2adb	70		p
	inc hl			;2adc	23		#
	ld (hl),c		;2add	71		q
	inc hl			;2ade	23		#
	ld (hl),b		;2adf	70		p
	inc hl			;2ae0	23		#
	xor a			;2ae1	af		.
	ld (hl),a		;2ae2	77		w
	inc hl			;2ae3	23		#
	ld (hl),a		;2ae4	77		w
	ld hl,0e3ech		;2ae5	21 ec e3	! . .
	ld a,e			;2ae8	7b		{
	or 0c0h			;2ae9	f6 c0		. .
	ld (hl),a		;2aeb	77		w
	pop de			;2aec	d1		.
	ld bc,l000bh		;2aed	01 0b 00	. . .
	ldir			;2af0	ed b0		. .
	jr l2a86h		;2af2	18 92		. .
	ld de,Rst08Vector	;2af4	11 08 00	. . .
	ld bc,(0dd8bh)		;2af7	ed 4b 8b dd	. K . .
	ld a,b			;2afb	78		x
	and 0c0h		;2afc	e6 c0		. .
	ld hl,l0001h		;2afe	21 01 00	! . .
	ld (0dd9fh),hl		;2b01	22 9f dd	" . .
	jr z,l2b07h		;2b04	28 01		( .
	dec hl			;2b06	2b		+
l2b07h:
	ld a,004h		;2b07	3e 04		> .
	call sub_2a8eh		;2b09	cd 8e 2a	. . *
l2b0ch:
	jp l30ebh		;2b0c	c3 eb 30	. . 0
	ld de,Rst08Vector	;2b0f	11 08 00	. . .
	call sub_2a4dh		;2b12	cd 4d 2a	. M *
	ld l,000h		;2b15	2e 00		. .
	call sub_2a7ch		;2b17	cd 7c 2a	. | *
	jr l2b0ch		;2b1a	18 f0		. .
	ld a,e			;2b1c	7b		{
	and 0f8h		;2b1d	e6 f8		. .
	or d			;2b1f	b2		.
	ld a,00dh		;2b20	3e 0d		> .
	ret nz			;2b22	c0		.
	ld a,l			;2b23	7d		}
	and 0feh		;2b24	e6 fe		. .
	or h			;2b26	b4		.
	ld a,017h		;2b27	3e 17		> .
	ret nz			;2b29	c0		.
	inc h			;2b2a	24		$
	ld a,e			;2b2b	7b		{
l2b2ch:
	or a			;2b2c	b7		.
	jr z,l2b34h		;2b2d	28 05		( .
	dec a			;2b2f	3d		=
	sla h			;2b30	cb 24		. $
	jr l2b2ch		;2b32	18 f8		. .
l2b34h:
	ld a,h			;2b34	7c		|
	bit 0,l			;2b35	cb 45		. E
	jr nz,l2b44h		;2b37	20 0b		  .
	xor 0ffh		;2b39	ee ff		. .
	and (iy+005h)		;2b3b	fd a6 05	. . .
l2b3eh:
	ld (iy+005h),a		;2b3e	fd 77 05	. w .
	ld a,081h		;2b41	3e 81		> .
	ret			;2b43	c9		.
l2b44h:
	or (iy+005h)		;2b44	fd b6 05	. . .
	jr l2b3eh		;2b47	18 f5		. .
	ld a,h			;2b49	7c		|
	or a			;2b4a	b7		.
	ld a,011h		;2b4b	3e 11		> .
	ret nz			;2b4d	c0		.
	ld c,e			;2b4e	4b		K
	ld b,d			;2b4f	42		B
	out (c),l		;2b50	ed 69		. i
	ld a,081h		;2b52	3e 81		> .
	ret			;2b54	c9		.
	ld c,e			;2b55	4b		K
	ld b,d			;2b56	42		B
	in e,(c)		;2b57	ed 58		. X
	ld d,000h		;2b59	16 00		. .
sub_2b5bh:
	call PopupTabStops_end	;2b5b	cd 8d 2b	. . +
	ld c,e			;2b5e	4b		K
	ld b,d			;2b5f	42		B
	ld a,b			;2b60	78		x
	or a			;2b61	b7		.
	jr nz,l2b75h		;2b62	20 11		  .
	ld a,c			;2b64	79		y
	rlca			;2b65	07		.
	jr c,l2b75h		;2b66	38 0d		8 .
	rrca			;2b68	0f		.
	cp 020h			;2b69	fe 20		.  
	jr c,l2b75h		;2b6b	38 08		8 .
	push af			;2b6d	f5		.
	rst 20h			;2b6e	e7		.
	dec c			;2b6f	0d		.
	and d			;2b70	a2		.
	pop af			;2b71	f1		.
	rst 10h			;2b72	d7		.
	rst 20h			;2b73	e7		.
	and d			;2b74	a2		.
l2b75h:
	ld hl,PopupTabStops_start	;2b75	21 88 2b	! . +
l2b78h:
	ld a,(hl)		;2b78	7e		~
	cp 081h			;2b79	fe 81		. .
	ret z			;2b7b	c8		.
	push bc			;2b7c	c5		.
	push af			;2b7d	f5		.
	rst 20h			;2b7e	e7		.
	adc a,l			;2b7f	8d		.
	pop af			;2b80	f1		.
	call sub_1ce0h		;2b81	cd e0 1c	. . .
	pop bc			;2b84	c1		.
	inc hl			;2b85	23		#
	jr l2b78h		;2b86	18 f0		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PopupTabStops: four tab columns (0,2,4,6) closed
; by the $81 sentinel; the loop just above feeds
; each value to the print helper to lay out the
; popup menu line right before OpenPopup ($2B8D).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'PopupTabStops' (start 0x2b88 end 0x2b8d)
PopupTabStops_start:
	defb 000h		;2b88	00		.
	defb 002h		;2b89	02		.
	defb 004h		;2b8a	04		.
	defb 006h		;2b8b	06		.
	defb 081h		;2b8c	81		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; OpenPopup: pop up a window from the descriptor at
; PopupWindowDef (E06D).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PopupTabStops_end:
OpenPopup:
	push de			;2b8d	d5		.
	ld hl,PopupWindowDef	;2b8e	21 6d e0	! m .
	rst 30h			;2b91	f7		.
	call sub_0c31h		;2b92	cd 31 0c	. 1 .
	rst 20h			;2b95	e7		.
	adc a,l			;2b96	8d		.
	pop de			;2b97	d1		.
	ret			;2b98	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; TapeMenu: the tape command cluster - save/verify/leader
; state machine; control flow is dense here (cf. SaveToTape
; $35BA and the key waits at $2F3E+).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
TapeMenu:
	bit 5,(iy+005h)		;2b99	fd cb 05 6e	. . . n
	ret nz			;2b9d	c0		.
	call PopupTabStops_end	;2b9e	cd 8d 2b	. . +
	call SetErrorTables	;2ba1	cd ac 0d	. . .
	ld hl,l3b6fh		;2ba4	21 6f 3b	! o ;
	call PrintJustifiedHeader	;2ba7	cd 93 1c	. . .
	rst 20h			;2baa	e7		.
	and b			;2bab	a0		.
	bit 4,(iy+014h)		;2bac	fd cb 14 66	. . . f
	call HelpCode2818_end	;2bb0	cd 8c 29	. . )
	rst 20h			;2bb3	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgBase: popup-menu item base led by a carriage
; return byte; the $A0 terminator prints as a
; trailing space.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgBase' (start 0x2bb4 end 0x2bba)
MsgBase_start:
	defb 00dh		;2bb4	0d		.
	defb 062h		;2bb5	62		b
	defb 061h		;2bb6	61		a
	defb 073h		;2bb7	73		s
	defb 065h		;2bb8	65		e
	defb 0a0h		;2bb9	a0		.
MsgBase_end:
	ld de,Rst08Vector+2	;2bba	11 0a 00	. . .
	ld c,(iy+017h)		;2bbd	fd 4e 17	. N .
	ld b,d			;2bc0	42		B
	call l1d2eh		;2bc1	cd 2e 1d	. . .
	rst 20h			;2bc4	e7		.
	adc a,l			;2bc5	8d		.
	ld b,03ch		;2bc6	06 3c		. <
	rst 8			;2bc8	cf		.
	ld bc,(StepContext)	;2bc9	ed 4b aa e0	. K . .
	call sub_1d18h		;2bcd	cd 18 1d	. . .
	rst 20h			;2bd0	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgOption: popup-menu item option (CR-prefixed,
; $A0-terminated).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgOption' (start 0x2bd1 end 0x2bd9)
MsgOption_start:
	defb 00dh		;2bd1	0d		.
	defb 06fh		;2bd2	6f		o
	defb 070h		;2bd3	70		p
	defb 074h		;2bd4	74		t
	defb 069h		;2bd5	69		i
	defb 06fh		;2bd6	6f		o
	defb 06eh		;2bd7	6e		n
	defb 0a0h		;2bd8	a0		.
MsgOption_end:
	ld l,(iy+005h)		;2bd9	fd 6e 05	. n .
	call sub_1957h		;2bdc	cd 57 19	. W .
	rst 20h			;2bdf	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgIntMode: popup-menu item Int mode (CR-prefixed);
; the choice drives the im0/im1/im2 dispatcher at
; $369E.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgIntMode' (start 0x2be0 end 0x2beb)
MsgIntMode_start:
	defb 00dh		;2be0	0d		.
	defb 049h		;2be1	49		I
	defb 06eh		;2be2	6e		n
	defb 074h		;2be3	74		t
	defb 020h		;2be4	20		 
	defb 06dh		;2be5	6d		m
	defb 06fh		;2be6	6f		o
	defb 064h		;2be7	64		d
	defb 065h		;2be8	65		e
	defb 0a0h		;2be9	a0		.
	defb 0afh		;2bea	af		.
MsgIntMode_end:
	ld d,a			;2beb	57		W
	ei			;2bec	fb		.
	halt			;2bed	76		v
	di			;2bee	f3		.
	ld a,031h		;2bef	3e 31		> 1
	dec d			;2bf1	15		.
	jr z,l2bf5h		;2bf2	28 01		( .
	inc a			;2bf4	3c		<
l2bf5h:
	rst 10h			;2bf5	d7		.
	bit 6,(iy+019h)		;2bf6	fd cb 19 76	. . . v
	jr z,l2c17h		;2bfa	28 1b		( .
	rst 20h			;2bfc	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgTapeTurbo: (t+$A9, printing (t) - appended to
; the tape line when turbo mode is selected.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgTapeTurbo' (start 0x2bfd end 0x2c00)
MsgTapeTurbo_start:
	defb 028h		;2bfd	28		(
	defb 074h		;2bfe	74		t
	defb 0a9h		;2bff	a9		.
MsgTapeTurbo_end:
	jr l2c17h		;2c00	18 15		. .
	ld a,d			;2c02	7a		z
	or a			;2c03	b7		.
	jr nz,l2c11h		;2c04	20 0b		  .
	ld a,e			;2c06	7b		{
	ld bc,l0003h+2		;2c07	01 05 00	. . .
	ld hl,l2c1ah		;2c0a	21 1a 2c	! . ,
	cpir			;2c0d	ed b1		. .
	jr z,l2c14h		;2c0f	28 03		( .
l2c11h:
	ld a,00dh		;2c11	3e 0d		> .
	ret			;2c13	c9		.
l2c14h:
	ld (iy+017h),e		;2c14	fd 73 17	. s .
l2c17h:
	ld a,081h		;2c17	3e 81		> .
	ret			;2c19	c9		.
l2c1ah:
	nop			;2c1a	00		.
	ld (bc),a		;2c1b	02		.
	ex af,af'		;2c1c	08		.
	ld a,(bc)		;2c1d	0a		.
	djnz SearchPattern	;2c1e	10 7a		. z
	cp 05bh			;2c20	fe 5b		. [
	ld a,03bh		;2c22	3e 3b		> ;
	ret c			;2c24	d8		.
	ld a,d			;2c25	7a		z
	cp 0c0h			;2c26	fe c0		. .
	ld a,03bh		;2c28	3e 3b		> ;
	ret nc			;2c2a	d0		.
	ld (StepContext),de	;2c2b	ed 53 aa e0	. S . .
	jr l2c17h		;2c2f	18 e6		. .
	ld de,l0318h		;2c31	11 18 03	. . .
	set 0,(iy+013h)		;2c34	fd cb 13 c6	. . . .
	jr l2c41h		;2c38	18 07		. .
	ld de,l0217h		;2c3a	11 17 02	. . .
	res 0,(iy+013h)		;2c3d	fd cb 13 86	. . . .
l2c41h:
	ld a,b			;2c41	78		x
	cp d			;2c42	ba		.
	ld a,00ah		;2c43	3e 0a		> .
	ret c			;2c45	d8		.
	ret z			;2c46	c8		.
	ld a,b			;2c47	78		x
	cp e			;2c48	bb		.
	ld a,01fh		;2c49	3e 1f		> .
	ret nc			;2c4b	d0		.
	ld a,b			;2c4c	78		x
	sub d			;2c4d	92		.
	push af			;2c4e	f5		.
	xor a			;2c4f	af		.
	ld (0dde0h),a		;2c50	32 e0 dd	2 . .
	call sub_0fach		;2c53	cd ac 0f	. . .
	push de			;2c56	d5		.
	call sub_0fach		;2c57	cd ac 0f	. . .
	ex (sp),hl		;2c5a	e3		.
	ex de,hl		;2c5b	eb		.
	call sub_2f0fh		;2c5c	cd 0f 2f	. . /
	ld (0dddch),de		;2c5f	ed 53 dc dd	. S . .
	ld (0dddeh),hl		;2c63	22 de dd	" . .
	ld hl,(PagingState)	;2c66	2a dd df	* . .
	ld (0dde1h),hl		;2c69	22 e1 dd	" . .
	ld (PagingBackup),hl	;2c6c	22 db df	" . .
	pop hl			;2c6f	e1		.
	bit 0,(iy+013h)		;2c70	fd cb 13 46	. . . F
	jr z,l2c87h		;2c74	28 11		( .
	call sub_0fach		;2c76	cd ac 0f	. . .
	ex de,hl		;2c79	eb		.
	call sub_2f85h		;2c7a	cd 85 2f	. . /
	call RestorePagingShadow	;2c7d	cd ba 2d	. . -
	ld hl,(PagingState)	;2c80	2a dd df	* . .
	ld (0dde1h),hl		;2c83	22 e1 dd	" . .
	ex de,hl		;2c86	eb		.
l2c87h:
	ld bc,0dde3h		;2c87	01 e3 dd	. . .
	pop af			;2c8a	f1		.
	ld (0dde0h),a		;2c8b	32 e0 dd	2 . .
l2c8eh:
	push bc			;2c8e	c5		.
	call sub_1069h		;2c8f	cd 69 10	. i .
	pop bc			;2c92	c1		.
	jr c,SearchPattern	;2c93	38 05		8 .
	ld a,e			;2c95	7b		{
	ld (bc),a		;2c96	02		.
	inc bc			;2c97	03		.
	jr l2c8eh		;2c98	18 f4		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SearchPattern: wildcard search across banks through the
; bank window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SearchPattern:
	ld hl,(0dddch)		;2c9a	2a dc dd	* . .
l2c9dh:
	ld bc,(0dddfh)		;2c9d	ed 4b df dd	. K . .
	push hl			;2ca1	e5		.
	ld de,0dde3h		;2ca2	11 e3 dd	. . .
l2ca5h:
	rst 28h			;2ca5	ef		.
	ld c,a			;2ca6	4f		O
	ld a,(de)		;2ca7	1a		.
	cp c			;2ca8	b9		.
	jr nz,l2cd2h		;2ca9	20 27		  '
	inc hl			;2cab	23		#
	inc de			;2cac	13		.
	djnz l2ca5h		;2cad	10 f6		. .
	ld (0dddch),hl		;2caf	22 dc dd	" . .
	pop hl			;2cb2	e1		.
	bit 0,(iy+013h)		;2cb3	fd cb 13 46	. . . F
	jr nz,l2cbch		;2cb7	20 03		  .
	ld (0dd69h),hl		;2cb9	22 69 dd	" i .
l2cbch:
	ld (0dda7h),hl		;2cbc	22 a7 dd	" . .
	ld a,084h		;2cbf	3e 84		> .
l2cc1h:
	push af			;2cc1	f5		.
	call RestorePaging	;2cc2	cd b1 35	. . 5
	ld a,(iy+00ah)		;2cc5	fd 7e 0a	. ~ .
	and 0cfh		;2cc8	e6 cf		. .
	ld (iy+00ah),a		;2cca	fd 77 0a	. w .
	call CommandLoop	;2ccd	cd d9 17	. . .
	pop af			;2cd0	f1		.
	ret			;2cd1	c9		.
l2cd2h:
	pop hl			;2cd2	e1		.
	push hl			;2cd3	e5		.
	or a			;2cd4	b7		.
	ld de,(0dddeh)		;2cd5	ed 5b de dd	. [ . .
	sbc hl,de		;2cd9	ed 52		. R
	pop hl			;2cdb	e1		.
	inc hl			;2cdc	23		#
	jr c,l2c9dh		;2cdd	38 be		8 .
	xor a			;2cdf	af		.
	ld (0dde0h),a		;2ce0	32 e0 dd	2 . .
	ld a,0a0h		;2ce3	3e a0		> .
	jr l2cc1h		;2ce5	18 da		. .
sub_2ce7h:
	ld a,(0dde0h)		;2ce7	3a e0 dd	: . .
	or a			;2cea	b7		.
	ld a,0a1h		;2ceb	3e a1		> .
	ret z			;2ced	c8		.
	ld a,00ch		;2cee	3e 0c		> .
	call sub_0dcah		;2cf0	cd ca 0d	. . .
	call SavePagingShadow	;2cf3	cd ae 2d	. . -
	ld hl,(0dde1h)		;2cf6	2a e1 dd	* . .
	ld (PagingState),hl	;2cf9	22 dd df	" . .
	jr SearchPattern	;2cfc	18 9c		. .
	call sub_2f19h		;2cfe	cd 19 2f	. . /
	inc hl			;2d01	23		#
	or a			;2d02	b7		.
	sbc hl,de		;2d03	ed 52		. R
	push bc			;2d05	c5		.
	push hl			;2d06	e5		.
	pop bc			;2d07	c1		.
	pop hl			;2d08	e1		.
	ex de,hl		;2d09	eb		.
	ld a,c			;2d0a	79		y
	or b			;2d0b	b0		.
	ld a,042h		;2d0c	3e 42		> B
	ret z			;2d0e	c8		.
	push hl			;2d0f	e5		.
	sbc hl,de		;2d10	ed 52		. R
	pop hl			;2d12	e1		.
	jr c,l2d20h		;2d13	38 0b		8 .
l2d15h:
	call sub_2d32h		;2d15	cd 32 2d	. 2 -
	inc hl			;2d18	23		#
	inc de			;2d19	13		.
	ld a,c			;2d1a	79		y
	or b			;2d1b	b0		.
	jr nz,l2d15h		;2d1c	20 f7		  .
	jr l2d2fh		;2d1e	18 0f		. .
l2d20h:
	dec bc			;2d20	0b		.
	add hl,bc		;2d21	09		.
	ex de,hl		;2d22	eb		.
l2d23h:
	add hl,bc		;2d23	09		.
	ex de,hl		;2d24	eb		.
	inc bc			;2d25	03		.
l2d26h:
	call sub_2d32h		;2d26	cd 32 2d	. 2 -
	dec hl			;2d29	2b		+
	dec de			;2d2a	1b		.
	ld a,b			;2d2b	78		x
	or c			;2d2c	b1		.
	jr nz,l2d26h		;2d2d	20 f7		  .
l2d2fh:
	jp l2e47h		;2d2f	c3 47 2e	. G .
sub_2d32h:
	call RestorePaging	;2d32	cd b1 35	. . 5
	rst 28h			;2d35	ef		.
l2d36h:
	call RestorePagingShadow	;2d36	cd ba 2d	. . -
	ex de,hl		;2d39	eb		.
	rst 0			;2d3a	c7		.
	ex de,hl		;2d3b	eb		.
	dec bc			;2d3c	0b		.
	ret			;2d3d	c9		.
	call sub_2f19h		;2d3e	cd 19 2f	. . /
	push bc			;2d41	c5		.
	push hl			;2d42	e5		.
	pop bc			;2d43	c1		.
	pop hl			;2d44	e1		.
	ex de,hl		;2d45	eb		.
l2d46h:
	ex de,hl		;2d46	eb		.
	call RestorePagingShadow	;2d47	cd ba 2d	. . -
	rst 28h			;2d4a	ef		.
	ex de,hl		;2d4b	eb		.
	call RestorePaging	;2d4c	cd b1 35	. . 5
	push bc			;2d4f	c5		.
	ld c,a			;2d50	4f		O
	rst 28h			;2d51	ef		.
	cp c			;2d52	b9		.
	jr nz,l2d67h		;2d53	20 12		  .
l2d55h:
	pop bc			;2d55	c1		.
	push hl			;2d56	e5		.
	or a			;2d57	b7		.
	sbc hl,bc		;2d58	ed 42		. B
	pop hl			;2d5a	e1		.
	inc de			;2d5b	13		.
	inc hl			;2d5c	23		#
	jr nz,l2d46h		;2d5d	20 e7		  .
	call sub_2da1h		;2d5f	cd a1 2d	. . -
	ld a,082h		;2d62	3e 82		> .
	ret nz			;2d64	c0		.
	dec a			;2d65	3d		=
	ret			;2d66	c9		.
l2d67h:
	ld b,a			;2d67	47		G
	push hl			;2d68	e5		.
	push de			;2d69	d5		.
	push bc			;2d6a	c5		.
	call sub_2da1h		;2d6b	cd a1 2d	. . -
	call nz,InitErrorWindow	;2d6e	c4 f8 35	. . 5
	ld b,041h		;2d71	06 41		. A
	rst 8			;2d73	cf		.
	pop bc			;2d74	c1		.
	pop de			;2d75	d1		.
	pop hl			;2d76	e1		.
	push hl			;2d77	e5		.
	push de			;2d78	d5		.
	push bc			;2d79	c5		.
	call PrintMarkerChar	;2d7a	cd c9 1c	. . .
	rst 20h			;2d7d	e7		.
	jr nz,l2d23h		;2d7e	20 a3		  .
	pop af			;2d80	f1		.
	push af			;2d81	f5		.
	call PrintHexByte	;2d82	cd 9a 1d	. . .
	rst 20h			;2d85	e7		.
	jr nz,l2dc2h		;2d86	20 3a		  :
	and b			;2d88	a0		.
	pop bc			;2d89	c1		.
	pop hl			;2d8a	e1		.
	push hl			;2d8b	e5		.
	push bc			;2d8c	c5		.
	call PrintMarkerChar	;2d8d	cd c9 1c	. . .
	rst 20h			;2d90	e7		.
	jr nz,l2d36h		;2d91	20 a3		  .
	pop hl			;2d93	e1		.
	ld a,l			;2d94	7d		}
	call PrintHexByte	;2d95	cd 9a 1d	. . .
	rst 20h			;2d98	e7		.
	adc a,l			;2d99	8d		.
	call sub_2f6ch		;2d9a	cd 6c 2f	. l /
	pop de			;2d9d	d1		.
	pop hl			;2d9e	e1		.
	jr l2d55h		;2d9f	18 b4		. .
sub_2da1h:
	push de			;2da1	d5		.
	push ix			;2da2	dd e5		. .
	ex (sp),hl		;2da4	e3		.
	ld de,PromptWindowDef	;2da5	11 35 e0	. 5 .
	or a			;2da8	b7		.
	sbc hl,de		;2da9	ed 52		. R
	pop hl			;2dab	e1		.
	pop de			;2dac	d1		.
	ret			;2dad	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SavePagingShadow / RestorePagingShadow ($2DBA): stash the
; real paging (DFDD) into the DFD9/DFDB shadows while the
; monitor reprograms banks.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SavePagingShadow:
	push hl			;2dae	e5		.
	ld hl,(PagingState)	;2daf	2a dd df	* . .
	ld (PagingBackup),hl	;2db2	22 db df	" . .
	ld (0dfd9h),hl		;2db5	22 d9 df	" . .
	pop hl			;2db8	e1		.
	ret			;2db9	c9		.
RestorePagingShadow:
	push hl			;2dba	e5		.
	ld hl,(0dfd9h)		;2dbb	2a d9 df	* . .
	ld (PagingState),hl	;2dbe	22 dd df	" . .
	pop hl			;2dc1	e1		.
l2dc2h:
	ret			;2dc2	c9		.
	call sub_2f19h		;2dc3	cd 19 2f	. . /
	ld a,b			;2dc6	78		x
	or a			;2dc7	b7		.
	ld a,00ch		;2dc8	3e 0c		> .
	ret nz			;2dca	c0		.
	call RestorePagingShadow	;2dcb	cd ba 2d	. . -
	ex de,hl		;2dce	eb		.
l2dcfh:
	ld a,c			;2dcf	79		y
	rst 0			;2dd0	c7		.
	push hl			;2dd1	e5		.
	or a			;2dd2	b7		.
	sbc hl,de		;2dd3	ed 52		. R
	pop hl			;2dd5	e1		.
	inc hl			;2dd6	23		#
	jr nz,l2dcfh		;2dd7	20 f6		  .
	jr l2e47h		;2dd9	18 6c		. l
	call sub_0fach		;2ddb	cd ac 0f	. . .
	ld c,e			;2dde	4b		K
	ld b,d			;2ddf	42		B
	jr l2de6h		;2de0	18 04		. .
	ld bc,(UserPc)		;2de2	ed 4b 6b dd	. K k .
l2de6h:
	set 1,(iy+013h)		;2de6	fd cb 13 ce	. . . .
	jr l2df4h		;2dea	18 08		. .
	ld bc,(0dd69h)		;2dec	ed 4b 69 dd	. K i .
	res 1,(iy+013h)		;2df0	fd cb 13 8e	. . . .
l2df4h:
	push bc			;2df4	c5		.
	call sub_1069h		;2df5	cd 69 10	. i .
	pop bc			;2df8	c1		.
	jr c,l2e4ah		;2df9	38 4f		8 O
	push hl			;2dfb	e5		.
	ld l,c			;2dfc	69		i
	ld h,b			;2dfd	60		`
	ld a,e			;2dfe	7b		{
	rst 0			;2dff	c7		.
	inc bc			;2e00	03		.
	bit 1,(iy+013h)		;2e01	fd cb 13 4e	. . . N
	jr nz,l2e0bh		;2e05	20 04		  .
	ld (0dd69h),bc		;2e07	ed 43 69 dd	. C i .
l2e0bh:
	pop hl			;2e0b	e1		.
	jr l2df4h		;2e0c	18 e6		. .
sub_2e0eh:
	ld hl,(UserSp)		;2e0e	2a 6d dd	* m .
	rst 28h			;2e11	ef		.
	ld e,a			;2e12	5f		_
	inc hl			;2e13	23		#
	rst 28h			;2e14	ef		.
	ld d,a			;2e15	57		W
	inc hl			;2e16	23		#
	ld (UserSp),hl		;2e17	22 6d dd	" m .
	ret			;2e1a	c9		.
	call sub_2e0eh		;2e1b	cd 0e 2e	. . .
	call sub_2b5bh		;2e1e	cd 5b 2b	. [ +
	jr l2e3ch		;2e21	18 19		. .
	ld hl,(0dd69h)		;2e23	2a 69 dd	* i .
	call sub_3013h		;2e26	cd 13 30	. . 0
	ex de,hl		;2e29	eb		.
	ld (0dd69h),de		;2e2a	ed 53 69 dd	. S i .
	jr l2e4ah		;2e2e	18 1a		. .
	ld a,001h		;2e30	3e 01		> .
	jr l2e36h		;2e32	18 02		. .
	ld a,002h		;2e34	3e 02		> .
l2e36h:
	xor (iy+00bh)		;2e36	fd ae 0b	. . .
	ld (iy+00bh),a		;2e39	fd 77 0b	. w .
l2e3ch:
	ld a,(iy+00ah)		;2e3c	fd 7e 0a	. ~ .
l2e3fh:
	and 0f1h		;2e3f	e6 f1		. .
	ld (iy+00ah),a		;2e41	fd 77 0a	. w .
l2e44h:
	jp l0e13h		;2e44	c3 13 0e	. . .
l2e47h:
	call RestorePaging	;2e47	cd b1 35	. . 5
l2e4ah:
	ld a,(iy+00ah)		;2e4a	fd 7e 0a	. ~ .
	and 08fh		;2e4d	e6 8f		. .
	jr l2e3fh		;2e4f	18 ee		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CmdInterruptState: the EI/DI command - toggles the saved
; user IFF (DD83 bit 2) that the step trampoline applies as
; its DI/EI prefix.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CmdInterruptState:
	ld a,e			;2e51	7b		{
	and 0feh		;2e52	e6 fe		. .
	or d			;2e54	b2		.
	ld a,017h		;2e55	3e 17		> .
	ret nz			;2e57	c0		.
	ld hl,UserIff		;2e58	21 83 dd	! . .
	ld a,e			;2e5b	7b		{
	or e			;2e5c	b3		.
	jr z,l2e63h		;2e5d	28 04		( .
	set 2,(hl)		;2e5f	cb d6		. .
	jr l2e44h		;2e61	18 e1		. .
l2e63h:
	res 2,(hl)		;2e63	cb 96		. .
	jr l2e44h		;2e65	18 dd		. .
	call sub_2ec6h		;2e67	cd c6 2e	. . .
	set 4,(iy+012h)		;2e6a	fd cb 12 e6	. . . .
	push af			;2e6e	f5		.
	push de			;2e6f	d5		.
	ld de,(UserPc)		;2e70	ed 5b 6b dd	. [ k .
	inc de			;2e74	13		.
	call sub_3577h		;2e75	cd 77 35	. w 5
	ld de,Rst08Vector	;2e78	11 08 00	. . .
	call sub_3577h		;2e7b	cd 77 35	. w 5
	pop de			;2e7e	d1		.
	pop af			;2e7f	f1		.
	jr l2e85h		;2e80	18 03		. .
	call sub_2ec6h		;2e82	cd c6 2e	. . .
l2e85h:
	jr c,l2e8bh		;2e85	38 04		8 .
	ld (UserPc),de		;2e87	ed 53 6b dd	. S k .
l2e8bh:
	jp l011ch		;2e8b	c3 1c 01	. . .
	ld a,e			;2e8e	7b		{
	and 0feh		;2e8f	e6 fe		. .
	or d			;2e91	b2		.
	ld a,019h		;2e92	3e 19		> .
	ret nz			;2e94	c0		.
	ld a,01bh		;2e95	3e 1b		> .
	bit 5,(iy-002h)		;2e97	fd cb fe 6e	. . . n
	ret nz			;2e9b	c0		.
	ld hl,l2e4ah		;2e9c	21 4a 2e	! J .
	ex (sp),hl		;2e9f	e3		.
	res 4,(iy-002h)		;2ea0	fd cb fe a6	. . . .
	bit 0,e			;2ea4	cb 43		. C
	ret z			;2ea6	c8		.
	set 4,(iy-002h)		;2ea7	fd cb fe e6	. . . .
	ret			;2eab	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScreenBankCheck: is the visible screen the bank being
; edited? (shadow-screen test).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScreenBankCheck:
	push hl			;2eac	e5		.
	ld hl,(PagingState)	;2ead	2a dd df	* . .
	ld a,007h		;2eb0	3e 07		> .
	and l			;2eb2	a5		.
	bit 4,h			;2eb3	cb 64		. d
	pop hl			;2eb5	e1		.
	ret z			;2eb6	c8		.
	or 008h			;2eb7	f6 08		. .
	ret			;2eb9	c9		.
sub_2ebah:
	call ScreenBankCheck	;2eba	cd ac 2e	. . .
	cp l			;2ebd	bd		.
	ret			;2ebe	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CalcPagingPorts: bank number in HL -> the #7FFD/#1FFD byte
; pair (bank bits, RAM-at-$C000 bit, TR-DOS session bits),
; respecting the paging mirror.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CalcPagingPorts:
	ld bc,l2e4ah		;2ebf	01 4a 2e	. J .
	push bc			;2ec2	c5		.
	ex de,hl		;2ec3	eb		.
	jr l2ed0h		;2ec4	18 0a		. .
sub_2ec6h:
	cp 001h			;2ec6	fe 01		. .
	ret c			;2ec8	d8		.
	ret z			;2ec9	c8		.
	cp 002h			;2eca	fe 02		. .
l2ecch:
	ld a,00bh		;2ecc	3e 0b		> .
	jr nz,l2f16h		;2ece	20 46		  F
l2ed0h:
	ld a,l			;2ed0	7d		}
	and 0f0h		;2ed1	e6 f0		. .
	or h			;2ed3	b4		.
	ld a,018h		;2ed4	3e 18		> .
	jr nz,l2f16h		;2ed6	20 3e		  >
	ld a,l			;2ed8	7d		}
	and 007h		;2ed9	e6 07		. .
	jr z,sub_2ee5h		;2edb	28 08		( .
	ld a,01ch		;2edd	3e 1c		> .
	bit 5,(iy-002h)		;2edf	fd cb fe 6e	. . . n
	jr nz,l2f16h		;2ee3	20 31		  1
sub_2ee5h:
	ld bc,(PagingState)	;2ee5	ed 4b dd df	. K . .
	res 4,b			;2ee9	cb a0		. .
	bit 3,l			;2eeb	cb 5d		. ]
	jr z,l2ef1h		;2eed	28 02		( .
	set 4,b			;2eef	cb e0		. .
l2ef1h:
	res 3,l			;2ef1	cb 9d		. .
	ld a,c			;2ef3	79		y
	and 0f8h		;2ef4	e6 f8		. .
	or l			;2ef6	b5		.
	ld c,a			;2ef7	4f		O
	ld (PagingState),bc	;2ef8	ed 43 dd df	. C . .
	xor a			;2efc	af		.
	ret			;2efd	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckAddressRange: validate a command address range (carry
; on error, message via ReportError).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckAddressRange:
	cp 002h			;2efe	fe 02		. .
	jr z,sub_2f0fh		;2f00	28 0d		( .
	jr nc,l2ecch		;2f02	30 c8		0 .
	ld hl,0ffffh		;2f04	21 ff ff	! . .
	cp 001h			;2f07	fe 01		. .
	jr z,sub_2f0fh		;2f09	28 04		( .
	ld de,(0dd69h)		;2f0b	ed 5b 69 dd	. [ i .
sub_2f0fh:
	or a			;2f0f	b7		.
	sbc hl,de		;2f10	ed 52		. R
	add hl,de		;2f12	19		.
	ret nc			;2f13	d0		.
	ld a,01dh		;2f14	3e 1d		> .
l2f16h:
	jp ExitToError		;2f16	c3 1e 0e	. . .
sub_2f19h:
	call SavePagingShadow	;2f19	cd ae 2d	. . -
	cp 004h			;2f1c	fe 04		. .
	call z,sub_2f8ah	;2f1e	cc 8a 2f	. . /
	jr z,sub_2f0fh		;2f21	28 ec		( .
	cp 003h			;2f23	fe 03		. .
	jr z,sub_2f0fh		;2f25	28 e8		( .
	ld a,00ah		;2f27	3e 0a		> .
	jr l2f16h		;2f29	18 eb		. .
	cp 001h			;2f2b	fe 01		. .
	jr z,l2f3ah		;2f2d	28 0b		( .
	jr nc,l2ecch		;2f2f	30 9b		0 .
	call CallTrdos		;2f31	cd 84 35	. . 5
	call sub_2fb3h		;2f34	cd b3 2f	. . /
	jp l0e0ah		;2f37	c3 0a 0e	. . .
l2f3ah:
	ld a,e			;2f3a	7b		{
	and 0feh		;2f3b	e6 fe		. .
	or d			;2f3d	b2		.
	jr nz,l2f4dh		;2f3e	20 0d		  .
	ld a,081h		;2f40	3e 81		> .
	ld hl,StepFlags		;2f42	21 f1 df	! . .
	set 1,(hl)		;2f45	cb ce		. .
	bit 0,e			;2f47	cb 43		. C
	ret z			;2f49	c8		.
	res 1,(hl)		;2f4a	cb 8e		. .
	ret			;2f4c	c9		.
l2f4dh:
	ld a,d			;2f4d	7a		z
	or a			;2f4e	b7		.
	ld a,017h		;2f4f	3e 17		> .
	ret nz			;2f51	c0		.
	ld hl,PagingState	;2f52	21 dd df	! . .
	ld a,e			;2f55	7b		{
	cp 005h			;2f56	fe 05		. .
	jr z,l2f61h		;2f58	28 07		( .
	cp 007h			;2f5a	fe 07		. .
	jr z,l2f68h		;2f5c	28 0a		( .
	ld a,017h		;2f5e	3e 17		> .
	ret			;2f60	c9		.
l2f61h:
	res 3,(hl)		;2f61	cb 9e		. .
l2f63h:
	ld a,081h		;2f63	3e 81		> .
	jp l0e13h		;2f65	c3 13 0e	. . .
l2f68h:
	set 3,(hl)		;2f68	cb de		. .
	jr l2f63h		;2f6a	18 f7		. .
sub_2f6ch:
	call sub_2fadh		;2f6c	cd ad 2f	. . /
	ld a,0feh		;2f6f	3e fe		> .
	in a,(0feh)		;2f71	db fe		. .
	rrca			;2f73	0f		.
	ret c			;2f74	d8		.
	ld a,0f7h		;2f75	3e f7		> .
	in a,(0feh)		;2f77	db fe		. .
	rrca			;2f79	0f		.
	ret c			;2f7a	d8		.
	call sub_2fbeh		;2f7b	cd be 2f	. . /
l2f7eh:
	call sub_0839h		;2f7e	cd 39 08	. 9 .
	ld a,09ah		;2f81	3e 9a		> .
	jr l2f16h		;2f83	18 91		. .
sub_2f85h:
	push af			;2f85	f5		.
	push hl			;2f86	e5		.
	push bc			;2f87	c5		.
	jr l2f90h		;2f88	18 06		. .
sub_2f8ah:
	push af			;2f8a	f5		.
	push hl			;2f8b	e5		.
	push bc			;2f8c	c5		.
	ld hl,(0dd9fh)		;2f8d	2a 9f dd	* . .
l2f90h:
	call l2ed0h		;2f90	cd d0 2e	. . .
	ld hl,(PagingState)	;2f93	2a dd df	* . .
	ld (0dfd9h),hl		;2f96	22 d9 df	" . .
	call RestorePaging	;2f99	cd b1 35	. . 5
	pop bc			;2f9c	c1		.
	pop hl			;2f9d	e1		.
	pop af			;2f9e	f1		.
	scf			;2f9f	37		7
	ret			;2fa0	c9		.
sub_2fa1h:
	ld a,0c0h		;2fa1	3e c0		> .
l2fa3h:
	push bc			;2fa3	c5		.
	ld b,000h		;2fa4	06 00		. .
l2fa6h:
	djnz l2fa6h		;2fa6	10 fe		. .
	pop bc			;2fa8	c1		.
	dec a			;2fa9	3d		=
	jr nz,l2fa3h		;2faa	20 f7		  .
	ret			;2fac	c9		.
sub_2fadh:
	ld a,07fh		;2fad	3e 7f		> .
	in a,(0feh)		;2faf	db fe		. .
	rrca			;2fb1	0f		.
	ret c			;2fb2	d8		.
sub_2fb3h:
	call sub_2fa1h		;2fb3	cd a1 2f	. . /
l2fb6h:
	in a,(0feh)		;2fb6	db fe		. .
	cpl			;2fb8	2f		/
	and 01fh		;2fb9	e6 1f		. .
	ret nz			;2fbb	c0		.
	jr l2fb6h		;2fbc	18 f8		. .
sub_2fbeh:
	xor a			;2fbe	af		.
	in a,(0feh)		;2fbf	db fe		. .
	cpl			;2fc1	2f		/
	and 01fh		;2fc2	e6 1f		. .
	ret z			;2fc4	c8		.
	jr sub_2fbeh		;2fc5	18 f7		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DecodeXorTable: XOR-unmask a ROM table into RAM using the
; keys at XorDecodeKeys ($00FC).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DecodeXorTable:
	ex (sp),hl		;2fc7	e3		.
	inc hl			;2fc8	23		#
	inc hl			;2fc9	23		#
	ld b,(hl)		;2fca	46		F
	inc hl			;2fcb	23		#
	push de			;2fcc	d5		.
	ld e,(hl)		;2fcd	5e		^
	inc hl			;2fce	23		#
	ld d,000h		;2fcf	16 00		. .
	ld a,(de)		;2fd1	1a		.
	pop de			;2fd2	d1		.
	ex (sp),hl		;2fd3	e3		.
	ld c,a			;2fd4	4f		O
	ld a,(l00feh)		;2fd5	3a fe 00	: . .
	xor c			;2fd8	a9		.
	ld c,a			;2fd9	4f		O
	ld a,(l00fdh)		;2fda	3a fd 00	: . .
	xor c			;2fdd	a9		.
	ld c,a			;2fde	4f		O
	ld a,(Filler00F1_end)	;2fdf	3a fc 00	: . .
	xor c			;2fe2	a9		.
	ld c,a			;2fe3	4f		O
l2fe4h:
	ld a,c			;2fe4	79		y
	xor b			;2fe5	a8		.
	xor (hl)		;2fe6	ae		.
	ld (de),a		;2fe7	12		.
	inc hl			;2fe8	23		#
	inc de			;2fe9	13		.
	djnz l2fe4h		;2fea	10 f8		. .
	ret			;2fec	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RunCommand: walk the command table, execute the matching
; entry (jp (HL) per entry), wait for a key, return to the
; main loop.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RunCommand:
	scf			;2fed	37		7
	jr l2ff1h		;2fee	18 01		. .
	or a			;2ff0	b7		.
l2ff1h:
	pop hl			;2ff1	e1		.
	ld e,(hl)		;2ff2	5e		^
	inc hl			;2ff3	23		#
	ld d,(hl)		;2ff4	56		V
	inc hl			;2ff5	23		#
	push hl			;2ff6	e5		.
	ex de,hl		;2ff7	eb		.
	jr c,l300fh		;2ff8	38 15		8 .
	ld a,(hl)		;2ffa	7e		~
	or a			;2ffb	b7		.
	jr nz,l3012h		;2ffc	20 14		  .
	ld a,03dh		;2ffe	3e 3d		> =
l3000h:
	sla a			;3000	cb 27		. '
	call sub_0da8h		;3002	cd a8 0d	. . .
	scf			;3005	37		7
	call sub_0dcah		;3006	cd ca 0d	. . .
	call sub_2fb3h		;3009	cd b3 2f	. . /
	jp l0108h		;300c	c3 08 01	. . .
l300fh:
	ld a,(hl)		;300f	7e		~
	or a			;3010	b7		.
	ret z			;3011	c8		.
l3012h:
	jp (hl)			;3012	e9		.
sub_3013h:
	push hl			;3013	e5		.
	call FetchPrefixBytes	;3014	cd a8 1a	. . .
	pop hl			;3017	e1		.
	bit 6,(iy+001h)		;3018	fd cb 01 76	. . . v
	ret z			;301c	c8		.
	ld hl,(StepPc)		;301d	2a d0 dd	* . .
	ret			;3020	c9		.
	xor a			;3021	af		.
	call AddBreakpoint	;3022	cd 53 30	. S 0
	ex de,hl		;3025	eb		.
	call sub_3013h		;3026	cd 13 30	. . 0
	jr InlineBlob3044_end	;3029	18 25		. %
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BreakpointSlot: address of breakpoint slot A (DD8D-based
; table).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BreakpointSlot:
	ld l,a			;302b	6f		o
	ld h,000h		;302c	26 00		& .
	ld de,0dd8dh		;302e	11 8d dd	. . .
	add hl,hl		;3031	29		)
	add hl,de		;3032	19		.
	ret			;3033	c9		.
	ld a,0c3h		;3034	3e c3		> .
	call sub_156eh		;3036	cd 6e 15	. n .
	jr c,l305eh		;3039	38 23		8 #
	bit 4,(iy+005h)		;303b	fd cb 05 66	. . . f
	push af			;303f	f5		.
	call z,sub_1069h	;3040	cc 69 10	. i .
	pop af			;3043	f1		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; InlineBlob3044: twelve bytes eaten as data by the
; breakpoint path - the conditional call at $3040
; (sub_1069h scans via sub_1056h and advances HL)
; walks across them, and the jr at $3029 bypasses
; the region entirely.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'InlineBlob3044' (start 0x3044 end 0x3050)
InlineBlob3044_start:
	defb 0c4h		;3044	c4		.
	defb 008h		;3045	08		.
	defb 011h		;3046	11		.
	defb 020h		;3047	20		 
	defb 0edh		;3048	ed		.
	defb 0d5h		;3049	d5		.
	defb 03eh		;304a	3e		>
	defb 00ch		;304b	0c		.
	defb 0cdh		;304c	cd		.
	defb 0cah		;304d	ca		.
	defb 00dh		;304e	0d		.
	defb 0e1h		;304f	e1		.
InlineBlob3044_end:
	jp l30dah		;3050	c3 da 30	. . 0
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AddBreakpoint: program a slot - address plus the original
; bytes saved for clean removal.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AddBreakpoint:
	call BreakpointSlot	;3053	cd 2b 30	. + 0
	ld de,(0dd8bh)		;3056	ed 5b 8b dd	. [ . .
	ld (hl),e		;305a	73		s
	inc hl			;305b	23		#
	ld (hl),d		;305c	72		r
	ret			;305d	c9		.
l305eh:
	res 1,(ix+007h)		;305e	dd cb 07 8e	. . . .
	jp l2f7eh		;3062	c3 7e 2f	. ~ /
sub_3065h:
	xor a			;3065	af		.
	ld hl,(UserPc)		;3066	2a 6b dd	* k .
	bit 6,(iy+005h)		;3069	fd cb 05 76	. . . v
	jr nz,l3072h		;306d	20 03		  .
	ld hl,(0dd69h)		;306f	2a 69 dd	* i .
l3072h:
	or a			;3072	b7		.
	jr z,l307bh		;3073	28 06		( .
	cp 002h			;3075	fe 02		. .
	ld a,00bh		;3077	3e 0b		> .
	ret nc			;3079	d0		.
	ex de,hl		;307a	eb		.
l307bh:
	ld (0dd8bh),hl		;307b	22 8b dd	" . .
	ld a,00ch		;307e	3e 0c		> .
	call sub_0dcah		;3080	cd ca 0d	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ListBreakpoints: the breakpoint list window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ListBreakpoints:
	call InitErrorWindow	;3083	cd f8 35	. . 5
	ld (ix+000h),000h	;3086	dd 36 00 00	. 6 . .
l308ah:
	set 1,(ix+007h)		;308a	dd cb 07 ce	. . . .
	ld hl,(0dd8bh)		;308e	2a 8b dd	* . .
	ld b,016h		;3091	06 16		. .
	jr l3098h		;3093	18 03		. .
l3095h:
	inc (ix+000h)		;3095	dd 34 00	. 4 .
l3098h:
	push bc			;3098	c5		.
	call ArmWatchpoint	;3099	cd 33 31	. 3 1
	pop bc			;309c	c1		.
	djnz l3095h		;309d	10 f6		. .
l309fh:
	ld hl,l309fh		;309f	21 9f 30	! . 0
	push hl			;30a2	e5		.
	call AutoRepeatKey	;30a3	cd 0d 1b	. . .
	call ScanTokens		;30a6	cd ac 17	. . .
	call l305eh		;30a9	cd 5e 30	. ^ 0
	rlca			;30ac	07		.
	ld e,(hl)		;30ad	5e		^
	jr nc,$+10		;30ae	30 08		0 .
	call c,sub_0930h	;30b0	dc 30 09	. 0 .
	pop hl			;30b3	e1		.
	jr nc,$+12		;30b4	30 0a		0 .
	ld a,(de)		;30b6	1a		.
	ld sp,0ee0bh		;30b7	31 0b ee	1 . .
	jr nc,$+15		;30ba	30 0d		0 .
	inc (hl)		;30bc	34		4
	jr nc,l308ah		;30bd	30 cb		0 .
	ld hl,06230h		;30bf	21 30 62	! 0 b
	call p,06e2ah		;30c2	f4 2a 6e	. * n
	rrca			;30c5	0f		.
	dec hl			;30c6	2b		+
	rst 38h			;30c7	ff		.
	sub 030h		;30c8	d6 30		. 0
	ret c			;30ca	d8		.
	ld c,005h		;30cb	0e 05		. .
	cp c			;30cd	b9		.
	jr c,AddBreakpoint	;30ce	38 83		8 .
	sub c			;30d0	91		.
	cp c			;30d1	b9		.
	ret nc			;30d2	d0		.
	call BreakpointSlot	;30d3	cd 2b 30	. + 0
	ld e,(hl)		;30d6	5e		^
	inc hl			;30d7	23		#
	ld d,(hl)		;30d8	56		V
	ex de,hl		;30d9	eb		.
l30dah:
	jr l30e8h		;30da	18 0c		. .
	ld bc,0ffffh		;30dc	01 ff ff	. . .
	jr l30e4h		;30df	18 03		. .
	ld bc,l0001h		;30e1	01 01 00	. . .
l30e4h:
	ld hl,(0dd8bh)		;30e4	2a 8b dd	* . .
	add hl,bc		;30e7	09		.
l30e8h:
	ld (0dd8bh),hl		;30e8	22 8b dd	" . .
l30ebh:
	pop hl			;30eb	e1		.
	jr ListBreakpoints	;30ec	18 95		. .
	call ScrollDown		;30ee	cd ee 0b	. . .
	ld (ix+000h),000h	;30f1	dd 36 00 00	. 6 . .
	ld bc,0fffbh		;30f5	01 fb ff	. . .
l30f8h:
	inc bc			;30f8	03		.
	ld hl,(0dd8bh)		;30f9	2a 8b dd	* . .
	add hl,bc		;30fc	09		.
	push bc			;30fd	c5		.
	call FetchPrefixBytes	;30fe	cd a8 1a	. . .
	pop bc			;3101	c1		.
	ld de,(0dd8bh)		;3102	ed 5b 8b dd	. [ . .
	or a			;3106	b7		.
	sbc hl,de		;3107	ed 52		. R
	jr z,l310dh		;3109	28 02		( .
	jr nc,l3116h		;310b	30 09		0 .
l310dh:
	add hl,de		;310d	19		.
	jr nz,l30f8h		;310e	20 e8		  .
	add hl,bc		;3110	09		.
	ld (0dd8bh),hl		;3111	22 8b dd	" . .
	jr ArmWatchpoint	;3114	18 1d		. .
l3116h:
	add hl,de		;3116	19		.
	add hl,bc		;3117	09		.
	jr l30e8h		;3118	18 ce		. .
	call ScrollUp		;311a	cd 61 0b	. a .
	ld (ix+000h),015h	;311d	dd 36 00 15	. 6 . .
	ld hl,(0dd8bh)		;3121	2a 8b dd	* . .
	call FetchPrefixBytes	;3124	cd a8 1a	. . .
	ld (0dd8bh),hl		;3127	22 8b dd	" . .
	ld b,015h		;312a	06 15		. .
l312ch:
	push bc			;312c	c5		.
	call FetchPrefixBytes	;312d	cd a8 1a	. . .
	pop bc			;3130	c1		.
	djnz l312ch		;3131	10 f9		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ArmWatchpoint: activate a watch entry (address / length /
; value).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ArmWatchpoint:
	ld a,(ix+006h)		;3133	dd 7e 06	. ~ .
	push af			;3136	f5		.
	ld (ix+001h),000h	;3137	dd 36 01 00	. 6 . .
	push ix			;313b	dd e5		. .
	call InitWatchTable	;313d	cd f7 29	. . )
l3140h:
	bit 7,(ix+000h)		;3140	dd cb 00 7e	. . . ~
	jr z,l3168h		;3144	28 22		( "
	push hl			;3146	e5		.
	ld l,(ix+001h)		;3147	dd 6e 01	. n .
	call sub_2ebah		;314a	cd ba 2e	. . .
	pop hl			;314d	e1		.
	jr nz,l3168h		;314e	20 18		  .
	push de			;3150	d5		.
	ld e,(ix+002h)		;3151	dd 5e 02	. ^ .
	ld d,(ix+003h)		;3154	dd 56 03	. V .
	or a			;3157	b7		.
	sbc hl,de		;3158	ed 52		. R
	add hl,de		;315a	19		.
	pop de			;315b	d1		.
	jr nz,l3168h		;315c	20 0a		  .
	ld a,(0e0a9h)		;315e	3a a9 e0	: . .
	ex (sp),ix		;3161	dd e3		. .
	ld (ix+006h),a		;3163	dd 77 06	. w .
	ex (sp),ix		;3166	dd e3		. .
l3168h:
	add ix,de		;3168	dd 19		. .
	djnz l3140h		;316a	10 d4		. .
	pop ix			;316c	dd e1		. .
	call sub_1abeh		;316e	cd be 1a	. . .
	pop af			;3171	f1		.
	ld (ix+006h),a		;3172	dd 77 06	. w .
	ret			;3175	c9		.
	set 4,(iy+009h)		;3176	fd cb 09 e6	. . . .
	or a			;317a	b7		.
	jr z,l318fh		;317b	28 12		( .
	cp 002h			;317d	fe 02		. .
	ld a,00bh		;317f	3e 0b		> .
	jr nc,l3192h		;3181	30 0f		0 .
	ld a,e			;3183	7b		{
	and 0feh		;3184	e6 fe		. .
	or d			;3186	b2		.
	ld a,03eh		;3187	3e 3e		> >
	jr nz,l3192h		;3189	20 07		  .
	ld a,e			;318b	7b		{
	ld (0ffcah),a		;318c	32 ca ff	2 . .
l318fh:
	call l319ah		;318f	cd 9a 31	. . 1
l3192h:
	call sub_0839h		;3192	cd 39 08	. 9 .
	ld a,081h		;3195	3e 81		> .
	jp l0df8h		;3197	c3 f8 0d	. . .
l319ah:
	call InitErrorWindow	;319a	cd f8 35	. . 5
	ld (iy+008h),020h	;319d	fd 36 08 20	. 6 .  
	ld hl,(0ffd0h)		;31a1	2a d0 ff	* . .
	push hl			;31a4	e5		.
	rst 18h			;31a5	df		.
	call z,0da38h		;31a6	cc 38 da	. 8 .
	and b			;31a9	a0		.
	ld (006c5h),a		;31aa	32 c5 06	2 . .
	inc c			;31ad	0c		.
	rst 8			;31ae	cf		.
	rst 20h			;31af	e7		.
	and d			;31b0	a2		.
	ld hl,0e4ech		;31b1	21 ec e4	! . .
	ld b,008h		;31b4	06 08		. .
	call l1c7ch		;31b6	cd 7c 1c	. | .
	rst 20h			;31b9	e7		.
	ld (0f18dh),hl		;31ba	22 8d f1	" . .
	push af			;31bd	f5		.
	bit 5,a			;31be	cb 6f		. o
	ld b,039h		;31c0	06 39		. 9
	jr z,l31c5h		;31c2	28 01		( .
	inc b			;31c4	04		.
l31c5h:
	rst 8			;31c5	cf		.
	ld b,038h		;31c6	06 38		. 8
	rst 8			;31c8	cf		.
	pop af			;31c9	f1		.
	rlca			;31ca	07		.
	ld b,03ch		;31cb	06 3c		. <
	jr nc,l31d0h		;31cd	30 01		0 .
	inc b			;31cf	04		.
l31d0h:
	rst 8			;31d0	cf		.
	ld b,03bh		;31d1	06 3b		. ;
	rst 8			;31d3	cf		.
	rst 20h			;31d4	e7		.
	xor b			;31d5	a8		.
	ld a,(0e4deh)		;31d6	3a de e4	: . .
	call sub_32afh		;31d9	cd af 32	. . 2
	rst 20h			;31dc	e7		.
	xor a			;31dd	af		.
	ld a,(0e4dah)		;31de	3a da e4	: . .
	call sub_32afh		;31e1	cd af 32	. . 2
	rst 20h			;31e4	e7		.
	add hl,hl		;31e5	29		)
	dec c			;31e6	0d		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg31E7: disk-catalog message File.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg31E7' (start 0x31e7 end 0x31eb)
Msg31E7_start:
	defb 046h		;31e7	46		F
	defb 069h		;31e8	69		i
	defb 06ch		;31e9	6c		l
	defb 0e5h		;31ea	e5		.
Msg31E7_end:
	ld a,(0e4dbh)		;31eb	3a db e4	: . .
	call sub_32b8h		;31ee	cd b8 32	. . 2
	rst 20h			;31f1	e7		.
	xor a			;31f2	af		.
	ld a,(0e4ebh)		;31f3	3a eb e4	: . .
	call sub_32b8h		;31f6	cd b8 32	. . 2
	rst 20h			;31f9	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg31FA: disk-catalog message , free.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg31FA' (start 0x31fa end 0x3200)
Msg31FA_start:
	defb 02ch		;31fa	2c		,
	defb 020h		;31fb	20		 
	defb 066h		;31fc	66		f
	defb 072h		;31fd	72		r
	defb 065h		;31fe	65		e
	defb 0e5h		;31ff	e5		.
Msg31FA_end:
	ld bc,(0e4dch)		;3200	ed 4b dc e4	. K . .
	call sub_1d15h		;3204	cd 15 1d	. . .
	rst 20h			;3207	e7		.
	jr nz,$-86		;3208	20 a8		  .
	ld a,(0e4d8h)		;320a	3a d8 e4	: . .
	call sub_32afh		;320d	cd af 32	. . 2
	rst 20h			;3210	e7		.
	xor a			;3211	af		.
	ld a,(0e4d9h)		;3212	3a d9 e4	: . .
	call sub_32afh		;3215	cd af 32	. . 2
	rst 20h			;3218	e7		.
	add hl,hl		;3219	29		)
	dec c			;321a	0d		.
	adc a,l			;321b	8d		.
l321ch:
	ld b,010h		;321c	06 10		. .
l321eh:
	push bc			;321e	c5		.
	rst 18h			;321f	df		.
	add hl,de		;3220	19		.
	add hl,sp		;3221	39		9
	pop bc			;3222	c1		.
	jp c,l32a0h		;3223	da a0 32	. . 2
	ld a,(hl)		;3226	7e		~
	or a			;3227	b7		.
	jr z,l3299h		;3228	28 6f		( o
	push bc			;322a	c5		.
	dec a			;322b	3d		=
	jr nz,l323eh		;322c	20 10		  .
	inc hl			;322e	23		#
	bit 4,(iy+009h)		;322f	fd cb 09 66	. . . f
	ld a,082h		;3233	3e 82		> .
	jr z,l3239h		;3235	28 02		( .
	ld a,03fh		;3237	3e 3f		> ?
l3239h:
	rst 10h			;3239	d7		.
	ld b,007h		;323a	06 07		. .
	jr l3240h		;323c	18 02		. .
l323eh:
	ld b,008h		;323e	06 08		. .
l3240h:
	call l1c7ch		;3240	cd 7c 1c	. | .
	rst 20h			;3243	e7		.
	jr nz,$-66		;3244	20 bc		  .
	ld a,(hl)		;3246	7e		~
	inc hl			;3247	23		#
	rst 10h			;3248	d7		.
	rst 20h			;3249	e7		.
	cp (hl)			;324a	be		.
	ld b,002h		;324b	06 02		. .
l324dh:
	push bc			;324d	c5		.
	ld c,(hl)		;324e	4e		N
	inc hl			;324f	23		#
	ld b,(hl)		;3250	46		F
	inc hl			;3251	23		#
	push hl			;3252	e5		.
	call sub_1d18h		;3253	cd 18 1d	. . .
	pop hl			;3256	e1		.
	pop bc			;3257	c1		.
	djnz l324dh		;3258	10 f3		. .
	rst 20h			;325a	e7		.
	and b			;325b	a0		.
	ld a,(hl)		;325c	7e		~
	inc hl			;325d	23		#
	push hl			;325e	e5		.
	call sub_32b8h		;325f	cd b8 32	. . 2
	pop hl			;3262	e1		.
	ld a,01eh		;3263	3e 1e		> .
	call sub_1b03h		;3265	cd 03 1b	. . .
	ld b,002h		;3268	06 02		. .
l326ah:
	push bc			;326a	c5		.
	rst 20h			;326b	e7		.
	and b			;326c	a0		.
	ld a,(hl)		;326d	7e		~
	inc hl			;326e	23		#
	push hl			;326f	e5		.
	call sub_32afh		;3270	cd af 32	. . 2
	pop hl			;3273	e1		.
	pop bc			;3274	c1		.
	djnz l326ah		;3275	10 f3		. .
	rst 20h			;3277	e7		.
	adc a,l			;3278	8d		.
	pop bc			;3279	c1		.
	djnz l321eh		;327a	10 a2		. .
	call sub_2da1h		;327c	cd a1 2d	. . -
l327fh:
	jr nz,l321ch		;327f	20 9b		  .
	rst 20h			;3281	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3282: message More... (listing pager).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg3282' (start 0x3282 end 0x3289)
Msg3282_start:
	defb 04dh		;3282	4d		M
	defb 06fh		;3283	6f		o
	defb 072h		;3284	72		r
	defb 065h		;3285	65		e
	defb 02eh		;3286	2e		.
	defb 02eh		;3287	2e		.
	defb 0aeh		;3288	ae		.
Msg3282_end:
	ld (ix+001h),000h	;3289	dd 36 01 00	. 6 . .
	call AutoRepeatKey	;328d	cd 0d 1b	. . .
	cp 007h			;3290	fe 07		. .
	jr nz,l327fh		;3292	20 eb		  .
	ld a,00ah		;3294	3e 0a		> .
	call sub_1b03h		;3296	cd 03 1b	. . .
l3299h:
	rst 20h			;3299	e7		.
	dec c			;329a	0d		.
	ld b,l			;329b	45		E
	ld c,(hl)		;329c	4e		N
	call nz,l0518h		;329d	c4 18 05	. . .
l32a0h:
	ld b,a			;32a0	47		G
	call sub_34a0h		;32a1	cd a0 34	. . 4
	rst 8			;32a4	cf		.
	call sub_0839h		;32a5	cd 39 08	. 9 .
	pop hl			;32a8	e1		.
	ld (0ffd0h),hl		;32a9	22 d0 ff	" . .
	jp sub_2fb3h		;32ac	c3 b3 2f	. . /
sub_32afh:
	ld b,000h		;32af	06 00		. .
	ld c,a			;32b1	4f		O
	ld de,l1023h		;32b2	11 23 10	. # .
	jp sub_1ce3h		;32b5	c3 e3 1c	. . .
sub_32b8h:
	ld c,a			;32b8	4f		O
	ld b,000h		;32b9	06 00		. .
	jp sub_1d15h		;32bb	c3 15 1d	. . .
	ld d,000h		;32be	16 00		. .
	ld a,(l00fbh)		;32c0	3a fb 00	: . .
	sub (iy-007h)		;32c3	fd 96 f9	. . .
	jr z,l32cah		;32c6	28 02		( .
	res 0,e			;32c8	cb 83		. .
l32cah:
	ld hl,0e3f7h		;32ca	21 f7 e3	! . .
	ld bc,l0105h		;32cd	01 05 01	. . .
	jp sub_02d6h		;32d0	c3 d6 02	. . .
l32d3h:
	ld hl,l15b5h		;32d3	21 b5 15	! . .
	ex (sp),hl		;32d6	e3		.
	call sub_3515h		;32d7	cd 15 35	. . 5
	ret nz			;32da	c0		.
	ld (ix+000h),015h	;32db	dd 36 00 15	. 6 . .
	ld bc,Rst10Vector	;32df	01 10 00	. . .
	call sub_02d6h		;32e2	cd d6 02	. . .
	ld d,027h		;32e5	16 27		. '
	bit 5,a			;32e7	cb 6f		. o
	jr z,l32edh		;32e9	28 02		( .
	ld d,04fh		;32eb	16 4f		. O
l32edh:
	ld bc,Rst10Vector	;32ed	01 10 00	. . .
	call sub_02d6h		;32f0	cd d6 02	. . .
	ld e,001h		;32f3	1e 01		. .
	rlca			;32f5	07		.
	jr c,l32fah		;32f6	38 02		8 .
l32f8h:
	ld e,000h		;32f8	1e 00		. .
l32fah:
	push de			;32fa	d5		.
	ld (ix+001h),000h	;32fb	dd 36 01 00	. 6 . .
	call FillRowToEnd	;32ff	cd 58 0c	. X .
	pop bc			;3302	c1		.
	push bc			;3303	c5		.
	ld (iy+007h),001h	;3304	fd 36 07 01	. 6 . .
	ld c,b			;3308	48		H
	ld b,000h		;3309	06 00		. .
	call sub_1d25h		;330b	cd 25 1d	. % .
	rst 20h			;330e	e7		.
	xor a			;330f	af		.
	pop bc			;3310	c1		.
	push bc			;3311	c5		.
	ld b,000h		;3312	06 00		. .
	call sub_1d25h		;3314	cd 25 1d	. % .
	call LookupToken	;3317	cd d5 34	. . 4
	ld b,a			;331a	47		G
	ld (ix+001h),005h	;331b	dd 36 01 05	. 6 . .
	ld (ix+00ah),008h	;331f	dd 36 0a 08	. 6 . .
l3323h:
	ld a,082h		;3323	3e 82		> .
	rst 10h			;3325	d7		.
	djnz l3323h		;3326	10 fb		. .
	ld (ix+00ah),006h	;3328	dd 36 0a 06	. 6 . .
	rst 20h			;332c	e7		.
	adc a,l			;332d	8d		.
	bit 7,(iy+013h)		;332e	fd cb 13 7e	. . . ~
	jr z,l3342h		;3332	28 0e		( .
	rst 20h			;3334	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3335: message Formating. (disk format).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg3335' (start 0x3335 end 0x333f)
Msg3335_start:
	defb 046h		;3335	46		F
	defb 06fh		;3336	6f		o
	defb 072h		;3337	72		r
	defb 06dh		;3338	6d		m
	defb 061h		;3339	61		a
	defb 074h		;333a	74		t
	defb 069h		;333b	69		i
	defb 06eh		;333c	6e		n
	defb 067h		;333d	67		g
	defb 0aeh		;333e	ae		.
Msg3335_end:
	call PrintFrameCount	;333f	cd ab 34	. . 4
l3342h:
	pop de			;3342	d1		.
	push de			;3343	d5		.
	ld a,e			;3344	7b		{
	ld c,00eh		;3345	0e 0e		. .
	call sub_02d6h		;3347	cd d6 02	. . .
	jr c,l33ach		;334a	38 60		8 `
	call LookupToken	;334c	cd d5 34	. . 4
	pop de			;334f	d1		.
	ld c,000h		;3350	0e 00		. .
	ld hl,0e3f7h		;3352	21 f7 e3	! . .
	push hl			;3355	e5		.
l3356h:
	ld (hl),000h		;3356	36 00		6 .
	inc hl			;3358	23		#
	ld (hl),d		;3359	72		r
	inc hl			;335a	23		#
	ld (hl),e		;335b	73		s
	inc hl			;335c	23		#
	inc c			;335d	0c		.
	ld (hl),000h		;335e	36 00		6 .
	inc hl			;3360	23		#
	ld (hl),b		;3361	70		p
	inc hl			;3362	23		#
	cp c			;3363	b9		.
	jr nz,l3356h		;3364	20 f0		  .
	ld (hl),0feh		;3366	36 fe		6 .
	ex de,hl		;3368	eb		.
	push hl			;3369	e5		.
	ld c,001h		;336a	0e 01		. .
l336ch:
	ld hl,0e3fah		;336c	21 fa e3	! . .
	ld b,000h		;336f	06 00		. .
l3371h:
	ld a,(hl)		;3371	7e		~
	or a			;3372	b7		.
	jr nz,l337ah		;3373	20 05		  .
	ld (hl),c		;3375	71		q
	inc c			;3376	0c		.
	inc b			;3377	04		.
	jr l3381h		;3378	18 07		. .
l337ah:
	call sub_3545h		;337a	cd 45 35	. E 5
	jr nc,l338eh		;337d	30 0f		0 .
	jr l3371h		;337f	18 f0		. .
l3381h:
	ld a,(iy+018h)		;3381	fd 7e 18	. ~ .
l3384h:
	call sub_3545h		;3384	cd 45 35	. E 5
	jr nc,l338eh		;3387	30 05		0 .
	dec a			;3389	3d		=
	jr nz,l3384h		;338a	20 f8		  .
	jr l3371h		;338c	18 e3		. .
l338eh:
	ld a,b			;338e	78		x
	or a			;338f	b7		.
	jr nz,l336ch		;3390	20 da		  .
	pop de			;3392	d1		.
	pop hl			;3393	e1		.
	push de			;3394	d5		.
	push hl			;3395	e5		.
	ld bc,Rst10Vector	;3396	01 10 00	. . .
	call sub_02d6h		;3399	cd d6 02	. . .
	pop hl			;339c	e1		.
	ld b,a			;339d	47		G
	ld de,05a85h		;339e	11 85 5a	. . Z
	push de			;33a1	d5		.
	ld c,00bh		;33a2	0e 0b		. .
	or a			;33a4	b7		.
	bit 7,(iy+013h)		;33a5	fd cb 13 7e	. . . ~
	call nz,sub_02d6h	;33a9	c4 d6 02	. . .
l33ach:
	jp c,l3494h		;33ac	da 94 34	. . 4
	ld (ix+001h),000h	;33af	dd 36 01 00	. 6 . .
	rst 20h			;33b3	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg33B4: message Checking.  (disk verify).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg33B4' (start 0x33b4 end 0x33be)
Msg33B4_start:
	defb 043h		;33b4	43		C
	defb 068h		;33b5	68		h
	defb 065h		;33b6	65		e
	defb 063h		;33b7	63		c
	defb 06bh		;33b8	6b		k
	defb 069h		;33b9	69		i
	defb 06eh		;33ba	6e		n
	defb 067h		;33bb	67		g
	defb 02eh		;33bc	2e		.
	defb 0a0h		;33bd	a0		.
Msg33B4_end:
	call PrintFrameCount	;33be	cd ab 34	. . 4
	ld de,WriteAnyBankByte	;33c1	11 00 00	. . .
	ld hl,0e3f7h		;33c4	21 f7 e3	! . .
	push hl			;33c7	e5		.
	ld c,00ch		;33c8	0e 0c		. .
	call sub_02d6h		;33ca	cd d6 02	. . .
	call c,sub_348eh	;33cd	dc 8e 34	. . 4
	call LookupToken	;33d0	cd d5 34	. . 4
	pop hl			;33d3	e1		.
	exx			;33d4	d9		.
	pop hl			;33d5	e1		.
	exx			;33d6	d9		.
	ld de,(0dfd2h)		;33d7	ed 5b d2 df	. [ . .
	ld bc,(0dfd4h)		;33db	ed 4b d4 df	. K . .
l33dfh:
	ld a,(hl)		;33df	7e		~
	cp 0feh			;33e0	fe fe		. .
	jr z,l33f8h		;33e2	28 14		( .
	inc de			;33e4	13		.
	or a			;33e5	b7		.
	ld a,034h		;33e6	3e 34		> 4
	jr z,l33edh		;33e8	28 03		( .
	ld a,032h		;33ea	3e 32		> 2
	inc bc			;33ec	03		.
l33edh:
	exx			;33ed	d9		.
	ld (hl),a		;33ee	77		w
	inc hl			;33ef	23		#
	exx			;33f0	d9		.
	inc hl			;33f1	23		#
	inc hl			;33f2	23		#
	inc hl			;33f3	23		#
	inc hl			;33f4	23		#
	inc hl			;33f5	23		#
	jr l33dfh		;33f6	18 e7		. .
l33f8h:
	ld (0dfd2h),de		;33f8	ed 53 d2 df	. S . .
	ld (0dfd4h),bc		;33fc	ed 43 d4 df	. C . .
	ld (ix+001h),00ah	;3400	dd 36 01 0a	. 6 . .
	call sub_34b3h		;3404	cd b3 34	. . 4
	pop de			;3407	d1		.
	ld a,e			;3408	7b		{
	or a			;3409	b7		.
	jp nz,l32f8h		;340a	c2 f8 32	. . 2
	dec d			;340d	15		.
	ld a,d			;340e	7a		z
	cp 0ffh			;340f	fe ff		. .
	jp nz,l32edh		;3411	c2 ed 32	. . 2
	ld bc,Rst10Vector	;3414	01 10 00	. . .
	call sub_02d6h		;3417	cd d6 02	. . .
	ld e,a			;341a	5f		_
	and 043h		;341b	e6 43		. C
	cp 001h			;341d	fe 01		. .
	jr nz,l347ah		;341f	20 59		  Y
	bit 7,(iy+013h)		;3421	fd cb 13 7e	. . . ~
	jr z,l347ah		;3425	28 53		( S
	ld hl,0e3f7h		;3427	21 f7 e3	! . .
	xor a			;342a	af		.
	ld b,a			;342b	47		G
l342ch:
	ld (hl),a		;342c	77		w
	inc hl			;342d	23		#
	djnz l342ch		;342e	10 fc		. .
	ld a,016h		;3430	3e 16		> .
	bit 7,e			;3432	cb 7b		. {
	jr nz,l3438h		;3434	20 02		  .
	ld a,018h		;3436	3e 18		> .
l3438h:
	bit 5,e			;3438	cb 6b		. k
	jr nz,l343eh		;343a	20 02		  .
	or 001h			;343c	f6 01		. .
l343eh:
	ld (0e4dah),a		;343e	32 da e4	2 . .
	ld hl,(0dfd2h)		;3441	2a d2 df	* . .
	ld de,0fff0h		;3444	11 f0 ff	. . .
	add hl,de		;3447	19		.
	ld (0e4d6h),hl		;3448	22 d6 e4	" . .
	ld (0e4dch),hl		;344b	22 dc e4	" . .
	ld hl,(0dfd4h)		;344e	2a d4 df	* . .
	ld (0e4d4h),hl		;3451	22 d4 e4	" . .
	ld a,010h		;3454	3e 10		> .
	ld (0e4deh),a		;3456	32 de e4	2 . .
	ld a,(l00fbh)		;3459	3a fb 00	: . .
	cp (iy-007h)		;345c	fd be f9	. . .
	jr nz,l3466h		;345f	20 05		  .
	ld a,001h		;3461	3e 01		> .
	ld (0e4d9h),a		;3463	32 d9 e4	2 . .
l3466h:
	ld hl,0e4ech		;3466	21 ec e4	! . .
	call sub_34efh		;3469	cd ef 34	. . 4
	ld c,007h		;346c	0e 07		. .
	call sub_02d6h		;346e	cd d6 02	. . .
	jr c,l3494h		;3471	38 21		8 !
	ld e,008h		;3473	1e 08		. .
	call sub_354fh		;3475	cd 4f 35	. O 5
	jr c,l3494h		;3478	38 1a		8 .
l347ah:
	ld hl,0e097h		;347a	21 97 e0	! . .
	rst 30h			;347d	f7		.
	call sub_0839h		;347e	cd 39 08	. 9 .
	rst 20h			;3481	e7		.
	dec c			;3482	0d		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3483: message Complete.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg3483' (start 0x3483 end 0x348b)
Msg3483_start:
	defb 043h		;3483	43		C
	defb 06fh		;3484	6f		o
	defb 06dh		;3485	6d		m
	defb 070h		;3486	70		p
	defb 06ch		;3487	6c		l
	defb 065h		;3488	65		e
	defb 074h		;3489	74		t
	defb 0e5h		;348a	e5		.
Msg3483_end:
	jp sub_2fb3h		;348b	c3 b3 2f	. . /
sub_348eh:
	ld a,(0ffddh)		;348e	3a dd ff	: . .
	cp 014h			;3491	fe 14		. .
	ret z			;3493	c8		.
l3494h:
	ld b,046h		;3494	06 46		. F
	call sub_34a0h		;3496	cd a0 34	. . 4
	rst 8			;3499	cf		.
	call sub_2fb3h		;349a	cd b3 2f	. . /
	jp l15b5h		;349d	c3 b5 15	. . .
sub_34a0h:
	rst 20h			;34a0	e7		.
	adc a,l			;34a1	8d		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ReadNmiPort: read the Scorpion NMI status port #FFDD (magic
; button state; v4.01 relocates it to #FFDC).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ReadNmiPort:
	ld hl,(0ffddh)		;34a2	2a dd ff	* . .
	ld h,000h		;34a5	26 00		& .
	ld (0dda7h),hl		;34a7	22 a7 dd	" . .
	ret			;34aa	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintFrameCount: display the frame counters at DFD2/DFD4.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintFrameCount:
	ld de,(0dfd2h)		;34ab	ed 5b d2 df	. [ . .
	ld bc,(0dfd4h)		;34af	ed 4b d4 df	. K . .
sub_34b3h:
	push hl			;34b3	e5		.
	push bc			;34b4	c5		.
	push de			;34b5	d5		.
	rst 20h			;34b6	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg34B7: frame-counter label  Total .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg34B7' (start 0x34b7 end 0x34be)
Msg34B7_start:
	defb 020h		;34b7	20		 
	defb 054h		;34b8	54		T
	defb 06fh		;34b9	6f		o
	defb 074h		;34ba	74		t
	defb 061h		;34bb	61		a
	defb 06ch		;34bc	6c		l
	defb 0a0h		;34bd	a0		.
Msg34B7_end:
	pop bc			;34be	c1		.
	pop de			;34bf	d1		.
	push bc			;34c0	c5		.
	push de			;34c1	d5		.
	call sub_1d20h		;34c2	cd 20 1d	.   .
	rst 20h			;34c5	e7		.
	cpl			;34c6	2f		/
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgBad: Bad - bad file/tape status message
; ($A0-terminated).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgBad' (start 0x34c7 end 0x34cc)
MsgBad_start:
	defb 020h		;34c7	20		 
	defb 042h		;34c8	42		B
	defb 061h		;34c9	61		a
	defb 064h		;34ca	64		d
	defb 0a0h		;34cb	a0		.
MsgBad_end:
	pop bc			;34cc	c1		.
	push bc			;34cd	c5		.
	call sub_1d20h		;34ce	cd 20 1d	.   .
	pop bc			;34d1	c1		.
	pop de			;34d2	d1		.
	pop hl			;34d3	e1		.
	ret			;34d4	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; LookupToken: resolve a token against the RAM table at
; $FFC8.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
LookupToken:
	ld bc,Rst10Vector	;34d5	01 10 00	. . .
	call sub_02d6h		;34d8	cd d6 02	. . .
	ld b,a			;34db	47		G
	and 003h		;34dc	e6 03		. .
	push af			;34de	f5		.
	bit 6,b			;34df	cb 70		. p
	jr nz,l34e5h		;34e1	20 02		  .
	add a,004h		;34e3	c6 04		. .
l34e5h:
	ld d,000h		;34e5	16 00		. .
	ld e,a			;34e7	5f		_
	pop bc			;34e8	c1		.
	ld hl,(0ffc8h)		;34e9	2a c8 ff	* . .
	add hl,de		;34ec	19		.
	ld a,(hl)		;34ed	7e		~
	ret			;34ee	c9		.
sub_34efh:
	push hl			;34ef	e5		.
	ld b,008h		;34f0	06 08		. .
l34f2h:
	ld (hl),020h		;34f2	36 20		6  
	inc hl			;34f4	23		#
	djnz l34f2h		;34f5	10 fb		. .
	ld a,089h		;34f7	3e 89		> .
	call sub_156eh		;34f9	cd 6e 15	. n .
	jr c,l3512h		;34fc	38 14		8 .
	jr z,l3512h		;34fe	28 12		( .
	pop de			;3500	d1		.
	push de			;3501	d5		.
	ld bc,Rst08Vector	;3502	01 08 00	. . .
l3505h:
	ld a,(hl)		;3505	7e		~
	cp 020h			;3506	fe 20		.  
	jr c,l350fh		;3508	38 05		8 .
	ldi			;350a	ed a0		. .
	jp pe,l3505h		;350c	ea 05 35	. . 5
l350fh:
	or a			;350f	b7		.
	pop hl			;3510	e1		.
	ret			;3511	c9		.
l3512h:
	scf			;3512	37		7
	pop hl			;3513	e1		.
	ret			;3514	c9		.
sub_3515h:
	ld hl,0e097h		;3515	21 97 e0	! . .
	rst 30h			;3518	f7		.
	call sub_0c31h		;3519	cd 31 0c	. 1 .
	rst 20h			;351c	e7		.
	dec de			;351d	1b		.
	nop			;351e	00		.
	ld a,(bc)		;351f	0a		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3520: message Insert disk, press Y key.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg3520' (start 0x3520 end 0x3538)
Msg3520_start:
	defb 049h		;3520	49		I
	defb 06eh		;3521	6e		n
	defb 073h		;3522	73		s
	defb 065h		;3523	65		e
	defb 072h		;3524	72		r
	defb 074h		;3525	74		t
	defb 020h		;3526	20		 
	defb 064h		;3527	64		d
	defb 069h		;3528	69		i
	defb 073h		;3529	73		s
	defb 06bh		;352a	6b		k
	defb 02ch		;352b	2c		,
	defb 020h		;352c	20		 
	defb 070h		;352d	70		p
	defb 072h		;352e	72		r
	defb 065h		;352f	65		e
	defb 073h		;3530	73		s
	defb 073h		;3531	73		s
	defb 020h		;3532	20		 
	defb 059h		;3533	59		Y
	defb 020h		;3534	20		 
	defb 06bh		;3535	6b		k
	defb 065h		;3536	65		e
	defb 0f9h		;3537	f9		.
Msg3520_end:
	call AutoRepeatKey	;3538	cd 0d 1b	. . .
	and 05fh		;353b	e6 5f		. _
	cp 059h			;353d	fe 59		. Y
	ret nz			;353f	c0		.
	rst 18h			;3540	df		.
	or (hl)			;3541	b6		.
	jr c,$-79		;3542	38 af		8 .
	ret			;3544	c9		.
sub_3545h:
	inc hl			;3545	23		#
	inc hl			;3546	23		#
	inc hl			;3547	23		#
	inc hl			;3548	23		#
	inc hl			;3549	23		#
	or a			;354a	b7		.
	sbc hl,de		;354b	ed 52		. R
	add hl,de		;354d	19		.
	ret			;354e	c9		.
sub_354fh:
	ld hl,0e3f7h		;354f	21 f7 e3	! . .
	ld d,000h		;3552	16 00		. .
	ld bc,l0105h+1		;3554	01 06 01	. . .
	jp sub_02d6h		;3557	c3 d6 02	. . .
l355ah:
	ei			;355a	fb		.
	halt			;355b	76		v
	di			;355c	f3		.
	ld de,(UserPc)		;355d	ed 5b 6b dd	. [ k .
	call sub_3577h		;3561	cd 77 35	. w 5
	ld de,l0ff3h		;3564	11 f3 0f	. . .
	call sub_3577h		;3567	cd 77 35	. w 5
	ld hl,l3d30h		;356a	21 30 3d	! 0 =
	ld (UserPc),hl		;356d	22 6b dd	" k .
	jp l011ch		;3570	c3 1c 01	. . .
	ld hl,l2e4ah		;3573	21 4a 2e	! J .
	ex (sp),hl		;3576	e3		.
sub_3577h:
	ld hl,(UserSp)		;3577	2a 6d dd	* m .
	dec hl			;357a	2b		+
	ld a,d			;357b	7a		z
	rst 0			;357c	c7		.
	dec hl			;357d	2b		+
	ld a,e			;357e	7b		{
	rst 0			;357f	c7		.
	ld (UserSp),hl		;3580	22 6d dd	" m .
	ret			;3583	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CallTrdos - enter a TR-DOS session: #1FFD=$02, #7FFD=$17,
; LDIR 6912 bytes $C000 -> $4000 (DOS screen into place),
; then restore #7FFD=$10 / #1FFD=$12 on return.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CallTrdos:
	bit 3,(iy-002h)		;3584	fd cb fe 5e	. . . ^
	jp z,sub_04cch		;3588	ca cc 04	. . .
	ld bc,l1ffdh		;358b	01 fd 1f	. . .
	ld a,002h		;358e	3e 02		> .
	out (c),a		;3590	ed 79		. y
	ld b,07fh		;3592	06 7f		. .
	ld a,017h		;3594	3e 17		> .
	out (c),a		;3596	ed 79		. y
	ld hl,0c000h		;3598	21 00 c0	! . .
	ld de,ZeroPad3FBE_end	;359b	11 00 40	. . @
	ld bc,PadColumns+1	;359e	01 00 1b	. . .
	ldir			;35a1	ed b0		. .
	ld bc,07ffdh		;35a3	01 fd 7f	. . .
	ld a,010h		;35a6	3e 10		> .
	out (c),a		;35a8	ed 79		. y
	ld b,01fh		;35aa	06 1f		. .
	ld a,012h		;35ac	3e 12		> .
	out (c),a		;35ae	ed 79		. y
	ret			;35b0	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RestorePaging: DFDB shadow -> DFDD paging mirror, ports
; refreshed.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RestorePaging:
	push hl			;35b1	e5		.
	ld hl,(PagingBackup)	;35b2	2a db df	* . .
	ld (PagingState),hl	;35b5	22 dd df	" . .
	pop hl			;35b8	e1		.
	ret			;35b9	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SaveToTape: header + data bytes to tape through
; FeedTapeOutput; leader and parity via the $2F3E+ waits.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SaveToTape:
	call CheckAddressRange	;35ba	cd fe 2e	. . .
	set 4,(iy+009h)		;35bd	fd cb 09 e6	. . . .
	jr l35c6h		;35c1	18 03		. .
	call CheckAddressRange	;35c3	cd fe 2e	. . .
l35c6h:
	ld bc,sub_1abeh		;35c6	01 be 1a	. . .
	jr l35dah		;35c9	18 0f		. .
	call CheckAddressRange	;35cb	cd fe 2e	. . .
	set 4,(iy+009h)		;35ce	fd cb 09 e6	. . . .
	jr l35d7h		;35d2	18 03		. .
	call CheckAddressRange	;35d4	cd fe 2e	. . .
l35d7h:
	ld bc,l19ffh		;35d7	01 ff 19	. . .
l35dah:
	ex de,hl		;35da	eb		.
	call InitErrorWindow	;35db	cd f8 35	. . 5
l35deh:
	call sub_2f6ch		;35de	cd 6c 2f	. l /
	rst 20h			;35e1	e7		.
	adc a,l			;35e2	8d		.
	push de			;35e3	d5		.
	push bc			;35e4	c5		.
	call sub_35f6h		;35e5	cd f6 35	. . 5
	pop bc			;35e8	c1		.
	pop de			;35e9	d1		.
	or a			;35ea	b7		.
	sbc hl,de		;35eb	ed 52		. R
	add hl,de		;35ed	19		.
	jr c,l35deh		;35ee	38 ee		8 .
	call sub_0839h		;35f0	cd 39 08	. 9 .
	ld a,081h		;35f3	3e 81		> .
	ret			;35f5	c9		.
sub_35f6h:
	push bc			;35f6	c5		.
	ret			;35f7	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; InitErrorWindow: open the service window stack and install
; the error hook below so RST 08 lands in a visible window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
InitErrorWindow:
	push hl			;35f8	e5		.
	push de			;35f9	d5		.
	push bc			;35fa	c5		.
	bit 4,(iy+009h)		;35fb	fd cb 09 66	. . . f
	ld hl,PromptWindowDef	;35ff	21 35 e0	! 5 .
	jr z,l3607h		;3602	28 03		( .
	ld hl,0e019h		;3604	21 19 e0	! . .
l3607h:
	rst 30h			;3607	f7		.
	push af			;3608	f5		.
	call z,sub_0c31h	;3609	cc 31 0c	. 1 .
l360ch:
	ld (iy+00ah),000h	;360c	fd 36 0a 00	. 6 . .
	pop af			;3610	f1		.
	jr nz,l3617h		;3611	20 04		  .
	ld (ix+000h),015h	;3613	dd 36 00 15	. 6 . .
l3617h:
	ld hl,ErrorWindowHook	;3617	21 21 36	! ! 6
	ld (0de12h),hl		;361a	22 12 de	" . .
	pop bc			;361d	c1		.
	pop de			;361e	d1		.
	pop hl			;361f	e1		.
	ret			;3620	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ErrorWindowHook: the (DE12)-vectored handler printing
; errors into the popup window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ErrorWindowHook:
	call sub_0839h		;3621	cd 39 08	. 9 .
	ld a,09eh		;3624	3e 9e		> .
	jp ExitToError		;3626	c3 1e 0e	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MenuBuilder: popup-menu code of the ROM tail
; (2.95 layout). It calls OpenPopup ($2B8D), then
; prints the five entries of KeywordTable ($3773)
; with helper strings indexed at
; ErrorShortStrings+3*i+2 ($3D19). The option
; loop ends in the interrupt-mode dispatcher: a
; 3-bytes-per-entry jump table at $369E (im 0 /
; im 1 / im 2 / invalid). The final routine
; ($36AA) copies ten (dest,word) pairs from
; RamInitTable ($3F96) into the workspace.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MenuBuilder' (start 0x3629 end 0x36bd)
MenuBuilder_start:
	call PopupTabStops_end	;3629	cd 8d 2b	. . +
	ld hl,EncodedTable36BD_end	;362c	21 73 37	! s 7
	ld c,000h		;362f	0e 00		. .
l3631h:
	ld b,005h		;3631	06 05		. .
l3633h:
	ld a,(hl)		;3633	7e		~
	or a			;3634	b7		.
	ld a,081h		;3635	3e 81		> .
	ret z			;3637	c8		.
	push bc			;3638	c5		.
	rst 20h			;3639	e7		.
	adc a,l			;363a	8d		.
	ld b,00ah		;363b	06 0a		. .
	call sub_1c95h		;363d	cd 95 1c	. . .
	pop bc			;3640	c1		.
	push bc			;3641	c5		.
	push hl			;3642	e5		.
	ld hl,WordLists_end	;3643	21 19 3d	! . =
	ld a,c			;3646	79		y
	add a,a			;3647	87		.
	add a,c			;3648	81		.
	ld c,a			;3649	4f		O
	inc c			;364a	0c		.
	inc c			;364b	0c		.
	ld b,000h		;364c	06 00		. .
	add hl,bc		;364e	09		.
	ld a,(hl)		;364f	7e		~
	bit 7,a			;3650	cb 7f		. .
	jr z,l3656h		;3652	28 02		( .
	jr l365ah		;3654	18 04		. .
l3656h:
	bit 5,a			;3656	cb 6f		. o
	jr z,l3664h		;3658	28 0a		( .
l365ah:
	rst 20h			;365a	e7		.
	ld h,e			;365b	63		c
	ld l,a			;365c	6f		o
	ld l,l			;365d	6d		m
	ld (hl),b		;365e	70		p
	ld l,h			;365f	6c		l
	ld h,l			;3660	65		e
	ret m			;3661	f8		.
	jr l3677h		;3662	18 13		. .
l3664h:
	bit 6,a			;3664	cb 77		. w
	push af			;3666	f5		.
	jr nz,l366eh		;3667	20 05		  .
	rst 20h			;3669	e7		.
	jr nz,l360ch		;366a	20 a0		  .
	jr l3671h		;366c	18 03		. .
l366eh:
	rst 20h			;366e	e7		.
	inc a			;366f	3c		<
	cp l			;3670	bd		.
l3671h:
	pop af			;3671	f1		.
	and 01fh		;3672	e6 1f		. .
	call sub_32b8h		;3674	cd b8 32	. . 2
l3677h:
	pop hl			;3677	e1		.
	pop bc			;3678	c1		.
	inc c			;3679	0c		.
	djnz l3633h		;367a	10 b7		. .
	push hl			;367c	e5		.
	push bc			;367d	c5		.
	call AutoRepeatKey	;367e	cd 0d 1b	. . .
	pop bc			;3681	c1		.
	pop hl			;3682	e1		.
	cp 007h			;3683	fe 07		. .
	jr nz,l3631h		;3685	20 aa		  .
	ld a,09ah		;3687	3e 9a		> .
	ret			;3689	c9		.
	ld a,e			;368a	7b		{
	and 0fch		;368b	e6 fc		. .
	or d			;368d	b2		.
	ld a,017h		;368e	3e 17		> .
	ret nz			;3690	c0		.
	ld a,081h		;3691	3e 81		> .
	ld hl,WriteAnyBankByte	;3693	21 00 00	! . .
	add hl,de		;3696	19		.
	add hl,de		;3697	19		.
	add hl,de		;3698	19		.
	ld de,l369eh		;3699	11 9e 36	. . 6
	add hl,de		;369c	19		.
	jp (hl)			;369d	e9		.
l369eh:
	im 0			;369e	ed 46		. F
	ret			;36a0	c9		.
	im 1			;36a1	ed 56		. V
	ret			;36a3	c9		.
	im 2			;36a4	ed 5e		. ^
	ret			;36a6	c9		.
	ld a,017h		;36a7	3e 17		> .
	ret			;36a9	c9		.
	ld hl,ErrorShortStrings_end	;36aa	21 96 3f	! . ?
	ld b,00ah		;36ad	06 0a		. .
l36afh:
	ld e,(hl)		;36af	5e		^
	inc hl			;36b0	23		#
	ld d,(hl)		;36b1	56		V
	inc hl			;36b2	23		#
	ld a,(hl)		;36b3	7e		~
	ld (de),a		;36b4	12		.
	inc hl			;36b5	23		#
	inc de			;36b6	13		.
	ld a,(hl)		;36b7	7e		~
	ld (de),a		;36b8	12		.
	inc hl			;36b9	23		#
	djnz l36afh		;36ba	10 f3		. .
	ret			;36bc	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EncodedTable36BD: XOR-masked ROM table, unmasked
; at boot into $E2DB by the decode call at $05BB
; (keys at XorDecodeKeys, $00FC).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MenuBuilder_end:

; BLOCK 'EncodedTable36BD' (start 0x36bd end 0x3773)
EncodedTable36BD_start:
	defb 0f0h		;36bd	f0		.
	defb 06eh		;36be	6e		n
	defb 099h		;36bf	99		.
	defb 063h		;36c0	63		c
	defb 0f6h		;36c1	f6		.
	defb 08ah		;36c2	8a		.
	defb 092h		;36c3	92		.
	defb 0e8h		;36c4	e8		.
	defb 075h		;36c5	75		u
	defb 093h		;36c6	93		.
	defb 067h		;36c7	67		g
	defb 086h		;36c8	86		.
	defb 07ah		;36c9	7a		z
	defb 0edh		;36ca	ed		.
	defb 09dh		;36cb	9d		.
	defb 0a2h		;36cc	a2		.
	defb 09ah		;36cd	9a		.
	defb 0e6h		;36ce	e6		.
	defb 0eeh		;36cf	ee		.
	defb 096h		;36d0	96		.
	defb 0a1h		;36d1	a1		.
	defb 08eh		;36d2	8e		.
	defb 070h		;36d3	70		p
	defb 0dbh		;36d4	db		.
	defb 0a5h		;36d5	a5		.
	defb 0dfh		;36d6	df		.
	defb 09fh		;36d7	9f		.
	defb 0b6h		;36d8	b6		.
	defb 04ah		;36d9	4a		J
	defb 0ddh		;36da	dd		.
	defb 066h		;36db	66		f
	defb 046h		;36dc	46		F
	defb 0afh		;36dd	af		.
	defb 0d3h		;36de	d3		.
	defb 04fh		;36df	4f		O
	defb 0beh		;36e0	be		.
	defb 042h		;36e1	42		B
	defb 0d5h		;36e2	d5		.
	defb 0abh		;36e3	ab		.
	defb 0adh		;36e4	ad		.
	defb 0c9h		;36e5	c9		.
	defb 056h		;36e6	56		V
	defb 0b2h		;36e7	b2		.
	defb 040h		;36e8	40		@
	defb 0a7h		;36e9	a7		.
	defb 059h		;36ea	59		Y
	defb 0cch		;36eb	cc		.
	defb 063h		;36ec	63		c
	defb 0b3h		;36ed	b3		.
	defb 088h		;36ee	88		.
	defb 0b8h		;36ef	b8		.
	defb 055h		;36f0	55		U
	defb 052h		;36f1	52		R
	defb 00ch		;36f2	0c		.
	defb 08dh		;36f3	8d		.
	defb 043h		;36f4	43		C
	defb 0a8h		;36f5	a8		.
	defb 048h		;36f6	48		H
	defb 098h		;36f7	98		.
	defb 078h		;36f8	78		x
	defb 055h		;36f9	55		U
	defb 0a9h		;36fa	a9		.
	defb 03ch		;36fb	3c		<
	defb 04ch		;36fc	4c		L
	defb 034h		;36fd	34		4
	defb 076h		;36fe	76		v
	defb 059h		;36ff	59		Y
	defb 0a3h		;3700	a3		.
	defb 036h		;3701	36		6
	defb 095h		;3702	95		.
	defb 084h		;3703	84		.
	defb 097h		;3704	97		.
	defb 052h		;3705	52		R
	defb 0adh		;3706	ad		.
	defb 02eh		;3707	2e		.
	defb 068h		;3708	68		h
	defb 047h		;3709	47		G
	defb 0b9h		;370a	b9		.
	defb 02ch		;370b	2c		,
	defb 05ch		;370c	5c		\
	defb 044h		;370d	44		D
	defb 066h		;370e	66		f
	defb 049h		;370f	49		I
	defb 0b3h		;3710	b3		.
	defb 026h		;3711	26		&
	defb 09dh		;3712	9d		.
	defb 090h		;3713	90		.
	defb 0ech		;3714	ec		.
	defb 061h		;3715	61		a
	defb 040h		;3716	40		@
	defb 09ah		;3717	9a		.
	defb 0abh		;3718	ab		.
	defb 079h		;3719	79		y
	defb 067h		;371a	67		g
	defb 055h		;371b	55		U
	defb 07bh		;371c	7b		{
	defb 065h		;371d	65		e
	defb 068h		;371e	68		h
	defb 07fh		;371f	7f		.
	defb 066h		;3720	66		f
	defb 030h		;3721	30		0
	defb 091h		;3722	91		.
	defb 088h		;3723	88		.
	defb 08fh		;3724	8f		.
	defb 052h		;3725	52		R
	defb 04ah		;3726	4a		J
	defb 02dh		;3727	2d		-
	defb 0bbh		;3728	bb		.
	defb 043h		;3729	43		C
	defb 077h		;372a	77		w
	defb 088h		;372b	88		.
	defb 09bh		;372c	9b		.
	defb 04ch		;372d	4c		L
	defb 0bdh		;372e	bd		.
	defb 08ch		;372f	8c		.
	defb 07fh		;3730	7f		.
	defb 082h		;3731	82		.
	defb 063h		;3732	63		c
	defb 043h		;3733	43		C
	defb 010h		;3734	10		.
	defb 0eeh		;3735	ee		.
	defb 079h		;3736	79		y
	defb 0f0h		;3737	f0		.
	defb 0c7h		;3738	c7		.
	defb 0ceh		;3739	ce		.
	defb 0f1h		;373a	f1		.
	defb 0c0h		;373b	c0		.
	defb 00bh		;373c	0b		.
	defb 0f6h		;373d	f6		.
	defb 077h		;373e	77		w
	defb 037h		;373f	37		7
	defb 01eh		;3740	1e		.
	defb 0e2h		;3741	e2		.
	defb 075h		;3742	75		u
	defb 00bh		;3743	0b		.
	defb 00dh		;3744	0d		.
	defb 02dh		;3745	2d		-
	defb 000h		;3746	00		.
	defb 0fch		;3747	fc		.
	defb 06fh		;3748	6f		o
	defb 0d6h		;3749	d6		.
	defb 0e5h		;374a	e5		.
	defb 0f6h		;374b	f6		.
	defb 038h		;374c	38		8
	defb 068h		;374d	68		h
	defb 0fbh		;374e	fb		.
	defb 038h		;374f	38		8
	defb 052h		;3750	52		R
	defb 0fch		;3751	fc		.
	defb 0ffh		;3752	ff		.
	defb 0f8h		;3753	f8		.
	defb 003h		;3754	03		.
	defb 022h		;3755	22		"
	defb 012h		;3756	12		.
	defb 0c2h		;3757	c2		.
	defb 0e5h		;3758	e5		.
	defb 017h		;3759	17		.
	defb 019h		;375a	19		.
	defb 025h		;375b	25		%
	defb 019h		;375c	19		.
	defb 018h		;375d	18		.
	defb 0d3h		;375e	d3		.
	defb 015h		;375f	15		.
	defb 01ch		;3760	1c		.
	defb 0f7h		;3761	f7		.
	defb 0f3h		;3762	f3		.
	defb 013h		;3763	13		.
	defb 0fbh		;3764	fb		.
	defb 001h		;3765	01		.
	defb 04bh		;3766	4b		K
	defb 0d2h		;3767	d2		.
	defb 00ch		;3768	0c		.
	defb 04bh		;3769	4b		K
	defb 0d7h		;376a	d7		.
	defb 007h		;376b	07		.
	defb 040h		;376c	40		@
	defb 0d8h		;376d	d8		.
	defb 0d5h		;376e	d5		.
	defb 06fh		;376f	6f		o
	defb 0fdh		;3770	fd		.
	defb 02eh		;3771	2e		.
	defb 03eh		;3772	3e		>
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeywordTable: menu/command keyword strings
; ('PC','BAS','BREa','BR','CAL','CATalog',...); the
; final character of each entry carries bit 7,
; and the menu loop at $3633 stops at a $00 byte.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EncodedTable36BD_end:

; BLOCK 'KeywordTable' (start 0x3773 end 0x3845)
KeywordTable_start:
	defb 02eh		;3773	2e		.
	defb 050h		;3774	50		P
	defb 0c3h		;3775	c3		.
	defb 0aeh		;3776	ae		.
	defb 042h		;3777	42		B
	defb 041h		;3778	41		A
	defb 053h		;3779	53		S
	defb 0c5h		;377a	c5		.
	defb 042h		;377b	42		B
	defb 052h		;377c	52		R
	defb 045h		;377d	45		E
	defb 061h		;377e	61		a
	defb 0ebh		;377f	eb		.
	defb 042h		;3780	42		B
	defb 052h		;3781	52		R
	defb 0cbh		;3782	cb		.
	defb 043h		;3783	43		C
	defb 041h		;3784	41		A
	defb 04ch		;3785	4c		L
	defb 0ech		;3786	ec		.
	defb 043h		;3787	43		C
	defb 041h		;3788	41		A
	defb 054h		;3789	54		T
	defb 061h		;378a	61		a
	defb 06ch		;378b	6c		l
	defb 06fh		;378c	6f		o
	defb 067h		;378d	67		g
	defb 075h		;378e	75		u
	defb 0e5h		;378f	e5		.
	defb 043h		;3790	43		C
	defb 048h		;3791	48		H
	defb 065h		;3792	65		e
	defb 063h		;3793	63		c
	defb 0ebh		;3794	eb		.
	defb 044h		;3795	44		D
	defb 041h		;3796	41		A
	defb 053h		;3797	53		S
	defb 0edh		;3798	ed		.
	defb 044h		;3799	44		D
	defb 041h		;379a	41		A
	defb 054h		;379b	54		T
	defb 0c1h		;379c	c1		.
	defb 044h		;379d	44		D
	defb 045h		;379e	45		E
	defb 04ch		;379f	4c		L
	defb 042h		;37a0	42		B
	defb 072h		;37a1	72		r
	defb 065h		;37a2	65		e
	defb 061h		;37a3	61		a
	defb 0ebh		;37a4	eb		.
	defb 044h		;37a5	44		D
	defb 049h		;37a6	49		I
	defb 053h		;37a7	53		S
	defb 0f3h		;37a8	f3		.
	defb 044h		;37a9	44		D
	defb 055h		;37aa	55		U
	defb 06dh		;37ab	6d		m
	defb 0f0h		;37ac	f0		.
	defb 045h		;37ad	45		E
	defb 052h		;37ae	52		R
	defb 041h		;37af	41		A
	defb 073h		;37b0	73		s
	defb 0e5h		;37b1	e5		.
	defb 045h		;37b2	45		E
	defb 058h		;37b3	58		X
	defb 0d8h		;37b4	d8		.
	defb 045h		;37b5	45		E
	defb 058h		;37b6	58		X
	defb 020h		;37b7	20		 
	defb 041h		;37b8	41		A
	defb 0c6h		;37b9	c6		.
	defb 045h		;37ba	45		E
	defb 058h		;37bb	58		X
	defb 069h		;37bc	69		i
	defb 0f4h		;37bd	f4		.
	defb 046h		;37be	46		F
	defb 049h		;37bf	49		I
	defb 04ch		;37c0	4c		L
	defb 0ech		;37c1	ec		.
	defb 046h		;37c2	46		F
	defb 049h		;37c3	49		I
	defb 04eh		;37c4	4e		N
	defb 0e4h		;37c5	e4		.
	defb 049h		;37c6	49		I
	defb 04eh		;37c7	4e		N
	defb 054h		;37c8	54		T
	defb 065h		;37c9	65		e
	defb 072h		;37ca	72		r
	defb 072h		;37cb	72		r
	defb 075h		;37cc	75		u
	defb 070h		;37cd	70		p
	defb 0f4h		;37ce	f4		.
	defb 049h		;37cf	49		I
	defb 0ceh		;37d0	ce		.
	defb 04ah		;37d1	4a		J
	defb 075h		;37d2	75		u
	defb 06dh		;37d3	6d		m
	defb 0f0h		;37d4	f0		.
	defb 04ch		;37d5	4c		L
	defb 042h		;37d6	42		B
	defb 072h		;37d7	72		r
	defb 065h		;37d8	65		e
	defb 061h		;37d9	61		a
	defb 0ebh		;37da	eb		.
	defb 04ch		;37db	4c		L
	defb 049h		;37dc	49		I
	defb 053h		;37dd	53		S
	defb 054h		;37de	54		T
	defb 042h		;37df	42		B
	defb 072h		;37e0	72		r
	defb 065h		;37e1	65		e
	defb 061h		;37e2	61		a
	defb 0ebh		;37e3	eb		.
	defb 04ch		;37e4	4c		L
	defb 043h		;37e5	43		C
	defb 041h		;37e6	41		A
	defb 054h		;37e7	54		T
	defb 061h		;37e8	61		a
	defb 06ch		;37e9	6c		l
	defb 06fh		;37ea	6f		o
	defb 067h		;37eb	67		g
	defb 075h		;37ec	75		u
	defb 0e5h		;37ed	e5		.
	defb 04ch		;37ee	4c		L
	defb 044h		;37ef	44		D
	defb 049h		;37f0	49		I
	defb 053h		;37f1	53		S
	defb 0f3h		;37f2	f3		.
	defb 04ch		;37f3	4c		L
	defb 044h		;37f4	44		D
	defb 055h		;37f5	55		U
	defb 06dh		;37f6	6d		m
	defb 0f0h		;37f7	f0		.
	defb 04ch		;37f8	4c		L
	defb 04fh		;37f9	4f		O
	defb 041h		;37fa	41		A
	defb 0c4h		;37fb	c4		.
	defb 04dh		;37fc	4d		M
	defb 045h		;37fd	45		E
	defb 04dh		;37fe	4d		M
	defb 06fh		;37ff	6f		o
	defb 072h		;3800	72		r
	defb 0f9h		;3801	f9		.
	defb 04dh		;3802	4d		M
	defb 04fh		;3803	4f		O
	defb 056h		;3804	56		V
	defb 0e5h		;3805	e5		.
	defb 04fh		;3806	4f		O
	defb 050h		;3807	50		P
	defb 054h		;3808	54		T
	defb 069h		;3809	69		i
	defb 06fh		;380a	6f		o
	defb 0eeh		;380b	ee		.
	defb 04fh		;380c	4f		O
	defb 055h		;380d	55		U
	defb 0d4h		;380e	d4		.
	defb 050h		;380f	50		P
	defb 055h		;3810	55		U
	defb 053h		;3811	53		S
	defb 0c8h		;3812	c8		.
	defb 050h		;3813	50		P
	defb 04fh		;3814	4f		O
	defb 0d0h		;3815	d0		.
	defb 052h		;3816	52		R
	defb 041h		;3817	41		A
	defb 0cdh		;3818	cd		.
	defb 052h		;3819	52		R
	defb 04fh		;381a	4f		O
	defb 0cdh		;381b	cd		.
	defb 053h		;381c	53		S
	defb 041h		;381d	41		A
	defb 056h		;381e	56		V
	defb 0c5h		;381f	c5		.
	defb 053h		;3820	53		S
	defb 045h		;3821	45		E
	defb 061h		;3822	61		a
	defb 072h		;3823	72		r
	defb 063h		;3824	63		c
	defb 0e8h		;3825	e8		.
	defb 053h		;3826	53		S
	defb 043h		;3827	43		C
	defb 052h		;3828	52		R
	defb 065h		;3829	65		e
	defb 065h		;382a	65		e
	defb 0eeh		;382b	ee		.
	defb 053h		;382c	53		S
	defb 048h		;382d	48		H
	defb 06fh		;382e	6f		o
	defb 0f7h		;382f	f7		.
	defb 053h		;3830	53		S
	defb 04ch		;3831	4c		L
	defb 04fh		;3832	4f		O
	defb 0d7h		;3833	d7		.
	defb 057h		;3834	57		W
	defb 04fh		;3835	4f		O
	defb 052h		;3836	52		R
	defb 04bh		;3837	4b		K
	defb 073h		;3838	73		s
	defb 070h		;3839	70		p
	defb 061h		;383a	61		a
	defb 063h		;383b	63		c
	defb 0e5h		;383c	e5		.
	defb 000h		;383d	00		.
sub_383eh:
	defb 006h		;383e	06		.
	defb 00eh		;383f	0e		.
	defb 0cdh		;3840	cd		.
	defb 00eh		;3841	0e		.
	defb 003h		;3842	03		.
	defb 007h		;3843	07		.
	defb 0c9h		;3844	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WordLists: packed word lists for the token
; recogniser - menu words ('main','menu',
; 'previous',...), interface words ('speed',
; 'RS232','9600',...) and the long error words
; ('abandoned','breakpoint',...).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
KeywordTable_end:

; BLOCK 'WordLists' (start 0x3845 end 0x3d19)
WordLists_start:
	defb 06dh		;3845	6d		m
	defb 061h		;3846	61		a
	defb 069h		;3847	69		i
	defb 0eeh		;3848	ee		.
	defb 06dh		;3849	6d		m
	defb 065h		;384a	65		e
	defb 06eh		;384b	6e		n
	defb 0f5h		;384c	f5		.
	defb 070h		;384d	70		p
	defb 072h		;384e	72		r
	defb 065h		;384f	65		e
	defb 076h		;3850	76		v
	defb 069h		;3851	69		i
	defb 06fh		;3852	6f		o
	defb 075h		;3853	75		u
	defb 0f3h		;3854	f3		.
	defb 063h		;3855	63		c
	defb 06fh		;3856	6f		o
	defb 06eh		;3857	6e		n
	defb 074h		;3858	74		t
	defb 069h		;3859	69		i
	defb 06eh		;385a	6e		n
	defb 075h		;385b	75		u
	defb 0e5h		;385c	e5		.
	defb 070h		;385d	70		p
	defb 072h		;385e	72		r
	defb 06fh		;385f	6f		o
	defb 067h		;3860	67		g
	defb 072h		;3861	72		r
	defb 061h		;3862	61		a
	defb 0edh		;3863	ed		.
	defb 06dh		;3864	6d		m
	defb 061h		;3865	61		a
	defb 067h		;3866	67		g
	defb 069h		;3867	69		i
	defb 0e3h		;3868	e3		.
	defb 062h		;3869	62		b
	defb 075h		;386a	75		u
	defb 074h		;386b	74		t
	defb 074h		;386c	74		t
	defb 06fh		;386d	6f		o
	defb 0eeh		;386e	ee		.
	defb 06dh		;386f	6d		m
	defb 06fh		;3870	6f		o
	defb 06eh		;3871	6e		n
	defb 069h		;3872	69		i
	defb 074h		;3873	74		t
	defb 06fh		;3874	6f		o
	defb 0f2h		;3875	f2		.
	defb 070h		;3876	70		p
	defb 072h		;3877	72		r
	defb 069h		;3878	69		i
	defb 06eh		;3879	6e		n
	defb 0f4h		;387a	f4		.
	defb 073h		;387b	73		s
	defb 063h		;387c	63		c
	defb 072h		;387d	72		r
	defb 065h		;387e	65		e
	defb 065h		;387f	65		e
	defb 0eeh		;3880	ee		.
	defb 074h		;3881	74		t
	defb 065h		;3882	65		e
	defb 073h		;3883	73		s
	defb 0f4h		;3884	f4		.
	defb 070h		;3885	70		p
	defb 072h		;3886	72		r
	defb 069h		;3887	69		i
	defb 06eh		;3888	6e		n
	defb 074h		;3889	74		t
	defb 065h		;388a	65		e
	defb 0f2h		;388b	f2		.
	defb 064h		;388c	64		d
	defb 069h		;388d	69		i
	defb 073h		;388e	73		s
	defb 0ebh		;388f	eb		.
	defb 053h		;3890	53		S
	defb 065h		;3891	65		e
	defb 074h		;3892	74		t
	defb 020h		;3893	20		 
	defb 055h		;3894	55		U
	defb 0f0h		;3895	f0		.
	defb 0e1h		;3896	e1		.
	defb 070h		;3897	70		p
	defb 06fh		;3898	6f		o
	defb 073h		;3899	73		s
	defb 069h		;389a	69		i
	defb 074h		;389b	74		t
	defb 069h		;389c	69		i
	defb 076h		;389d	76		v
	defb 0e5h		;389e	e5		.
	defb 06eh		;389f	6e		n
	defb 065h		;38a0	65		e
	defb 067h		;38a1	67		g
	defb 061h		;38a2	61		a
	defb 074h		;38a3	74		t
	defb 069h		;38a4	69		i
	defb 076h		;38a5	76		v
	defb 0e5h		;38a6	e5		.
	defb 052h		;38a7	52		R
	defb 041h		;38a8	41		A
	defb 0cdh		;38a9	cd		.
	defb 053h		;38aa	53		S
	defb 068h		;38ab	68		h
	defb 061h		;38ac	61		a
	defb 064h		;38ad	64		d
	defb 06fh		;38ae	6f		o
	defb 0f7h		;38af	f7		.
	defb 073h		;38b0	73		s
	defb 065h		;38b1	65		e
	defb 072h		;38b2	72		r
	defb 076h		;38b3	76		v
	defb 069h		;38b4	69		i
	defb 063h		;38b5	63		c
	defb 0e5h		;38b6	e5		.
	defb 01bh		;38b7	1b		.
	defb 001h		;38b8	01		.
	defb 088h		;38b9	88		.
	defb 01bh		;38ba	1b		.
	defb 003h		;38bb	03		.
	defb 08dh		;38bc	8d		.
	defb 0ebh		;38bd	eb		.
	defb 063h		;38be	63		c
	defb 06fh		;38bf	6f		o
	defb 06eh		;38c0	6e		n
	defb 073h		;38c1	73		s
	defb 074h		;38c2	74		t
	defb 061h		;38c3	61		a
	defb 06eh		;38c4	6e		n
	defb 0f4h		;38c5	f4		.
	defb 0e4h		;38c6	e4		.
	defb 0e4h		;38c7	e4		.
	defb 0f2h		;38c8	f2		.
	defb 0e2h		;38c9	e2		.
	defb 069h		;38ca	69		i
	defb 06eh		;38cb	6e		n
	defb 074h		;38cc	74		t
	defb 065h		;38cd	65		e
	defb 072h		;38ce	72		r
	defb 066h		;38cf	66		f
	defb 061h		;38d0	61		a
	defb 063h		;38d1	63		c
	defb 0e5h		;38d2	e5		.
	defb 073h		;38d3	73		s
	defb 070h		;38d4	70		p
	defb 065h		;38d5	65		e
	defb 065h		;38d6	65		e
	defb 0e4h		;38d7	e4		.
	defb 064h		;38d8	64		d
	defb 061h		;38d9	61		a
	defb 074h		;38da	74		t
	defb 0e1h		;38db	e1		.
	defb 062h		;38dc	62		b
	defb 069h		;38dd	69		i
	defb 074h		;38de	74		t
	defb 0f3h		;38df	f3		.
	defb 073h		;38e0	73		s
	defb 074h		;38e1	74		t
	defb 06fh		;38e2	6f		o
	defb 0f0h		;38e3	f0		.
	defb 073h		;38e4	73		s
	defb 074h		;38e5	74		t
	defb 072h		;38e6	72		r
	defb 069h		;38e7	69		i
	defb 06eh		;38e8	6e		n
	defb 0e7h		;38e9	e7		.
	defb 06ch		;38ea	6c		l
	defb 069h		;38eb	69		i
	defb 06dh		;38ec	6d		m
	defb 069h		;38ed	69		i
	defb 074h		;38ee	74		t
	defb 065h		;38ef	65		e
	defb 0f2h		;38f0	f2		.
	defb 02bh		;38f1	2b		+
	defb 04ch		;38f2	4c		L
	defb 0c6h		;38f3	c6		.
	defb 06ch		;38f4	6c		l
	defb 065h		;38f5	65		e
	defb 06eh		;38f6	6e		n
	defb 067h		;38f7	67		g
	defb 074h		;38f8	74		t
	defb 0e8h		;38f9	e8		.
	defb 070h		;38fa	70		p
	defb 061h		;38fb	61		a
	defb 067h		;38fc	67		g
	defb 0e5h		;38fd	e5		.
	defb 077h		;38fe	77		w
	defb 069h		;38ff	69		i
	defb 064h		;3900	64		d
	defb 074h		;3901	74		t
	defb 0e8h		;3902	e8		.
	defb 063h		;3903	63		c
	defb 065h		;3904	65		e
	defb 06eh		;3905	6e		n
	defb 074h		;3906	74		t
	defb 072h		;3907	72		r
	defb 06fh		;3908	6f		o
	defb 06eh		;3909	6e		n
	defb 069h		;390a	69		i
	defb 063h		;390b	63		c
	defb 0f3h		;390c	f3		.
	defb 052h		;390d	52		R
	defb 053h		;390e	53		S
	defb 032h		;390f	32		2
	defb 033h		;3910	33		3
	defb 0b2h		;3911	b2		.
	defb 039h		;3912	39		9
	defb 036h		;3913	36		6
	defb 030h		;3914	30		0
	defb 0b0h		;3915	b0		.
	defb 031h		;3916	31		1
	defb 032h		;3917	32		2
	defb 030h		;3918	30		0
	defb 0b0h		;3919	b0		.
	defb 0b8h		;391a	b8		.
	defb 0b7h		;391b	b7		.
	defb 0b1h		;391c	b1		.
	defb 0b2h		;391d	b2		.
	defb 04fh		;391e	4f		O
	defb 046h		;391f	46		F
	defb 0c6h		;3920	c6		.
	defb 04fh		;3921	4f		O
	defb 0ceh		;3922	ce		.
	defb 072h		;3923	72		r
	defb 065h		;3924	65		e
	defb 073h		;3925	73		s
	defb 065h		;3926	65		e
	defb 0f4h		;3927	f4		.
	defb 06ch		;3928	6c		l
	defb 069h		;3929	69		i
	defb 06eh		;392a	6e		n
	defb 0e5h		;392b	e5		.
	defb 066h		;392c	66		f
	defb 065h		;392d	65		e
	defb 065h		;392e	65		e
	defb 0e4h		;392f	e4		.
	defb 063h		;3930	63		c
	defb 061h		;3931	61		a
	defb 072h		;3932	72		r
	defb 072h		;3933	72		r
	defb 069h		;3934	69		i
	defb 061h		;3935	61		a
	defb 067h		;3936	67		g
	defb 0e5h		;3937	e5		.
	defb 072h		;3938	72		r
	defb 065h		;3939	65		e
	defb 074h		;393a	74		t
	defb 075h		;393b	75		u
	defb 072h		;393c	72		r
	defb 0eeh		;393d	ee		.
	defb 063h		;393e	63		c
	defb 06fh		;393f	6f		o
	defb 06dh		;3940	6d		m
	defb 070h		;3941	70		p
	defb 075h		;3942	75		u
	defb 074h		;3943	74		t
	defb 065h		;3944	65		e
	defb 0f2h		;3945	f2		.
	defb 066h		;3946	66		f
	defb 06fh		;3947	6f		o
	defb 072h		;3948	72		r
	defb 0edh		;3949	ed		.
	defb 073h		;394a	73		s
	defb 06fh		;394b	6f		o
	defb 075h		;394c	75		u
	defb 06eh		;394d	6e		n
	defb 0e4h		;394e	e4		.
	defb 066h		;394f	66		f
	defb 06fh		;3950	6f		o
	defb 072h		;3951	72		r
	defb 06dh		;3952	6d		m
	defb 061h		;3953	61		a
	defb 0f4h		;3954	f4		.
	defb 064h		;3955	64		d
	defb 072h		;3956	72		r
	defb 069h		;3957	69		i
	defb 076h		;3958	76		v
	defb 0e5h		;3959	e5		.
	defb 074h		;395a	74		t
	defb 072h		;395b	72		r
	defb 061h		;395c	61		a
	defb 063h		;395d	63		c
	defb 0ebh		;395e	eb		.
	defb 073h		;395f	73		s
	defb 069h		;3960	69		i
	defb 064h		;3961	64		d
	defb 0e5h		;3962	e5		.
	defb 073h		;3963	73		s
	defb 065h		;3964	65		e
	defb 063h		;3965	63		c
	defb 074h		;3966	74		t
	defb 06fh		;3967	6f		o
	defb 0f2h		;3968	f2		.
	defb 0c1h		;3969	c1		.
	defb 0c2h		;396a	c2		.
	defb 0c3h		;396b	c3		.
	defb 0c4h		;396c	c4		.
	defb 034h		;396d	34		4
	defb 0b0h		;396e	b0		.
	defb 038h		;396f	38		8
	defb 0b0h		;3970	b0		.
	defb 046h		;3971	46		F
	defb 0cdh		;3972	cd		.
	defb 04dh		;3973	4d		M
	defb 046h		;3974	46		F
	defb 0cdh		;3975	cd		.
	defb 031h		;3976	31		1
	defb 032h		;3977	32		2
	defb 0b8h		;3978	b8		.
	defb 032h		;3979	32		2
	defb 035h		;397a	35		5
	defb 0b6h		;397b	b6		.
	defb 035h		;397c	35		5
	defb 031h		;397d	31		1
	defb 0b2h		;397e	b2		.
	defb 031h		;397f	31		1
	defb 030h		;3980	30		0
	defb 032h		;3981	32		2
	defb 0b4h		;3982	b4		.
	defb 06fh		;3983	6f		o
	defb 06eh		;3984	6e		n
	defb 0e5h		;3985	e5		.
	defb 064h		;3986	64		d
	defb 06fh		;3987	6f		o
	defb 075h		;3988	75		u
	defb 062h		;3989	62		b
	defb 06ch		;398a	6c		l
	defb 0e5h		;398b	e5		.
	defb 068h		;398c	68		h
	defb 061h		;398d	61		a
	defb 06ch		;398e	6c		l
	defb 0e6h		;398f	e6		.
	defb 04ch		;3990	4c		L
	defb 04fh		;3991	4f		O
	defb 0d7h		;3992	d7		.
	defb 048h		;3993	48		H
	defb 049h		;3994	49		I
	defb 047h		;3995	47		G
	defb 0c8h		;3996	c8		.
	defb 072h		;3997	72		r
	defb 065h		;3998	65		e
	defb 074h		;3999	74		t
	defb 072h		;399a	72		r
	defb 0f9h		;399b	f9		.
	defb 075h		;399c	75		u
	defb 074h		;399d	74		t
	defb 069h		;399e	69		i
	defb 06ch		;399f	6c		l
	defb 069h		;39a0	69		i
	defb 074h		;39a1	74		t
	defb 0f9h		;39a2	f9		.
	defb 061h		;39a3	61		a
	defb 06eh		;39a4	6e		n
	defb 061h		;39a5	61		a
	defb 06ch		;39a6	6c		l
	defb 079h		;39a7	79		y
	defb 0f3h		;39a8	f3		.
	defb 062h		;39a9	62		b
	defb 061h		;39aa	61		a
	defb 0e4h		;39ab	e4		.
	defb 063h		;39ac	63		c
	defb 061h		;39ad	61		a
	defb 074h		;39ae	74		t
	defb 061h		;39af	61		a
	defb 06ch		;39b0	6c		l
	defb 06fh		;39b1	6f		o
	defb 067h		;39b2	67		g
	defb 075h		;39b3	75		u
	defb 0e5h		;39b4	e5		.
	defb 069h		;39b5	69		i
	defb 06eh		;39b6	6e		n
	defb 074h		;39b7	74		t
	defb 065h		;39b8	65		e
	defb 072h		;39b9	72		r
	defb 06ch		;39ba	6c		l
	defb 065h		;39bb	65		e
	defb 061h		;39bc	61		a
	defb 076h		;39bd	76		v
	defb 069h		;39be	69		i
	defb 06eh		;39bf	6e		n
	defb 0e7h		;39c0	e7		.
	defb 0aah		;39c1	aa		.
	defb 06eh		;39c2	6e		n
	defb 06fh		;39c3	6f		o
	defb 072h		;39c4	72		r
	defb 06dh		;39c5	6d		m
	defb 061h		;39c6	61		a
	defb 0ech		;39c7	ec		.
	defb 066h		;39c8	66		f
	defb 061h		;39c9	61		a
	defb 073h		;39ca	73		s
	defb 0f4h		;39cb	f4		.
	defb 00ch		;39cc	0c		.
	defb 01bh		;39cd	1b		.
	defb 00ah		;39ce	0a		.
	defb 087h		;39cf	87		.
	defb 01bh		;39d0	1b		.
	defb 00ch		;39d1	0c		.
	defb 08dh		;39d2	8d		.
	defb 06fh		;39d3	6f		o
	defb 0e6h		;39d4	e6		.
	defb 073h		;39d5	73		s
	defb 061h		;39d6	61		a
	defb 076h		;39d7	76		v
	defb 0e5h		;39d8	e5		.
	defb 06ch		;39d9	6c		l
	defb 06fh		;39da	6f		o
	defb 061h		;39db	61		a
	defb 0e4h		;39dc	e4		.
	defb 02eh		;39dd	2e		.
	defb 02eh		;39de	2e		.
	defb 0aeh		;39df	ae		.
	defb 0a6h		;39e0	a6		.
l39e1h:
	defb 061h		;39e1	61		a
	defb 0f4h		;39e2	f4		.
	defb 061h		;39e3	61		a
	defb 062h		;39e4	62		b
	defb 061h		;39e5	61		a
	defb 06eh		;39e6	6e		n
	defb 064h		;39e7	64		d
	defb 06fh		;39e8	6f		o
	defb 06eh		;39e9	6e		n
	defb 065h		;39ea	65		e
	defb 0e4h		;39eb	e4		.
	defb 061h		;39ec	61		a
	defb 06ch		;39ed	6c		l
	defb 072h		;39ee	72		r
	defb 065h		;39ef	65		e
	defb 061h		;39f0	61		a
	defb 064h		;39f1	64		d
	defb 0f9h		;39f2	f9		.
	defb 062h		;39f3	62		b
	defb 061h		;39f4	61		a
	defb 0e4h		;39f5	e4		.
	defb 062h		;39f6	62		b
	defb 069h		;39f7	69		i
	defb 0e7h		;39f8	e7		.
	defb 062h		;39f9	62		b
	defb 06ch		;39fa	6c		l
	defb 06fh		;39fb	6f		o
	defb 063h		;39fc	63		c
	defb 06bh		;39fd	6b		k
	defb 0f3h		;39fe	f3		.
	defb 062h		;39ff	62		b
	defb 072h		;3a00	72		r
	defb 065h		;3a01	65		e
	defb 061h		;3a02	61		a
	defb 06bh		;3a03	6b		k
	defb 070h		;3a04	70		p
	defb 06fh		;3a05	6f		o
	defb 069h		;3a06	69		i
	defb 06eh		;3a07	6e		n
	defb 0f4h		;3a08	f4		.
	defb 062h		;3a09	62		b
	defb 0f9h		;3a0a	f9		.
	defb 063h		;3a0b	63		c
	defb 06fh		;3a0c	6f		o
	defb 06dh		;3a0d	6d		m
	defb 06dh		;3a0e	6d		m
	defb 061h		;3a0f	61		a
	defb 06eh		;3a10	6e		n
	defb 0e4h		;3a11	e4		.
	defb 065h		;3a12	65		e
	defb 06eh		;3a13	6e		n
	defb 074h		;3a14	74		t
	defb 065h		;3a15	65		e
	defb 0f2h		;3a16	f2		.
	defb 065h		;3a17	65		e
	defb 06dh		;3a18	6d		m
	defb 070h		;3a19	70		p
	defb 074h		;3a1a	74		t
	defb 0f9h		;3a1b	f9		.
	defb 064h		;3a1c	64		d
	defb 065h		;3a1d	65		e
	defb 066h		;3a1e	66		f
	defb 069h		;3a1f	69		i
	defb 06eh		;3a20	6e		n
	defb 069h		;3a21	69		i
	defb 074h		;3a22	74		t
	defb 069h		;3a23	69		i
	defb 06fh		;3a24	6f		o
	defb 0eeh		;3a25	ee		.
	defb 064h		;3a26	64		d
	defb 065h		;3a27	65		e
	defb 076h		;3a28	76		v
	defb 069h		;3a29	69		i
	defb 073h		;3a2a	73		s
	defb 069h		;3a2b	69		i
	defb 06fh		;3a2c	6f		o
	defb 0eeh		;3a2d	ee		.
	defb 066h		;3a2e	66		f
	defb 061h		;3a2f	61		a
	defb 069h		;3a30	69		i
	defb 06ch		;3a31	6c		l
	defb 065h		;3a32	65		e
	defb 0e4h		;3a33	e4		.
	defb 066h		;3a34	66		f
	defb 069h		;3a35	69		i
	defb 06ch		;3a36	6c		l
	defb 0e5h		;3a37	e5		.
	defb 066h		;3a38	66		f
	defb 069h		;3a39	69		i
	defb 06eh		;3a3a	6e		n
	defb 069h		;3a3b	69		i
	defb 073h		;3a3c	73		s
	defb 0e8h		;3a3d	e8		.
	defb 066h		;3a3e	66		f
	defb 06fh		;3a3f	6f		o
	defb 075h		;3a40	75		u
	defb 06eh		;3a41	6e		n
	defb 0e4h		;3a42	e4		.
	defb 066h		;3a43	66		f
	defb 065h		;3a44	65		e
	defb 0f7h		;3a45	f7		.
	defb 069h		;3a46	69		i
	defb 064h		;3a47	64		d
	defb 065h		;3a48	65		e
	defb 06eh		;3a49	6e		n
	defb 074h		;3a4a	74		t
	defb 069h		;3a4b	69		i
	defb 063h		;3a4c	63		c
	defb 061h		;3a4d	61		a
	defb 0ech		;3a4e	ec		.
	defb 069h		;3a4f	69		i
	defb 06eh		;3a50	6e		n
	defb 073h		;3a51	73		s
	defb 075h		;3a52	75		u
	defb 066h		;3a53	66		f
	defb 066h		;3a54	66		f
	defb 069h		;3a55	69		i
	defb 063h		;3a56	63		c
	defb 069h		;3a57	69		i
	defb 065h		;3a58	65		e
	defb 06eh		;3a59	6e		n
	defb 0f4h		;3a5a	f4		.
	defb 06bh		;3a5b	6b		k
	defb 06eh		;3a5c	6e		n
	defb 06fh		;3a5d	6f		o
	defb 077h		;3a5e	77		w
	defb 0eeh		;3a5f	ee		.
	defb 06ch		;3a60	6c		l
	defb 06fh		;3a61	6f		o
	defb 06eh		;3a62	6e		n
	defb 0e7h		;3a63	e7		.
	defb 06dh		;3a64	6d		m
	defb 061h		;3a65	61		a
	defb 06eh		;3a66	6e		n
	defb 0f9h		;3a67	f9		.
	defb 06eh		;3a68	6e		n
	defb 0efh		;3a69	ef		.
	defb 06eh		;3a6a	6e		n
	defb 06fh		;3a6b	6f		o
	defb 0f4h		;3a6c	f4		.
	defb 06eh		;3a6d	6e		n
	defb 075h		;3a6e	75		u
	defb 06dh		;3a6f	6d		m
	defb 062h		;3a70	62		b
	defb 065h		;3a71	65		e
	defb 0f2h		;3a72	f2		.
	defb 06eh		;3a73	6e		n
	defb 061h		;3a74	61		a
	defb 06dh		;3a75	6d		m
	defb 0e5h		;3a76	e5		.
	defb 06fh		;3a77	6f		o
	defb 0e6h		;3a78	e6		.
	defb 06fh		;3a79	6f		o
	defb 070h		;3a7a	70		p
	defb 065h		;3a7b	65		e
	defb 072h		;3a7c	72		r
	defb 061h		;3a7d	61		a
	defb 06eh		;3a7e	6e		n
	defb 064h		;3a7f	64		d
	defb 0f3h		;3a80	f3		.
	defb 06fh		;3a81	6f		o
	defb 070h		;3a82	70		p
	defb 063h		;3a83	63		c
	defb 06fh		;3a84	6f		o
	defb 064h		;3a85	64		d
	defb 0e5h		;3a86	e5		.
	defb 06fh		;3a87	6f		o
	defb 070h		;3a88	70		p
	defb 065h		;3a89	65		e
	defb 0eeh		;3a8a	ee		.
	defb 06fh		;3a8b	6f		o
	defb 075h		;3a8c	75		u
	defb 0f4h		;3a8d	f4		.
	defb 070h		;3a8e	70		p
	defb 072h		;3a8f	72		r
	defb 065h		;3a90	65		e
	defb 073h		;3a91	73		s
	defb 065h		;3a92	65		e
	defb 06eh		;3a93	6e		n
	defb 0f4h		;3a94	f4		.
	defb 070h		;3a95	70		p
	defb 072h		;3a96	72		r
	defb 06fh		;3a97	6f		o
	defb 067h		;3a98	67		g
	defb 072h		;3a99	72		r
	defb 061h		;3a9a	61		a
	defb 0edh		;3a9b	ed		.
	defb 072h		;3a9c	72		r
	defb 061h		;3a9d	61		a
	defb 06eh		;3a9e	6e		n
	defb 067h		;3a9f	67		g
	defb 0e5h		;3aa0	e5		.
	defb 072h		;3aa1	72		r
	defb 065h		;3aa2	65		e
	defb 064h		;3aa3	64		d
	defb 065h		;3aa4	65		e
	defb 066h		;3aa5	66		f
	defb 069h		;3aa6	69		i
	defb 06eh		;3aa7	6e		n
	defb 065h		;3aa8	65		e
	defb 0e4h		;3aa9	e4		.
	defb 073h		;3aaa	73		s
	defb 074h		;3aab	74		t
	defb 061h		;3aac	61		a
	defb 063h		;3aad	63		c
	defb 0ebh		;3aae	eb		.
	defb 073h		;3aaf	73		s
	defb 070h		;3ab0	70		p
	defb 061h		;3ab1	61		a
	defb 063h		;3ab2	63		c
	defb 0e5h		;3ab3	e5		.
	defb 073h		;3ab4	73		s
	defb 079h		;3ab5	79		y
	defb 06eh		;3ab6	6e		n
	defb 074h		;3ab7	74		t
	defb 061h		;3ab8	61		a
	defb 0f8h		;3ab9	f8		.
	defb 073h		;3aba	73		s
	defb 074h		;3abb	74		t
	defb 072h		;3abc	72		r
	defb 069h		;3abd	69		i
	defb 06eh		;3abe	6e		n
	defb 0e7h		;3abf	e7		.
	defb 073h		;3ac0	73		s
	defb 06dh		;3ac1	6d		m
	defb 061h		;3ac2	61		a
	defb 06ch		;3ac3	6c		l
	defb 0ech		;3ac4	ec		.
	defb 073h		;3ac5	73		s
	defb 074h		;3ac6	74		t
	defb 061h		;3ac7	61		a
	defb 072h		;3ac8	72		r
	defb 0f4h		;3ac9	f4		.
	defb 073h		;3aca	73		s
	defb 065h		;3acb	65		e
	defb 061h		;3acc	61		a
	defb 072h		;3acd	72		r
	defb 063h		;3ace	63		c
	defb 0e8h		;3acf	e8		.
	defb 074h		;3ad0	74		t
	defb 06fh		;3ad1	6f		o
	defb 0efh		;3ad2	ef		.
	defb 075h		;3ad3	75		u
	defb 06eh		;3ad4	6e		n
	defb 064h		;3ad5	64		d
	defb 065h		;3ad6	65		e
	defb 066h		;3ad7	66		f
	defb 069h		;3ad8	69		i
	defb 06eh		;3ad9	6e		n
	defb 065h		;3ada	65		e
	defb 0e4h		;3adb	e4		.
	defb 077h		;3adc	77		w
	defb 06fh		;3add	6f		o
	defb 072h		;3ade	72		r
	defb 06bh		;3adf	6b		k
	defb 073h		;3ae0	73		s
	defb 070h		;3ae1	70		p
	defb 061h		;3ae2	61		a
	defb 063h		;3ae3	63		c
	defb 0e5h		;3ae4	e5		.
	defb 077h		;3ae5	77		w
	defb 06fh		;3ae6	6f		o
	defb 072h		;3ae7	72		r
	defb 0e4h		;3ae8	e4		.
	defb 07ah		;3ae9	7a		z
	defb 065h		;3aea	65		e
	defb 072h		;3aeb	72		r
	defb 0efh		;3aec	ef		.
	defb 0beh		;3aed	be		.
	defb 073h		;3aee	73		s
	defb 074h		;3aef	74		t
	defb 06fh		;3af0	6f		o
	defb 0f0h		;3af1	f0		.
	defb 065h		;3af2	65		e
	defb 072h		;3af3	72		r
	defb 072h		;3af4	72		r
	defb 06fh		;3af5	6f		o
	defb 0f2h		;3af6	f2		.
	defb 071h		;3af7	71		q
	defb 075h		;3af8	75		u
	defb 069h		;3af9	69		i
	defb 0f4h		;3afa	f4		.
	defb 06fh		;3afb	6f		o
	defb 070h		;3afc	70		p
	defb 065h		;3afd	65		e
	defb 072h		;3afe	72		r
	defb 061h		;3aff	61		a
	defb 06eh		;3b00	6e		n
	defb 0e4h		;3b01	e4		.
	defb 0a9h		;3b02	a9		.
	defb 062h		;3b03	62		b
	defb 072h		;3b04	72		r
	defb 061h		;3b05	61		a
	defb 063h		;3b06	63		c
	defb 06bh		;3b07	6b		k
	defb 065h		;3b08	65		e
	defb 074h		;3b09	74		t
	defb 0f3h		;3b0a	f3		.
	defb 069h		;3b0b	69		i
	defb 06eh		;3b0c	6e		n
	defb 076h		;3b0d	76		v
	defb 061h		;3b0e	61		a
	defb 06ch		;3b0f	6c		l
	defb 069h		;3b10	69		i
	defb 0e4h		;3b11	e4		.
	defb 074h		;3b12	74		t
	defb 079h		;3b13	79		y
	defb 070h		;3b14	70		p
	defb 0e5h		;3b15	e5		.
	defb 069h		;3b16	69		i
	defb 06ch		;3b17	6c		l
	defb 06ch		;3b18	6c		l
	defb 065h		;3b19	65		e
	defb 067h		;3b1a	67		g
	defb 061h		;3b1b	61		a
	defb 0ech		;3b1c	ec		.
	defb 076h		;3b1d	76		v
	defb 061h		;3b1e	61		a
	defb 06ch		;3b1f	6c		l
	defb 075h		;3b20	75		u
	defb 0e5h		;3b21	e5		.
	defb 062h		;3b22	62		b
	defb 061h		;3b23	61		a
	defb 06eh		;3b24	6e		n
	defb 0ebh		;3b25	eb		.
	defb 052h		;3b26	52		R
	defb 041h		;3b27	41		A
	defb 0cdh		;3b28	cd		.
	defb 052h		;3b29	52		R
	defb 04fh		;3b2a	4f		O
	defb 0cdh		;3b2b	cd		.
	defb 066h		;3b2c	66		f
	defb 06fh		;3b2d	6f		o
	defb 072h		;3b2e	72		r
	defb 062h		;3b2f	62		b
	defb 069h		;3b30	69		i
	defb 064h		;3b31	64		d
	defb 064h		;3b32	64		d
	defb 065h		;3b33	65		e
	defb 0eeh		;3b34	ee		.
	defb 070h		;3b35	70		p
	defb 072h		;3b36	72		r
	defb 069h		;3b37	69		i
	defb 06eh		;3b38	6e		n
	defb 0f4h		;3b39	f4		.
	defb 020h		;3b3a	20		 
	defb 057h		;3b3b	57		W
	defb 06fh		;3b3c	6f		o
	defb 072h		;3b3d	72		r
	defb 06bh		;3b3e	6b		k
	defb 069h		;3b3f	69		i
	defb 06eh		;3b40	6e		n
	defb 067h		;3b41	67		g
	defb 020h		;3b42	20		 
	defb 02eh		;3b43	2e		.
	defb 02eh		;3b44	2e		.
	defb 0aeh		;3b45	ae		.
	defb 061h		;3b46	61		a
	defb 064h		;3b47	64		d
	defb 064h		;3b48	64		d
	defb 072h		;3b49	72		r
	defb 065h		;3b4a	65		e
	defb 073h		;3b4b	73		s
	defb 0f3h		;3b4c	f3		.
	defb 073h		;3b4d	73		s
	defb 074h		;3b4e	74		t
	defb 061h		;3b4f	61		a
	defb 074h		;3b50	74		t
	defb 0e5h		;3b51	e5		.
	defb 073h		;3b52	73		s
	defb 065h		;3b53	65		e
	defb 06ch		;3b54	6c		l
	defb 065h		;3b55	65		e
	defb 063h		;3b56	63		c
	defb 074h		;3b57	74		t
	defb 069h		;3b58	69		i
	defb 06fh		;3b59	6f		o
	defb 06eh		;3b5a	6e		n
	defb 073h		;3b5b	73		s
	defb 0bah		;3b5c	ba		.
	defb 069h		;3b5d	69		i
	defb 06eh		;3b5e	6e		n
	defb 069h		;3b5f	69		i
	defb 0f4h		;3b60	f4		.
	defb 063h		;3b61	63		c
	defb 06fh		;3b62	6f		o
	defb 075h		;3b63	75		u
	defb 06eh		;3b64	6e		n
	defb 0f4h		;3b65	f4		.
	defb 02ah		;3b66	2a		*
	defb 02ah		;3b67	2a		*
	defb 0aah		;3b68	aa		.
	defb 063h		;3b69	63		c
	defb 075h		;3b6a	75		u
	defb 072h		;3b6b	72		r
	defb 065h		;3b6c	65		e
	defb 06eh		;3b6d	6e		n
	defb 0f4h		;3b6e	f4		.
l3b6fh:
	defb 061h		;3b6f	61		a
	defb 06eh		;3b70	6e		n
	defb 061h		;3b71	61		a
	defb 06ch		;3b72	6c		l
	defb 079h		;3b73	79		y
	defb 073h		;3b74	73		s
	defb 065h		;3b75	65		e
	defb 0f2h		;3b76	f2		.
	defb 064h		;3b77	64		d
	defb 065h		;3b78	65		e
	defb 066h		;3b79	66		f
	defb 069h		;3b7a	69		i
	defb 06eh		;3b7b	6e		n
	defb 069h		;3b7c	69		i
	defb 074h		;3b7d	74		t
	defb 069h		;3b7e	69		i
	defb 06fh		;3b7f	6f		o
	defb 06eh		;3b80	6e		n
	defb 073h		;3b81	73		s
	defb 0bah		;3b82	ba		.
	defb 06ch		;3b83	6c		l
	defb 065h		;3b84	65		e
	defb 066h		;3b85	66		f
	defb 0f4h		;3b86	f4		.
	defb 066h		;3b87	66		f
	defb 06fh		;3b88	6f		o
	defb 0f2h		;3b89	f2		.
	defb 062h		;3b8a	62		b
	defb 079h		;3b8b	79		y
	defb 074h		;3b8c	74		t
	defb 065h		;3b8d	65		e
	defb 0f3h		;3b8e	f3		.
	defb 050h		;3b8f	50		P
	defb 072h		;3b90	72		r
	defb 06fh		;3b91	6f		o
	defb 066h		;3b92	66		f
	defb 065h		;3b93	65		e
	defb 073h		;3b94	73		s
	defb 073h		;3b95	73		s
	defb 069h		;3b96	69		i
	defb 06fh		;3b97	6f		o
	defb 06eh		;3b98	6e		n
	defb 061h		;3b99	61		a
	defb 0ech		;3b9a	ec		.
	defb 064h		;3b9b	64		d
	defb 069h		;3b9c	69		i
	defb 073h		;3b9d	73		s
	defb 0ebh		;3b9e	eb		.
	defb 054h		;3b9f	54		T
	defb 052h		;3ba0	52		R
	defb 044h		;3ba1	44		D
	defb 04fh		;3ba2	4f		O
	defb 0d3h		;3ba3	d3		.
	defb 065h		;3ba4	65		e
	defb 078h		;3ba5	78		x
	defb 069h		;3ba6	69		i
	defb 073h		;3ba7	73		s
	defb 0f4h		;3ba8	f4		.
	defb 052h		;3ba9	52		R
	defb 02fh		;3baa	2f		/
	defb 0d7h		;3bab	d7		.
l3bach:
	defb 015h		;3bac	15		.
	defb 013h		;3bad	13		.
	defb 014h		;3bae	14		.
	defb 008h		;3baf	08		.
	defb 096h		;3bb0	96		.
	defb 001h		;3bb1	01		.
	defb 082h		;3bb2	82		.
	defb 009h		;3bb3	09		.
	defb 08ah		;3bb4	8a		.
	defb 08eh		;3bb5	8e		.
	defb 00eh		;3bb6	0e		.
	defb 097h		;3bb7	97		.
	defb 08bh		;3bb8	8b		.
	defb 003h		;3bb9	03		.
	defb 082h		;3bba	82		.
	defb 004h		;3bbb	04		.
	defb 085h		;3bbc	85		.
	defb 006h		;3bbd	06		.
	defb 087h		;3bbe	87		.
	defb 088h		;3bbf	88		.
	defb 08ch		;3bc0	8c		.
	defb 00dh		;3bc1	0d		.
	defb 0deh		;3bc2	de		.
	defb 00fh		;3bc3	0f		.
	defb 088h		;3bc4	88		.
	defb 090h		;3bc5	90		.
	defb 091h		;3bc6	91		.
	defb 08ah		;3bc7	8a		.
	defb 092h		;3bc8	92		.
	defb 018h		;3bc9	18		.
	defb 099h		;3bca	99		.
	defb 01ah		;3bcb	1a		.
	defb 09bh		;3bcc	9b		.
	defb 097h		;3bcd	97		.
	defb 01ah		;3bce	1a		.
	defb 01ch		;3bcf	1c		.
	defb 09bh		;3bd0	9b		.
	defb 00eh		;3bd1	0e		.
	defb 08ch		;3bd2	8c		.
	defb 09dh		;3bd3	9d		.
	defb 0a8h		;3bd4	a8		.
	defb 0a9h		;3bd5	a9		.
	defb 09eh		;3bd6	9e		.
	defb 0aah		;3bd7	aa		.
	defb 0abh		;3bd8	ab		.
	defb 01fh		;3bd9	1f		.
	defb 0a0h		;3bda	a0		.
	defb 0ach		;3bdb	ac		.
	defb 0adh		;3bdc	ad		.
	defb 021h		;3bdd	21		!
	defb 0a0h		;3bde	a0		.
	defb 0afh		;3bdf	af		.
	defb 0aeh		;3be0	ae		.
	defb 09fh		;3be1	9f		.
	defb 022h		;3be2	22		"
	defb 023h		;3be3	23		#
	defb 0a4h		;3be4	a4		.
	defb 0b1h		;3be5	b1		.
	defb 0b0h		;3be6	b0		.
	defb 022h		;3be7	22		"
	defb 0a7h		;3be8	a7		.
	defb 026h		;3be9	26		&
	defb 0a5h		;3bea	a5		.
	defb 0b2h		;3beb	b2		.
	defb 033h		;3bec	33		3
	defb 0b4h		;3bed	b4		.
	defb 035h		;3bee	35		5
	defb 0b6h		;3bef	b6		.
	defb 0b7h		;3bf0	b7		.
	defb 05ch		;3bf1	5c		\
	defb 05fh		;3bf2	5f		_
	defb 0ddh		;3bf3	dd		.
	defb 038h		;3bf4	38		8
	defb 0b4h		;3bf5	b4		.
	defb 00eh		;3bf6	0e		.
	defb 088h		;3bf7	88		.
	defb 032h		;3bf8	32		2
	defb 092h		;3bf9	92		.
	defb 0b9h		;3bfa	b9		.
	defb 00eh		;3bfb	0e		.
	defb 00dh		;3bfc	0d		.
	defb 0d1h		;3bfd	d1		.
	defb 00dh		;3bfe	0d		.
	defb 0bbh		;3bff	bb		.
	defb 0bfh		;3c00	bf		.
	defb 0c0h		;3c01	c0		.
	defb 0c1h		;3c02	c1		.
	defb 0c2h		;3c03	c2		.
	defb 0bch		;3c04	bc		.
	defb 0c3h		;3c05	c3		.
	defb 0c4h		;3c06	c4		.
	defb 0bdh		;3c07	bd		.
	defb 0cbh		;3c08	cb		.
	defb 0cch		;3c09	cc		.
	defb 0c6h		;3c0a	c6		.
	defb 0c5h		;3c0b	c5		.
	defb 0beh		;3c0c	be		.
	defb 0c7h		;3c0d	c7		.
	defb 0c8h		;3c0e	c8		.
	defb 0c9h		;3c0f	c9		.
	defb 0cah		;3c10	ca		.
	defb 03ah		;3c11	3a		:
	defb 08dh		;3c12	8d		.
	defb 021h		;3c13	21		!
	defb 005h		;3c14	05		.
	defb 080h		;3c15	80		.
	defb 0cfh		;3c16	cf		.
	defb 04dh		;3c17	4d		M
	defb 0cfh		;3c18	cf		.
	defb 04dh		;3c19	4d		M
	defb 0ceh		;3c1a	ce		.
	defb 0ceh		;3c1b	ce		.
	defb 0bah		;3c1c	ba		.
	defb 050h		;3c1d	50		P
	defb 098h		;3c1e	98		.
	defb 00dh		;3c1f	0d		.
	defb 0d1h		;3c20	d1		.
	defb 00bh		;3c21	0b		.
	defb 08dh		;3c22	8d		.
	defb 052h		;3c23	52		R
	defb 08dh		;3c24	8d		.
	defb 053h		;3c25	53		S
	defb 00dh		;3c26	0d		.
	defb 080h		;3c27	80		.
	defb 054h		;3c28	54		T
	defb 08dh		;3c29	8d		.
	defb 0d5h		;3c2a	d5		.
	defb 032h		;3c2b	32		2
	defb 088h		;3c2c	88		.
	defb 00ch		;3c2d	0c		.
	defb 056h		;3c2e	56		V
	defb 0aeh		;3c2f	ae		.
	defb 00ch		;3c30	0c		.
	defb 056h		;3c31	56		V
	defb 0afh		;3c32	af		.
	defb 0d7h		;3c33	d7		.
	defb 0d8h		;3c34	d8		.
	defb 037h		;3c35	37		7
	defb 09eh		;3c36	9e		.
	defb 059h		;3c37	59		Y
	defb 056h		;3c38	56		V
	defb 058h		;3c39	58		X
	defb 00bh		;3c3a	0b		.
	defb 05bh		;3c3b	5b		[
	defb 037h		;3c3c	37		7
	defb 056h		;3c3d	56		V
	defb 0dah		;3c3e	da		.
	defb 05ch		;3c3f	5c		\
	defb 0deh		;3c40	de		.
	defb 05dh		;3c41	5d		]
	defb 0deh		;3c42	de		.
	defb 05ch		;3c43	5c		\
	defb 05fh		;3c44	5f		_
	defb 05dh		;3c45	5d		]
	defb 05bh		;3c46	5b		[
	defb 085h		;3c47	85		.
l3c48h:
	defb 00ah		;3c48	0a		.
	defb 089h		;3c49	89		.
	defb 006h		;3c4a	06		.
	defb 093h		;3c4b	93		.
	defb 007h		;3c4c	07		.
	defb 080h		;3c4d	80		.
	defb 011h		;3c4e	11		.
	defb 001h		;3c4f	01		.
	defb 080h		;3c50	80		.
	defb 032h		;3c51	32		2
	defb 01ah		;3c52	1a		.
	defb 080h		;3c53	80		.
	defb 0c1h		;3c54	c1		.
	defb 00ah		;3c55	0a		.
	defb 09ah		;3c56	9a		.
	defb 02fh		;3c57	2f		/
	defb 0a4h		;3c58	a4		.
	defb 00ah		;3c59	0a		.
	defb 09bh		;3c5a	9b		.
	defb 02ch		;3c5b	2c		,
	defb 012h		;3c5c	12		.
	defb 09dh		;3c5d	9d		.
	defb 02ch		;3c5e	2c		,
	defb 017h		;3c5f	17		.
	defb 09dh		;3c60	9d		.
	defb 020h		;3c61	20		 
	defb 01ch		;3c62	1c		.
	defb 0a3h		;3c63	a3		.
	defb 015h		;3c64	15		.
	defb 09ah		;3c65	9a		.
	defb 004h		;3c66	04		.
	defb 0a8h		;3c67	a8		.
	defb 027h		;3c68	27		'
	defb 0b3h		;3c69	b3		.
	defb 019h		;3c6a	19		.
	defb 09ah		;3c6b	9a		.
	defb 01ah		;3c6c	1a		.
	defb 02ch		;3c6d	2c		,
	defb 085h		;3c6e	85		.
	defb 035h		;3c6f	35		5
	defb 019h		;3c70	19		.
	defb 091h		;3c71	91		.
	defb 034h		;3c72	34		4
	defb 035h		;3c73	35		5
	defb 019h		;3c74	19		.
	defb 091h		;3c75	91		.
	defb 02ch		;3c76	2c		,
	defb 017h		;3c77	17		.
	defb 0b6h		;3c78	b6		.
	defb 038h		;3c79	38		8
	defb 039h		;3c7a	39		9
	defb 0b7h		;3c7b	b7		.
	defb 009h		;3c7c	09		.
	defb 019h		;3c7d	19		.
	defb 091h		;3c7e	91		.
	defb 03ah		;3c7f	3a		:
	defb 03bh		;3c80	3b		;
	defb 0b5h		;3c81	b5		.
	defb 038h		;3c82	38		8
	defb 03ch		;3c83	3c		<
	defb 0bdh		;3c84	bd		.
	defb 038h		;3c85	38		8
	defb 03ch		;3c86	3c		<
	defb 0beh		;3c87	be		.
	defb 009h		;3c88	09		.
	defb 082h		;3c89	82		.
	defb 03fh		;3c8a	3f		?
	defb 03ch		;3c8b	3c		<
	defb 0beh		;3c8c	be		.
	defb 03fh		;3c8d	3f		?
	defb 03ch		;3c8e	3c		<
	defb 0bdh		;3c8f	bd		.
	defb 02ah		;3c90	2a		*
	defb 031h		;3c91	31		1
	defb 090h		;3c92	90		.
	defb 040h		;3c93	40		@
	defb 082h		;3c94	82		.
	defb 028h		;3c95	28		(
	defb 02ch		;3c96	2c		,
	defb 096h		;3c97	96		.
	defb 028h		;3c98	28		(
	defb 019h		;3c99	19		.
	defb 091h		;3c9a	91		.
	defb 018h		;3c9b	18		.
	defb 02bh		;3c9c	2b		+
	defb 0a8h		;3c9d	a8		.
	defb 038h		;3c9e	38		8
	defb 01ah		;3c9f	1a		.
	defb 087h		;3ca0	87		.
	defb 038h		;3ca1	38		8
	defb 043h		;3ca2	43		C
	defb 087h		;3ca3	87		.
	defb 038h		;3ca4	38		8
	defb 039h		;3ca5	39		9
	defb 087h		;3ca6	87		.
	defb 004h		;3ca7	04		.
	defb 042h		;3ca8	42		B
	defb 087h		;3ca9	87		.
	defb 007h		;3caa	07		.
	defb 0adh		;3cab	ad		.
	defb 048h		;3cac	48		H
	defb 007h		;3cad	07		.
	defb 0c4h		;3cae	c4		.
	defb 018h		;3caf	18		.
	defb 042h		;3cb0	42		B
	defb 03ch		;3cb1	3c		<
	defb 039h		;3cb2	39		9
	defb 043h		;3cb3	43		C
	defb 045h		;3cb4	45		E
	defb 046h		;3cb5	46		F
	defb 0afh		;3cb6	af		.
	defb 047h		;3cb7	47		G
	defb 02dh		;3cb8	2d		-
	defb 0c7h		;3cb9	c7		.
	defb 004h		;3cba	04		.
	defb 09eh		;3cbb	9e		.
	defb 010h		;3cbc	10		.
	defb 0a2h		;3cbd	a2		.
	defb 004h		;3cbe	04		.
	defb 0b5h		;3cbf	b5		.
	defb 014h		;3cc0	14		.
	defb 022h		;3cc1	22		"
	defb 0a6h		;3cc2	a6		.
	defb 022h		;3cc3	22		"
	defb 0adh		;3cc4	ad		.
	defb 03ah		;3cc5	3a		:
	defb 02fh		;3cc6	2f		/
	defb 09bh		;3cc7	9b		.
	defb 018h		;3cc8	18		.
	defb 08ch		;3cc9	8c		.
	defb 02fh		;3cca	2f		/
	defb 019h		;3ccb	19		.
	defb 095h		;3ccc	95		.
	defb 03fh		;3ccd	3f		?
	defb 039h		;3cce	39		9
	defb 0afh		;3ccf	af		.
	defb 049h		;3cd0	49		I
	defb 0cah		;3cd1	ca		.
	defb 026h		;3cd2	26		&
	defb 04bh		;3cd3	4b		K
	defb 04ch		;3cd4	4c		L
	defb 025h		;3cd5	25		%
	defb 080h		;3cd6	80		.
	defb 0cdh		;3cd7	cd		.
	defb 025h		;3cd8	25		%
	defb 019h		;3cd9	19		.
	defb 08bh		;3cda	8b		.
	defb 043h		;3cdb	43		C
	defb 01ch		;3cdc	1c		.
	defb 0a5h		;3cdd	a5		.
	defb 049h		;3cde	49		I
	defb 0b3h		;3cdf	b3		.
	defb 020h		;3ce0	20		 
	defb 01ch		;3ce1	1c		.
	defb 025h		;3ce2	25		%
	defb 0a6h		;3ce3	a6		.
	defb 025h		;3ce4	25		%
	defb 08bh		;3ce5	8b		.
	defb 02eh		;3ce6	2e		.
	defb 094h		;3ce7	94		.
	defb 0aeh		;3ce8	ae		.
	defb 04eh		;3ce9	4e		N
	defb 022h		;3cea	22		"
	defb 019h		;3ceb	19		.
	defb 091h		;3cec	91		.
	defb 015h		;3ced	15		.
	defb 0cfh		;3cee	cf		.
	defb 04fh		;3cef	4f		O
	defb 019h		;3cf0	19		.
	defb 0d0h		;3cf1	d0		.
	defb 00dh		;3cf2	0d		.
	defb 008h		;3cf3	08		.
	defb 0b0h		;3cf4	b0		.
	defb 00eh		;3cf5	0e		.
	defb 081h		;3cf6	81		.
	defb 02ch		;3cf7	2c		,
	defb 012h		;3cf8	12		.
	defb 086h		;3cf9	86		.
	defb 00ah		;3cfa	0a		.
	defb 0c2h		;3cfb	c2		.
	defb 00eh		;3cfc	0e		.
	defb 0cfh		;3cfd	cf		.
	defb 038h		;3cfe	38		8
	defb 00fh		;3cff	0f		.
l3d00h:
	defb 09bh		;3d00	9b		.
	defb 004h		;3d01	04		.
	defb 046h		;3d02	46		F
	defb 01ch		;3d03	1c		.
	defb 08fh		;3d04	8f		.
	defb 00fh		;3d05	0f		.
	defb 003h		;3d06	03		.
	defb 0d1h		;3d07	d1		.
	defb 00fh		;3d08	0f		.
	defb 019h		;3d09	19		.
	defb 091h		;3d0a	91		.
	defb 04fh		;3d0b	4f		O
	defb 018h		;3d0c	18		.
	defb 0a6h		;3d0d	a6		.
	defb 03ah		;3d0e	3a		:
	defb 00fh		;3d0f	0f		.
	defb 0b9h		;3d10	b9		.
	defb 080h		;3d11	80		.
	defb 080h		;3d12	80		.
	defb 080h		;3d13	80		.
	defb 080h		;3d14	80		.
	defb 080h		;3d15	80		.
	defb 052h		;3d16	52		R
	defb 033h		;3d17	33		3
	defb 080h		;3d18	80		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ErrorShortStrings: dense pool of 2-4 character
; strings (error fragments, month names, menu
; decorations) - the message base also used by
; the menu builder via $3D19+3*i+2.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WordLists_end:

; BLOCK 'ErrorShortStrings' (start 0x3d19 end 0x3f96)
ErrorShortStrings_start:
	defb 0e2h		;3d19	e2		.
	defb 02dh		;3d1a	2d		-
	defb 080h		;3d1b	80		.
	defb 0ech		;3d1c	ec		.
	defb 02dh		;3d1d	2d		-
	defb 080h		;3d1e	80		.
	defb 002h		;3d1f	02		.
	defb 02ch		;3d20	2c		,
	defb 001h		;3d21	01		.
	defb 08eh		;3d22	8e		.
	defb 02ah		;3d23	2a		*
	defb 045h		;3d24	45		E
	defb 06dh		;3d25	6d		m
	defb 02ah		;3d26	2a		*
	defb 002h		;3d27	02		.
	defb 067h		;3d28	67		g
	defb 02eh		;3d29	2e		.
	defb 042h		;3d2a	42		B
	defb 07ah		;3d2b	7a		z
	defb 031h		;3d2c	31		1
	defb 041h		;3d2d	41		A
	defb 03eh		;3d2e	3e		>
	defb 02dh		;3d2f	2d		-
l3d30h:
	defb 044h		;3d30	44		D
	defb 06fh		;3d31	6f		o
	defb 030h		;3d32	30		0
	defb 041h		;3d33	41		A
	defb 0dbh		;3d34	db		.
	defb 02dh		;3d35	2d		-
	defb 081h		;3d36	81		.
	defb 03ah		;3d37	3a		:
	defb 02ah		;3d38	2a		*
	defb 001h		;3d39	01		.
	defb 0c3h		;3d3a	c3		.
	defb 035h		;3d3b	35		5
	defb 042h		;3d3c	42		B
	defb 0d4h		;3d3d	d4		.
	defb 035h		;3d3e	35		5
	defb 042h		;3d3f	42		B
	defb 004h		;3d40	04		.
	defb 028h		;3d41	28		(
	defb 020h		;3d42	20		 
	defb 030h		;3d43	30		0
	defb 02eh		;3d44	2e		.
	defb 000h		;3d45	00		.
	defb 034h		;3d46	34		4
	defb 02eh		;3d47	2e		.
	defb 000h		;3d48	00		.
	defb 09eh		;3d49	9e		.
	defb 00dh		;3d4a	0d		.
	defb 000h		;3d4b	00		.
	defb 0c3h		;3d4c	c3		.
	defb 02dh		;3d4d	2d		-
	defb 044h		;3d4e	44		D
	defb 03ah		;3d4f	3a		:
	defb 02ch		;3d50	2c		,
	defb 082h		;3d51	82		.
	defb 051h		;3d52	51		Q
	defb 02eh		;3d53	2e		.
	defb 001h		;3d54	01		.
	defb 055h		;3d55	55		U
	defb 02bh		;3d56	2b		+
	defb 001h		;3d57	01		.
	defb 082h		;3d58	82		.
	defb 02eh		;3d59	2e		.
	defb 042h		;3d5a	42		B
	defb 0dbh		;3d5b	db		.
	defb 028h		;3d5c	28		(
	defb 000h		;3d5d	00		.
	defb 0dbh		;3d5e	db		.
	defb 028h		;3d5f	28		(
	defb 000h		;3d60	00		.
	defb 076h		;3d61	76		v
	defb 031h		;3d62	31		1
	defb 041h		;3d63	41		A
	defb 0bah		;3d64	ba		.
	defb 035h		;3d65	35		5
	defb 042h		;3d66	42		B
	defb 0cbh		;3d67	cb		.
	defb 035h		;3d68	35		5
	defb 042h		;3d69	42		B
	defb 022h		;3d6a	22		"
	defb 028h		;3d6b	28		(
	defb 020h		;3d6c	20		 
	defb 02ah		;3d6d	2a		*
	defb 02eh		;3d6e	2e		.
	defb 001h		;3d6f	01		.
	defb 0feh		;3d70	fe		.
	defb 02ch		;3d71	2c		,
	defb 044h		;3d72	44		D
	defb 01ch		;3d73	1c		.
	defb 02bh		;3d74	2b		+
	defb 002h		;3d75	02		.
	defb 049h		;3d76	49		I
	defb 02bh		;3d77	2b		+
	defb 002h		;3d78	02		.
	defb 073h		;3d79	73		s
	defb 035h		;3d7a	35		5
	defb 001h		;3d7b	01		.
	defb 01bh		;3d7c	1b		.
	defb 02eh		;3d7d	2e		.
	defb 000h		;3d7e	00		.
	defb 0bfh		;3d7f	bf		.
	defb 02eh		;3d80	2e		.
	defb 001h		;3d81	01		.
	defb 08eh		;3d82	8e		.
	defb 02eh		;3d83	2e		.
	defb 001h		;3d84	01		.
	defb 013h		;3d85	13		.
	defb 028h		;3d86	28		(
	defb 020h		;3d87	20		 
	defb 031h		;3d88	31		1
	defb 02ch		;3d89	2c		,
	defb 083h		;3d8a	83		.
	defb 02bh		;3d8b	2b		+
	defb 02fh		;3d8c	2f		/
	defb 041h		;3d8d	41		A
	defb 09eh		;3d8e	9e		.
	defb 02bh		;3d8f	2b		+
	defb 000h		;3d90	00		.
	defb 0c5h		;3d91	c5		.
	defb 028h		;3d92	28		(
	defb 001h		;3d93	01		.
	defb 01fh		;3d94	1f		.
	defb 02ch		;3d95	2c		,
	defb 001h		;3d96	01		.
	defb 000h		;3d97	00		.
l3d98h:
	defb 02dh		;3d98	2d		-
	defb 0e8h		;3d99	e8		.
	defb 05dh		;3d9a	5d		]
	defb 0e8h		;3d9b	e8		.
	defb 06fh		;3d9c	6f		o
	defb 0e8h		;3d9d	e8		.
	defb 04bh		;3d9e	4b		K
	defb 0e8h		;3d9f	e8		.
	defb 000h		;3da0	00		.
	defb 000h		;3da1	00		.
	defb 08dh		;3da2	8d		.
	defb 0e8h		;3da3	e8		.
	defb 000h		;3da4	00		.
	defb 000h		;3da5	00		.
	defb 0bfh		;3da6	bf		.
	defb 0e8h		;3da7	e8		.
	defb 0f7h		;3da8	f7		.
	defb 0e8h		;3da9	e8		.
	defb 0cdh		;3daa	cd		.
	defb 0e8h		;3dab	e8		.
l3dach:
	defb 040h		;3dac	40		@
	defb 051h		;3dad	51		Q
	defb 040h		;3dae	40		@
	defb 04bh		;3daf	4b		K
	defb 040h		;3db0	40		@
	defb 04dh		;3db1	4d		M
	defb 040h		;3db2	40		@
	defb 045h		;3db3	45		E
	defb 040h		;3db4	40		@
	defb 04eh		;3db5	4e		N
	defb 046h		;3db6	46		F
	defb 04ch		;3db7	4c		L
	defb 0ech		;3db8	ec		.
	defb 01fh		;3db9	1f		.
	defb 001h		;3dba	01		.
	defb 009h		;3dbb	09		.
	defb 00dh		;3dbc	0d		.
	defb 03bh		;3dbd	3b		;
	defb 0f1h		;3dbe	f1		.
	defb 01fh		;3dbf	1f		.
	defb 0cah		;3dc0	ca		.
	defb 0ffh		;3dc1	ff		.
	defb 080h		;3dc2	80		.
	defb 03ch		;3dc3	3c		<
	defb 03dh		;3dc4	3d		=
	defb 00dh		;3dc5	0d		.
	defb 038h		;3dc6	38		8
	defb 0f1h		;3dc7	f1		.
	defb 01fh		;3dc8	1f		.
	defb 0cah		;3dc9	ca		.
	defb 0ffh		;3dca	ff		.
	defb 020h		;3dcb	20		 
	defb 039h		;3dcc	39		9
	defb 03ah		;3dcd	3a		:
	defb 042h		;3dce	42		B
	defb 052h		;3dcf	52		R
	defb 018h		;3dd0	18		.
	defb 001h		;3dd1	01		.
	defb 01bh		;3dd2	1b		.
	defb 00dh		;3dd3	0d		.
	defb 03bh		;3dd4	3b		;
	defb 0edh		;3dd5	ed		.
	defb 01fh		;3dd6	1f		.
	defb 0cah		;3dd7	ca		.
	defb 0ffh		;3dd8	ff		.
	defb 080h		;3dd9	80		.
	defb 03ch		;3dda	3c		<
	defb 03dh		;3ddb	3d		=
	defb 04dh		;3ddc	4d		M
	defb 038h		;3ddd	38		8
	defb 0edh		;3dde	ed		.
	defb 01fh		;3ddf	1f		.
	defb 0cah		;3de0	ca		.
	defb 0ffh		;3de1	ff		.
	defb 040h		;3de2	40		@
	defb 039h		;3de3	39		9
	defb 03ah		;3de4	3a		:
	defb 047h		;3de5	47		G
	defb 033h		;3de6	33		3
	defb 0ebh		;3de7	eb		.
	defb 01fh		;3de8	1f		.
	defb 034h		;3de9	34		4
	defb 035h		;3dea	35		5
	defb 036h		;3deb	36		6
	defb 037h		;3dec	37		7
	defb 00fh		;3ded	0f		.
	defb 040h		;3dee	40		@
	defb 0f1h		;3def	f1		.
	defb 01fh		;3df0	1f		.
	defb 0cah		;3df1	ca		.
	defb 0ffh		;3df2	ff		.
	defb 041h		;3df3	41		A
	defb 042h		;3df4	42		B
	defb 043h		;3df5	43		C
	defb 044h		;3df6	44		D
	defb 00fh		;3df7	0f		.
	defb 01ah		;3df8	1a		.
	defb 0edh		;3df9	ed		.
	defb 01fh		;3dfa	1f		.
	defb 0cah		;3dfb	ca		.
	defb 0ffh		;3dfc	ff		.
	defb 047h		;3dfd	47		G
	defb 048h		;3dfe	48		H
	defb 049h		;3dff	49		I
l3e00h:
	defb 04ah		;3e00	4a		J
	defb 041h		;3e01	41		A
	defb 031h		;3e02	31		1
	defb 014h		;3e03	14		.
	defb 001h		;3e04	01		.
	defb 025h		;3e05	25		%
	defb 026h		;3e06	26		&
	defb 041h		;3e07	41		A
	defb 030h		;3e08	30		0
	defb 014h		;3e09	14		.
	defb 080h		;3e0a	80		.
	defb 025h		;3e0b	25		%
	defb 026h		;3e0c	26		&
	defb 040h		;3e0d	40		@
	defb 00ah		;3e0e	0a		.
	defb 040h		;3e0f	40		@
	defb 029h		;3e10	29		)
	defb 002h		;3e11	02		.
	defb 027h		;3e12	27		'
	defb 03fh		;3e13	3f		?
	defb 000h		;3e14	00		.
	defb 0eah		;3e15	ea		.
	defb 002h		;3e16	02		.
	defb 028h		;3e17	28		(
	defb 03eh		;3e18	3e		>
	defb 000h		;3e19	00		.
	defb 079h		;3e1a	79		y
	defb 002h		;3e1b	02		.
	defb 02ah		;3e1c	2a		*
	defb 028h		;3e1d	28		(
	defb 000h		;3e1e	00		.
	defb 021h		;3e1f	21		!
	defb 042h		;3e20	42		B
	defb 02bh		;3e21	2b		+
	defb 027h		;3e22	27		'
	defb 000h		;3e23	00		.
	defb 021h		;3e24	21		!
	defb 002h		;3e25	02		.
	defb 02eh		;3e26	2e		.
	defb 029h		;3e27	29		)
	defb 000h		;3e28	00		.
	defb 021h		;3e29	21		!
	defb 001h		;3e2a	01		.
	defb 023h		;3e2b	23		#
	defb 015h		;3e2c	15		.
	defb 080h		;3e2d	80		.
	defb 00eh		;3e2e	0e		.
	defb 00fh		;3e2f	0f		.
	defb 041h		;3e30	41		A
	defb 01ah		;3e31	1a		.
	defb 015h		;3e32	15		.
	defb 002h		;3e33	02		.
	defb 01bh		;3e34	1b		.
	defb 01ch		;3e35	1c		.
	defb 001h		;3e36	01		.
	defb 01dh		;3e37	1d		.
	defb 015h		;3e38	15		.
	defb 004h		;3e39	04		.
	defb 01eh		;3e3a	1e		.
	defb 01fh		;3e3b	1f		.
l3e3ch:
	defb 001h		;3e3c	01		.
	defb 020h		;3e3d	20		 
	defb 015h		;3e3e	15		.
	defb 008h		;3e3f	08		.
	defb 021h		;3e40	21		!
	defb 022h		;3e41	22		"
	defb 041h		;3e42	41		A
	defb 017h		;3e43	17		.
	defb 015h		;3e44	15		.
	defb 001h		;3e45	01		.
	defb 018h		;3e46	18		.
	defb 019h		;3e47	19		.
	defb 040h		;3e48	40		@
	defb 00bh		;3e49	0b		.
	defb 040h		;3e4a	40		@
	defb 02ch		;3e4b	2c		,
	defb 080h		;3e4c	80		.
	defb 007h		;3e4d	07		.
	defb 080h		;3e4e	80		.
	defb 008h		;3e4f	08		.
	defb 040h		;3e50	40		@
	defb 009h		;3e51	09		.
	defb 040h		;3e52	40		@
	defb 02dh		;3e53	2d		-
	defb 040h		;3e54	40		@
	defb 00ah		;3e55	0a		.
	defb 041h		;3e56	41		A
	defb 003h		;3e57	03		.
	defb 012h		;3e58	12		.
	defb 004h		;3e59	04		.
	defb 00eh		;3e5a	0e		.
	defb 00fh		;3e5b	0f		.
	defb 040h		;3e5c	40		@
	defb 003h		;3e5d	03		.
	defb 040h		;3e5e	40		@
	defb 055h		;3e5f	55		U
	defb 040h		;3e60	40		@
	defb 054h		;3e61	54		T
	defb 040h		;3e62	40		@
	defb 00ch		;3e63	0c		.
	defb 040h		;3e64	40		@
	defb 004h		;3e65	04		.
	defb 040h		;3e66	40		@
	defb 053h		;3e67	53		S
	defb 040h		;3e68	40		@
	defb 05ah		;3e69	5a		Z
	defb 040h		;3e6a	40		@
	defb 05bh		;3e6b	5b		[
	defb 041h		;3e6c	41		A
	defb 058h		;3e6d	58		X
	defb 019h		;3e6e	19		.
	defb 040h		;3e6f	40		@
	defb 056h		;3e70	56		V
	defb 057h		;3e71	57		W
l3e72h:
	defb 088h		;3e72	88		.
	defb 0b3h		;3e73	b3		.
	defb 0b0h		;3e74	b0		.
	defb 0fch		;3e75	fc		.
	defb 010h		;3e76	10		.
	defb 011h		;3e77	11		.
	defb 096h		;3e78	96		.
	defb 0f8h		;3e79	f8		.
	defb 010h		;3e7a	10		.
	defb 0c1h		;3e7b	c1		.
	defb 0f5h		;3e7c	f5		.
	defb 0ebh		;3e7d	eb		.
	defb 012h		;3e7e	12		.
	defb 0e0h		;3e7f	e0		.
	defb 08ch		;3e80	8c		.
	defb 08bh		;3e81	8b		.
	defb 00eh		;3e82	0e		.
	defb 0c1h		;3e83	c1		.
	defb 084h		;3e84	84		.
	defb 0ech		;3e85	ec		.
	defb 0a6h		;3e86	a6		.
	defb 0d6h		;3e87	d6		.
	defb 097h		;3e88	97		.
	defb 0e8h		;3e89	e8		.
	defb 01eh		;3e8a	1e		.
	defb 0d0h		;3e8b	d0		.
	defb 09eh		;3e8c	9e		.
	defb 0fch		;3e8d	fc		.
	defb 06ah		;3e8e	6a		j
	defb 05eh		;3e8f	5e		^
	defb 0afh		;3e90	af		.
	defb 0f3h		;3e91	f3		.
	defb 0aeh		;3e92	ae		.
	defb 09eh		;3e93	9e		.
	defb 070h		;3e94	70		p
	defb 05dh		;3e95	5d		]
	defb 0f5h		;3e96	f5		.
	defb 097h		;3e97	97		.
	defb 070h		;3e98	70		p
	defb 070h		;3e99	70		p
	defb 0e5h		;3e9a	e5		.
	defb 0dfh		;3e9b	df		.
	defb 064h		;3e9c	64		d
	defb 0dbh		;3e9d	db		.
	defb 0f6h		;3e9e	f6		.
	defb 0d6h		;3e9f	d6		.
	defb 062h		;3ea0	62		b
	defb 0dch		;3ea1	dc		.
	defb 0ddh		;3ea2	dd		.
	defb 0c2h		;3ea3	c2		.
	defb 0c0h		;3ea4	c0		.
	defb 08ch		;3ea5	8c		.
	defb 062h		;3ea6	62		b
	defb 04bh		;3ea7	4b		K
	defb 0f7h		;3ea8	f7		.
	defb 088h		;3ea9	88		.
	defb 072h		;3eaa	72		r
	defb 0eah		;3eab	ea		.
	defb 0f8h		;3eac	f8		.
	defb 084h		;3ead	84		.
	defb 078h		;3eae	78		x
	defb 0e9h		;3eaf	e9		.
	defb 0e8h		;3eb0	e8		.
	defb 080h		;3eb1	80		.
	defb 078h		;3eb2	78		x
	defb 0e2h		;3eb3	e2		.
	defb 0f6h		;3eb4	f6		.
	defb 0f7h		;3eb5	f7		.
	defb 0f2h		;3eb6	f2		.
	defb 0bah		;3eb7	ba		.
	defb 054h		;3eb8	54		T
	defb 079h		;3eb9	79		y
	defb 0c5h		;3eba	c5		.
	defb 0cdh		;3ebb	cd		.
	defb 052h		;3ebc	52		R
	defb 008h		;3ebd	08		.
	defb 0cah		;3ebe	ca		.
	defb 0b2h		;3ebf	b2		.
	defb 060h		;3ec0	60		`
	defb 089h		;3ec1	89		.
	defb 0d3h		;3ec2	d3		.
	defb 0aeh		;3ec3	ae		.
	defb 081h		;3ec4	81		.
	defb 097h		;3ec5	97		.
	defb 0c6h		;3ec6	c6		.
	defb 0aah		;3ec7	aa		.
	defb 0e0h		;3ec8	e0		.
	defb 095h		;3ec9	95		.
	defb 0d0h		;3eca	d0		.
	defb 0beh		;3ecb	be		.
	defb 028h		;3ecc	28		(
	defb 018h		;3ecd	18		.
	defb 0b8h		;3ece	b8		.
	defb 0a2h		;3ecf	a2		.
	defb 056h		;3ed0	56		V
	defb 0fah		;3ed1	fa		.
	defb 0e5h		;3ed2	e5		.
	defb 007h		;3ed3	07		.
	defb 010h		;3ed4	10		.
	defb 05ch		;3ed5	5c		\
	defb 0b2h		;3ed6	b2		.
	defb 09bh		;3ed7	9b		.
	defb 03dh		;3ed8	3d		=
	defb 058h		;3ed9	58		X
	defb 080h		;3eda	80		.
	defb 0eah		;3edb	ea		.
	defb 02bh		;3edc	2b		+
	defb 020h		;3edd	20		 
	defb 09eh		;3ede	9e		.
	defb 0eeh		;3edf	ee		.
	defb 03dh		;3ee0	3d		=
	defb 024h		;3ee1	24		$
	defb 094h		;3ee2	94		.
	defb 0f2h		;3ee3	f2		.
	defb 030h		;3ee4	30		0
	defb 038h		;3ee5	38		8
	defb 092h		;3ee6	92		.
	defb 0f6h		;3ee7	f6		.
	defb 020h		;3ee8	20		 
	defb 048h		;3ee9	48		H
	defb 078h		;3eea	78		x
	defb 0fah		;3eeb	fa		.
	defb 02bh		;3eec	2b		+
	defb 044h		;3eed	44		D
	defb 07eh		;3eee	7e		~
	defb 0f7h		;3eef	f7		.
	defb 02ah		;3ef0	2a		*
	defb 040h		;3ef1	40		@
	defb 061h		;3ef2	61		a
	defb 0cbh		;3ef3	cb		.
	defb 006h		;3ef4	06		.
	defb 07ch		;3ef5	7c		|
	defb 04bh		;3ef6	4b		K
	defb 0cfh		;3ef7	cf		.
	defb 018h		;3ef8	18		.
	defb 078h		;3ef9	78		x
	defb 053h		;3efa	53		S
	defb 0c3h		;3efb	c3		.
	defb 00fh		;3efc	0f		.
	defb 074h		;3efd	74		t
	defb 050h		;3efe	50		P
	defb 0c7h		;3eff	c7		.
	defb 00eh		;3f00	0e		.
	defb 070h		;3f01	70		p
	defb 05dh		;3f02	5d		]
	defb 03dh		;3f03	3d		=
	defb 022h		;3f04	22		"
	defb 00ch		;3f05	0c		.
	defb 022h		;3f06	22		"
	defb 06ah		;3f07	6a		j
	defb 084h		;3f08	84		.
	defb 0a9h		;3f09	a9		.
	defb 014h		;3f0a	14		.
	defb 079h		;3f0b	79		y
	defb 073h		;3f0c	73		s
	defb 0d8h		;3f0d	d8		.
	defb 019h		;3f0e	19		.
	defb 062h		;3f0f	62		b
	defb 079h		;3f10	79		y
	defb 0c1h		;3f11	c1		.
	defb 027h		;3f12	27		'
	defb 063h		;3f13	63		c
	defb 050h		;3f14	50		P
	defb 01ch		;3f15	1c		.
	defb 0f2h		;3f16	f2		.
	defb 0dbh		;3f17	db		.
	defb 070h		;3f18	70		p
	defb 018h		;3f19	18		.
	defb 06fh		;3f1a	6f		o
	defb 0f3h		;3f1b	f3		.
	defb 06ch		;3f1c	6c		l
	defb 014h		;3f1d	14		.
	defb 06ah		;3f1e	6a		j
	defb 0aeh		;3f1f	ae		.
	defb 06fh		;3f20	6f		o
	defb 010h		;3f21	10		.
	defb 079h		;3f22	79		y
	defb 0b2h		;3f23	b2		.
	defb 070h		;3f24	70		p
	defb 00ch		;3f25	0c		.
	defb 009h		;3f26	09		.
	defb 0f7h		;3f27	f7		.
	defb 068h		;3f28	68		h
	defb 008h		;3f29	08		.
	defb 064h		;3f2a	64		d
	defb 0b3h		;3f2b	b3		.
	defb 07ah		;3f2c	7a		z
	defb 004h		;3f2d	04		.
	defb 053h		;3f2e	53		S
	defb 091h		;3f2f	91		.
	defb 065h		;3f30	65		e
	defb 000h		;3f31	00		.
	defb 05eh		;3f32	5e		^
	defb 082h		;3f33	82		.
	defb 055h		;3f34	55		U
	defb 03ch		;3f35	3c		<
	defb 033h		;3f36	33		3
	defb 0c2h		;3f37	c2		.
	defb 04dh		;3f38	4d		M
	defb 038h		;3f39	38		8
	defb 07ch		;3f3a	7c		|
	defb 083h		;3f3b	83		.
	defb 07ch		;3f3c	7c		|
	defb 036h		;3f3d	36		6
	defb 07ah		;3f3e	7a		z
	defb 032h		;3f3f	32		2
	defb 0dch		;3f40	dc		.
	defb 0f1h		;3f41	f1		.
	defb 05ah		;3f42	5a		Z
	defb 02eh		;3f43	2e		.
	defb 059h		;3f44	59		Y
	defb 0c9h		;3f45	c9		.
	defb 041h		;3f46	41		A
	defb 02ah		;3f47	2a		*
	defb 064h		;3f48	64		d
	defb 080h		;3f49	80		.
	defb 040h		;3f4a	40		@
	defb 026h		;3f4b	26		&
	defb 06eh		;3f4c	6e		n
	defb 0b9h		;3f4d	b9		.
	defb 05eh		;3f4e	5e		^
	defb 022h		;3f4f	22		"
	defb 064h		;3f50	64		d
	defb 0bch		;3f51	bc		.
	defb 06ah		;3f52	6a		j
	defb 061h		;3f53	61		a
	defb 0eeh		;3f54	ee		.
	defb 066h		;3f55	66		f
	defb 065h		;3f56	65		e
	defb 0e2h		;3f57	e2		.
	defb 06dh		;3f58	6d		m
	defb 061h		;3f59	61		a
	defb 0f2h		;3f5a	f2		.
	defb 061h		;3f5b	61		a
	defb 070h		;3f5c	70		p
	defb 0f2h		;3f5d	f2		.
	defb 06dh		;3f5e	6d		m
	defb 061h		;3f5f	61		a
	defb 0f9h		;3f60	f9		.
	defb 06ah		;3f61	6a		j
	defb 075h		;3f62	75		u
	defb 0eeh		;3f63	ee		.
	defb 06ah		;3f64	6a		j
	defb 075h		;3f65	75		u
	defb 0ech		;3f66	ec		.
	defb 061h		;3f67	61		a
	defb 075h		;3f68	75		u
	defb 0e7h		;3f69	e7		.
	defb 073h		;3f6a	73		s
	defb 065h		;3f6b	65		e
	defb 0f0h		;3f6c	f0		.
	defb 06fh		;3f6d	6f		o
	defb 063h		;3f6e	63		c
	defb 0f4h		;3f6f	f4		.
	defb 06eh		;3f70	6e		n
	defb 06fh		;3f71	6f		o
	defb 0f6h		;3f72	f6		.
	defb 064h		;3f73	64		d
	defb 065h		;3f74	65		e
	defb 0e3h		;3f75	e3		.
	defb 01ah		;3f76	1a		.
	defb 00fh		;3f77	0f		.
	defb 008h		;3f78	08		.
	defb 004h		;3f79	04		.
	defb 016h		;3f7a	16		.
	defb 010h		;3f7b	10		.
	defb 009h		;3f7c	09		.
	defb 005h		;3f7d	05		.
	defb 0c0h		;3f7e	c0		.
	defb 0ffh		;3f7f	ff		.
	defb 000h		;3f80	00		.
	defb 003h		;3f81	03		.
	defb 0c0h		;3f82	c0		.
	defb 0c0h		;3f83	c0		.
	defb 0ffh		;3f84	ff		.
	defb 0ffh		;3f85	ff		.
	defb 0a1h		;3f86	a1		.
	defb 0a1h		;3f87	a1		.
	defb 000h		;3f88	00		.
	defb 000h		;3f89	00		.
	defb 000h		;3f8a	00		.
	defb 000h		;3f8b	00		.
	defb 000h		;3f8c	00		.
	defb 000h		;3f8d	00		.
	defb 0ffh		;3f8e	ff		.
	defb 0ffh		;3f8f	ff		.
	defb 0ffh		;3f90	ff		.
	defb 0ffh		;3f91	ff		.
	defb 000h		;3f92	00		.
	defb 0ffh		;3f93	ff		.
	defb 000h		;3f94	00		.
	defb 000h		;3f95	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RamInitTable: ten (dest,word) pairs written into
; workspace/screen RAM by the walker at $36AA -
; window defaults at $DFFA-$DFFE, further cells
; at $E0AC-$E0B4 and two tags at $C063/$C064.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ErrorShortStrings_end:

; BLOCK 'RamInitTable' (start 0x3f96 end 0x3fbe)
RamInitTable_start:
	defb 0fah		;3f96	fa		.
	defb 0dfh		;3f97	df		.
	defb 008h		;3f98	08		.
	defb 0deh		;3f99	de		.
	defb 0fch		;3f9a	fc		.
	defb 0dfh		;3f9b	df		.
	defb 042h		;3f9c	42		B
	defb 0deh		;3f9d	de		.
	defb 0feh		;3f9e	fe		.
	defb 0dfh		;3f9f	df		.
	defb 042h		;3fa0	42		B
	defb 0deh		;3fa1	de		.
	defb 0ach		;3fa2	ac		.
	defb 0e0h		;3fa3	e0		.
	defb 0a0h		;3fa4	a0		.
	defb 0f8h		;3fa5	f8		.
	defb 0aeh		;3fa6	ae		.
	defb 0e0h		;3fa7	e0		.
	defb 019h		;3fa8	19		.
	defb 03dh		;3fa9	3d		=
	defb 0b0h		;3faa	b0		.
	defb 0e0h		;3fab	e0		.
	defb 073h		;3fac	73		s
	defb 037h		;3fad	37		7
	defb 0b2h		;3fae	b2		.
	defb 0e0h		;3faf	e0		.
	defb 0bbh		;3fb0	bb		.
	defb 028h		;3fb1	28		(
	defb 0b4h		;3fb2	b4		.
	defb 0e0h		;3fb3	e0		.
	defb 0b3h		;3fb4	b3		.
	defb 028h		;3fb5	28		(
	defb 063h		;3fb6	63		c
	defb 0c0h		;3fb7	c0		.
	defb 000h		;3fb8	00		.
	defb 000h		;3fb9	00		.
	defb 064h		;3fba	64		d
	defb 0c0h		;3fbb	c0		.
	defb 000h		;3fbc	00		.
	defb 000h		;3fbd	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ZeroPad3FBE: two orphan one-character strings
; ('c'+$80, 'd'+$80) and zero padding up to the
; end of the 16 KiB page.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RamInitTable_end:

; BLOCK 'ZeroPad3FBE' (start 0x3fbe end 0x4000)
ZeroPad3FBE_start:
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
ZeroPad3FBE_end:
