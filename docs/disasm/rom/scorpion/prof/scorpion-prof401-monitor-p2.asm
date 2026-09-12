; ============================================================================
;  Scorpion ZS-256 Turbo - SERVICE MONITOR  (page 2 of scorp_prof401.rom)
; ============================================================================
;
;  Source   : data/rom/scorp_prof401.rom, bytes $8000-$BFFF (page 2), 16 KiB
;  Family   : Scorpion service monitor, v4.01 ("ProfRAM")
;  Sisters  : scorpion.rom       p2 = v2.x   (base monitor - the labels here
;             were derived from it by byte-run alignment),
;             scorp295.rom      p2 = v2.95  (boot ROM checksum, code moved).
;
;  This file: v4.01 repurposes RST 18h (inline hex parse) and
;             RST 30h (RAM hook at $E3D3 with inline args), tokenizes
;             commands through a linked list at $23A3, and moves the
;             workspace (window defs +$40, stacks/pointers into
;             $E3xx, tables into $E5xx-$EAxx); several base routines
;             (breakpoint UI, tape writer, print hook) are gone.
;  Method   : z80dasm 1.2.0, code/data block map from a dedicated
;             Z80 length-decoder + reachability analysis; labels and
;             comments added by hand after studying the three listings.
;  Style    : educational, after Logan & O'Hara, "The Complete Spectrum
;             ROM Disassembly" (see docs/rom for the inspiration).
;
; ----------------------------------------------------------------------------
;  THE ROM BUNDLE
; ----------------------------------------------------------------------------
;  scorp_prof401.rom is a 64 KiB bundle of four 16 KiB pages.  Hash comparison
;  against data/rom/*.rom shows the real contents (page numbers here are
;  offsets inside the bundle file, NOT the emulator's slot numbering):
;
;    page 0 ($0000-)  patched standard 128K ROM0 (~290 bytes differ from
;                     128.rom - TR-DOS autorun patches)
;    page 1 ($4000-)  patched 48K BASIC ROM (~115 bytes differ from
;                     sos.rom - same autorun patch family)
;    page 2 ($8000-)  THIS service monitor v4.01 (machine-code debugger)
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
;  MONITOR RST API  (stub at $0000-$0038; v2.x/v2.95 share it byte
;  for byte, v4.01 repurposes vectors 18h and 30h)
;
; ----------------------------------------------------------------------------
;    RST 00h  write one byte through the banked window (carry = 1)
;    RST 08h  report error, code in B
;    RST 10h  print character in A
;    RST 18h  parse one inline hex digit (handler at $0E41); the
;             base monitor used this vector for RAM-extension calls
;    RST 20h  print inline bit-7-terminated message (EX (SP),HL trick)
;    RST 28h  read one byte through the banked window (carry = 0)
;    RST 30h  RAM hook: JP $E3D3, inline argument bytes follow
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
;    $DDDC/$DDDE  error message table pointers
;    $E004        AY register-7 save (read/restore pair $0398/$03C2)
;    $E012        paging mirror (E = last #7FFD, D = last #1FFD value)
;    $E00E/$E010  paging shadows (SavePagingShadow writes both)
;    $E014        IY base for the monitor's flags (IY+$00..$16)
;    $E000-$E00C  keyboard state for auto-repeat
;    $E075/$E091/$E0AD/$E0C9  window descriptors (prompt, disasm,
;                              registers) - IX-relative blocks
;    $E09F        default IX work buffer (SetDefaultWorkspace $3019)
;    $E11A        step context copy, $E39A step trampoline buffer
;    $E336        monitor SP ($E38D step engine), $E3A4 cursor
;                 cell save (16 bytes), $E3B6 blink counter
;    $E3B7        work-buffer pointer, $E3BC printer buffer ptr
;    $E52D        watchpoint table: 8 entries x 11 bytes
;    $E9A9        extension menu table (RAM hooks at $EB03/$EB0D)
;    $E9BD        command table (2-byte entries, see RunCommand)
;    $E3BE/$EAED  tables decoded at boot from the XOR tail
;    $EAF5        hardware configuration table (validated at boot)
;
;  All page-2 addresses in the comments below are offsets in this 16 KiB
;  block (z80dasm origin $0000).  Labels are PascalCase per project rules.
;
;  Trailing data at the end of the page: the XOR-masked tail at
;  $3B91-$3FFF (decrypted at boot into $E3BE and $EAED by
;  DecodeTables $06B1) holding RAM hook code, port-name strings
;  and table data; the boot configuration lives behind the
;  pointer cell at $F508, the 6x8 font RAM copy at $FCA0.
;

	org 00000h
EncodedTail_end:	equ 0x4000
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
ErrorTextTable:	equ 0xdddc
ErrorTextTable2:	equ 0xddde
PagingBackup:	equ 0xe00e
PagingState:	equ 0xe012
IyWorkBase:	equ 0xe014
StepFlags:	equ 0xe026
PromptWindowDef:	equ 0xe075
DisasmWindowDef:	equ 0xe091
WorkBuffer:	equ 0xe09f
PopupWindowDef:	equ 0xe0ad
RegWindowDef:	equ 0xe0c9
StepContext:	equ 0xe11a
MonitorStack:	equ 0xe336
StepStub:	equ 0xe39a
CursorCellSave:	equ 0xe3a4
WorkBufferPtr:	equ 0xe3b7
DecodedTables:	equ 0xe3be
WatchTable:	equ 0xe52d
ExtMenuTable:	equ 0xe9a9
CommandTable:	equ 0xe9bd
HwConfigTable:	equ 0xeaf5
BootConfigTable:	equ 0xf508
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 00h - WRITE one byte through the banked window.
; SCF presets carry=1; the shared engine at PeekPokeAnyBank ($058E)
; treats carry as "write".  Twin: RST 28h below enters with carry
; clear = READ.  H bits 6/7 select the window (see $058E).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

WriteAnyBankByte:
	scf			;0000	37		7
l0001h:
	bit 7,h			;0001	cb 7c		. |
l0003h:
	jp PeekPokeAnyBank	;0003	c3 8e 05	. . .
	jr SoftEntry		;0006	18 61		. a
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 08h - report the error whose code is in B (ReportError $02A5).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst08Vector:
	jp ReportError		;0008	c3 a5 02	. . .
l000bh:
	out (c),a		;000b	ed 79		. y
l000dh:
	jp WarmEntry		;000d	c3 9c 00	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 10h - print the character in A (PrintChar $2B12).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst10Vector:
	jp PrintChar		;0010	c3 12 2b	. . +
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
l0015h:
	defb 0c3h		;0015	c3		.
	defb 0b6h		;0016	b6		.
	defb 000h		;0017	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 18h - v4.01 repurposed this vector: JP $0E41 parses ONE
; inline hexadecimal digit (upper-cased) that follows the RST at
; the call site and stacks the corrected resume address (see
; Rst18Handler).  The base monitor used RST 18h for RAM-extension
; calls (RamExtCall $02DC) instead.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst18Vector:
	jp 0e41bh		;0018	c3 1b e4	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Filler001B: three nop bytes of vector-page
; padding between the RST 18 and RST 20 stubs.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Filler001B' (start 0x001b end 0x001e)
Filler001B_start:
	defb 03eh		;001b	3e		>
	defb 000h		;001c	00		.
	defb 0c9h		;001d	c9		.
Filler001B_end:
	jr ColdStart		;001e	18 1b		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RST 20h - print inline message (Rst20Handler $16D0): characters
; follow the call site, the last one carries bit 7 set.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst20Vector:
	jp Rst20Handler		;0020	c3 d0 16	. . .
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
; RST 30h - v4.01 repurposed this vector: JP $E3D3 hands control
; to RAM-resident code (decoded at boot from the XOR tail).  The
; bytes following each RST 30h are inline arguments consumed
; RAM-side through the stacked return address - 22 sites, e.g.
; $06C6 in the boot sequence.  The base monitor used RST 30h to
; set the IX work buffer; SetWorkspace ($301C) keeps that role
; as a plain routine.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Filler002D_end:
Rst30Vector:
	jp 0e3d3h		;0030	c3 d3 e3	. . .
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
	ld hl,HwConfigTable	;0049	21 f5 ea	! . .
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
	defb 028h		;0054	28		(
	defb 0e0h		;0055	e0		.
Filler0053_end:
	push hl			;0056	e5		.
	call sub_1037h		;0057	cd 37 10	. 7 .
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
	call sub_0672h		;007b	cd 72 06	. r .
	pop bc			;007e	c1		.
	call sub_3a93h		;007f	cd 93 3a	. . :
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
; at UserSp, monitor SP=$E336 installed; the boot counter at
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
; SP -> UserSp, monitor stack $E336 installed.  BootCounter==0
; -> full SaveContext + register/IM analysis chain; otherwise the
; dispatch is entered directly.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MonitorEntry:
	di			;00b6	f3		.
	ld a,012h		;00b7	3e 12		> .
	out (c),a		;00b9	ed 79		. y
	ld b,07fh		;00bb	06 7f		. .
	ld a,010h		;00bd	3e 10		> .
	out (c),a		;00bf	ed 79		. y
	ld (UserSp),sp		;00c1	ed 73 6d dd	. s m .
	ld (0ddf7h),sp		;00c5	ed 73 f7 dd	. s . .
	ld sp,0e38dh		;00c9	31 8d e3	1 . .
	ld bc,(BootCounter)	;00cc	ed 4b 64 c0	. K d .
	ld a,c			;00d0	79		y
	or b			;00d1	b0		.
	jr nz,l0116h		;00d2	20 42		  B
	call SaveContext	;00d4	cd ce 03	. . .
	call AyReadRegister	;00d7	cd 98 03	. . .
	ld a,(UserIm)		;00da	3a 86 dd	: . .
	cp 080h			;00dd	fe 80		. .
	jp z,l0922h		;00df	ca 22 09	. " .
	call CheckWatchpoints	;00e2	cd 22 32	. " 2
	ld a,(UserIm)		;00e5	3a 86 dd	: . .
	and 003h		;00e8	e6 03		. .
	jr nz,l0114h		;00ea	20 28		  (
	call sub_0373h		;00ec	cd 73 03	. s .
	jr l011dh		;00ef	18 2c		. ,
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Filler00F1: unreferenced junk immediately
; before the XOR key table at $00FC.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Filler00F1' (start 0x00f1 end 0x00fc)
Filler00F1_start:
	defb 061h		;00f1	61		a
	defb 06fh		;00f2	6f		o
	defb 008h		;00f3	08		.
	defb 035h		;00f4	35		5
	defb 0cah		;00f5	ca		.
l00f6h:
	defb 080h		;00f6	80		.
	defb 071h		;00f7	71		q
	defb 01ah		;00f8	1a		.
	defb 06bh		;00f9	6b		k
	defb 0d7h		;00fa	d7		.
l00fbh:
	defb 001h		;00fb	01		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; XorKeys: private key bytes for DecodeXorTable ($3837); several
; ROM tables are stored XOR-masked and are unmasked into RAM at boot.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Filler00F1_end:
XorDecodeKeys:

; BLOCK 'XorKeys' (start 0x00fc end 0x0100)
XorKeys_start:
	defb 005h		;00fc	05		.
l00fdh:
	defb 007h		;00fd	07		.
l00feh:
	defb 061h		;00fe	61		a
	defb 016h		;00ff	16		.
XorKeys_end:
	push hl			;0100	e5		.
	ld (bc),a		;0101	02		.
	nop			;0102	00		.
	nop			;0103	00		.
	nop			;0104	00		.
l0105h:
	nop			;0105	00		.
	nop			;0106	00		.
	nop			;0107	00		.
	nop			;0108	00		.
	nop			;0109	00		.
	nop			;010a	00		.
	nop			;010b	00		.
	nop			;010c	00		.
	nop			;010d	00		.
	nop			;010e	00		.
	nop			;010f	00		.
	nop			;0110	00		.
	inc c			;0111	0c		.
	ex af,af'		;0112	08		.
	inc b			;0113	04		.
l0114h:
	jr l0167h		;0114	18 51		. Q
l0116h:
	push bc			;0116	c5		.
l0117h:
	dec bc			;0117	0b		.
	ld a,b			;0118	78		x
	or c			;0119	b1		.
	jr nz,l0117h		;011a	20 fb		  .
	ret			;011c	c9		.
l011dh:
	ld a,(0e039h)		;011d	3a 39 e0	: 9 .
	ld b,a			;0120	47		G
l0121h:
	call sub_0241h		;0121	cd 41 02	. A .
	djnz l0121h		;0124	10 fb		. .
	call sub_2c30h		;0126	cd 30 2c	. 0 ,
l0129h:
	bit 0,(iy+012h)		;0129	fd cb 12 46	. . . F
	jr z,l0136h		;012d	28 07		( .
	ld a,(0c063h)		;012f	3a 63 c0	: c .
	or a			;0132	b7		.
	jp z,l0af2h		;0133	ca f2 0a	. . .
l0136h:
	rst 30h			;0136	f7		.
	or l			;0137	b5		.
	ld c,005h		;0138	0e 05		. .
	call sub_0370h		;013a	cd 70 03	. p .
l013dh:
	ld sp,0e38dh		;013d	31 8d e3	1 . .
	call sub_0581h		;0140	cd 81 05	. . .
l0143h:
	call sub_3228h		;0143	cd 28 32	. ( 2
l0146h:
	call sub_024ch		;0146	cd 4c 02	. L .
	res 3,(iy+012h)		;0149	fd cb 12 9e	. . . .
	ld a,(UserIm)		;014d	3a 86 dd	: . .
	and 00ch		;0150	e6 0c		. .
	call nz,0e483h		;0152	c4 83 e4	. . .
	call AyWriteData	;0155	cd c2 03	. . .
	res 7,(iy+012h)		;0158	fd cb 12 be	. . . .
	call sub_04e2h		;015c	cd e2 04	. . .
	xor a			;015f	af		.
	ld sp,(UserSp)		;0160	ed 7b 6d dd	. { m .
	jp l000bh		;0164	c3 0b 00	. . .
l0167h:
	ld hl,(UserPc)		;0167	2a 6b dd	* k .
	dec hl			;016a	2b		+
	ld (UserPc),hl		;016b	22 6b dd	" k .
	ld a,(StepFlags)	;016e	3a 26 e0	: & .
	bit 5,a			;0171	cb 6f		. o
	jr z,l018eh		;0173	28 19		( .
	ex de,hl		;0175	eb		.
	ld hl,(StepContext)	;0176	2a 1a e1	* . .
	push hl			;0179	e5		.
	scf			;017a	37		7
	sbc hl,de		;017b	ed 52		. R
	ex de,hl		;017d	eb		.
	pop de			;017e	d1		.
	jr nc,l018eh		;017f	30 0d		0 .
	ex de,hl		;0181	eb		.
	push de			;0182	d5		.
	ld de,Rst08Vector	;0183	11 08 00	. . .
	add hl,de		;0186	19		.
	pop de			;0187	d1		.
	sbc hl,de		;0188	ed 52		. R
	ex de,hl		;018a	eb		.
	jp nc,l0557h		;018b	d2 57 05	. W .
l018eh:
	bit 4,a			;018e	cb 67		. g
	res 4,(iy+012h)		;0190	fd cb 12 a6	. . . .
	jr nz,l01f2h		;0194	20 5c		  \
	call sub_328bh		;0196	cd 8b 32	. . 2
	ld e,000h		;0199	1e 00		. .
	jr c,l01f5h		;019b	38 58		8 X
l019dh:
	ld l,(ix+007h)		;019d	dd 6e 07	. n .
	ld h,(ix+008h)		;01a0	dd 66 08	. f .
	ld a,l			;01a3	7d		}
	or h			;01a4	b4		.
	jr z,l01b2h		;01a5	28 0b		( .
	dec hl			;01a7	2b		+
	ld (ix+007h),l		;01a8	dd 75 07	. u .
	ld (ix+008h),h		;01ab	dd 74 08	. t .
	ld a,h			;01ae	7c		|
	or l			;01af	b5		.
	jr nz,l01e0h		;01b0	20 2e		  .
l01b2h:
	ld l,(ix+005h)		;01b2	dd 6e 05	. n .
	ld (ix+007h),l		;01b5	dd 75 07	. u .
	ld h,(ix+006h)		;01b8	dd 66 06	. f .
	ld (ix+008h),h		;01bb	dd 74 08	. t .
	push ix			;01be	dd e5		. .
	pop hl			;01c0	e1		.
	bit 1,(hl)		;01c1	cb 4e		. N
	jr nz,l01c7h		;01c3	20 02		  .
	res 7,(hl)		;01c5	cb be		. .
l01c7h:
	ld a,009h		;01c7	3e 09		> .
	sub b			;01c9	90		.
	call LookupCommand	;01ca	cd 10 32	. . 2
	push af			;01cd	f5		.
	call nz,sub_1de5h	;01ce	c4 e5 1d	. . .
	pop af			;01d1	f1		.
l01d2h:
	ld l,a			;01d2	6f		o
	ld a,083h		;01d3	3e 83		> .
l01d5h:
	ld h,000h		;01d5	26 00		& .
	ld (0dda7h),hl		;01d7	22 a7 dd	" . .
	call sub_0373h		;01da	cd 73 03	. s .
	jp ExitToError		;01dd	c3 06 0b	. . .
l01e0h:
	bit 7,e			;01e0	cb 7b		. {
	jp nz,l2c9dh		;01e2	c2 9d 2c	. . ,
	ld sp,MonitorStack	;01e5	31 36 e3	1 6 .
	res 7,(iy+016h)		;01e8	fd cb 16 be	. . . .
	call BuildStepStub	;01ec	cd f0 17	. . .
l01efh:
	jp l0143h		;01ef	c3 43 01	. C .
l01f2h:
	xor a			;01f2	af		.
	jr l01d2h		;01f3	18 dd		. .
l01f5h:
	push de			;01f5	d5		.
	inc hl			;01f6	23		#
	rst 28h			;01f7	ef		.
	cp 0ffh			;01f8	fe ff		. .
	jr z,l0200h		;01fa	28 04		( .
	sla a			;01fc	cb 27		. '
	jr c,l0225h		;01fe	38 25		8 %
l0200h:
	ex de,hl		;0200	eb		.
	call sub_3a14h		;0201	cd 14 3a	. . :
	bit 4,(iy-002h)		;0204	fd cb fe 66	. . . f
	jr z,l01f2h		;0208	28 e8		( .
	ld hl,(05c5dh)		;020a	2a 5d 5c	* ] \
	ld (05c5fh),hl		;020d	22 5f 5c	" _ \
	ld hl,Filler0053_start	;0210	21 53 00	! S .
	ld (UserPc),hl		;0213	22 6b dd	" k .
l0216h:
	res 5,(iy+00bh)		;0216	fd cb 0b ae	. . . .
	pop de			;021a	d1		.
	bit 7,e			;021b	cb 7b		. {
	jr z,l01efh		;021d	28 d0		( .
	ld hl,(UserPc)		;021f	2a 6b dd	* k .
	jp l18c0h		;0222	c3 c0 18	. . .
l0225h:
	cp 01bh			;0225	fe 1b		. .
	jr nc,l0200h		;0227	30 d7		0 .
	inc hl			;0229	23		#
	ld (UserPc),hl		;022a	22 6b dd	" k .
	ld hl,l0216h		;022d	21 16 02	! . .
	push hl			;0230	e5		.
	set 5,(iy+00bh)		;0231	fd cb 0b ee	. . . .
	ld hl,l08b7h		;0235	21 b7 08	! . .
	ld e,a			;0238	5f		_
	ld d,000h		;0239	16 00		. .
	add hl,de		;023b	19		.
	ld a,(hl)		;023c	7e		~
	inc hl			;023d	23		#
	ld h,(hl)		;023e	66		f
	ld l,a			;023f	6f		o
	jp (hl)			;0240	e9		.
sub_0241h:
	push bc			;0241	c5		.
	ld bc,l0400h		;0242	01 00 04	. . .
l0245h:
	dec bc			;0245	0b		.
	ld a,c			;0246	79		y
	or b			;0247	b0		.
	jr nz,l0245h		;0248	20 fb		  .
	pop bc			;024a	c1		.
	ret			;024b	c9		.
sub_024ch:
	ld a,(0ffdch)		;024c	3a dc ff	: . .
	bit 5,a			;024f	cb 6f		. o
	ld a,(0ffcah)		;0251	3a ca ff	: . .
	jr nz,l025ah		;0254	20 04		  .
	ld (0e9f2h),a		;0256	32 f2 e9	2 . .
	ret			;0259	c9		.
l025ah:
	ld (0e9f1h),a		;025a	32 f1 e9	2 . .
	ret			;025d	c9		.
	call ReadPort1FFD	;025e	cd d9 04	. . .
	call ReadPort7FFD	;0261	cd d5 04	. . .
	ld a,0afh		;0264	3e af		> .
	ld (0e516h),a		;0266	32 16 e5	2 . .
	call ShortDelay		;0269	cd 1f 2c	. . ,
	jr z,l0276h		;026c	28 08		( .
	ld a,(0e02dh)		;026e	3a 2d e0	: - .
	or 0c0h			;0271	f6 c0		. .
	ld (0e02dh),a		;0273	32 2d e0	2 - .
l0276h:
	ld (0e52bh),hl		;0276	22 2b e5	" + .
	ld a,0cdh		;0279	3e cd		> .
	ld (0e51ch),a		;027b	32 1c e5	2 . .
	ld c,0ffh		;027e	0e ff		. .
	xor a			;0280	af		.
	ld e,a			;0281	5f		_
	ld d,a			;0282	57		W
	inc a			;0283	3c		<
	ld b,a			;0284	47		G
	ei			;0285	fb		.
	halt			;0286	76		v
	inc a			;0287	3c		<
	ld b,a			;0288	47		G
l0289h:
	call 0e483h		;0289	cd 83 e4	. . .
	defb 0edh,070h ;in f,(c)	;028c	ed 70		. p
	call 0e483h		;028e	cd 83 e4	. . .
	dec de			;0291	1b		.
	or a			;0292	b7		.
	jr nz,l0289h		;0293	20 f4		  .
	di			;0295	f3		.
	rst 30h			;0296	f7		.
	ret pe			;0297	e8		.
	ld (bc),a		;0298	02		.
	inc b			;0299	04		.
	ld (0dffch),de		;029a	ed 53 fc df	. S . .
	xor a			;029e	af		.
	jp nz,l3052h		;029f	c2 52 30	. R 0
	jp Filler0053_end	;02a2	c3 56 00	. V .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ReportError - RST 08h backend, error code in B.  Walks the
; message table at (ErrorTextTable) with C as a rotating bit mask
; ($80, sla per entry); bit 7 chain-loads sub-strings.  Codes
; without a table entry fall back to printing the number.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ReportError:
	rst 30h			;02a5	f7		.
	call 00511h		;02a6	cd 11 05	. . .
	ret			;02a9	c9		.
	rst 30h			;02aa	f7		.
	ccf			;02ab	3f		?
	add hl,bc		;02ac	09		.
	ld b,03eh		;02ad	06 3e		. >
	rlca			;02af	07		.
	ld (0dd80h),a		;02b0	32 80 dd	2 . .
	rst 30h			;02b3	f7		.
	ld e,d			;02b4	5a		Z
	dec d			;02b5	15		.
	rlca			;02b6	07		.
	rst 30h			;02b7	f7		.
	sub h			;02b8	94		.
	inc bc			;02b9	03		.
	rlca			;02ba	07		.
	ret nz			;02bb	c0		.
	ld hl,0e005h		;02bc	21 05 e0	! . .
l02bfh:
	in a,(01fh)		;02bf	db 1f		. .
	and h			;02c1	a4		.
	jr z,l02bfh		;02c2	28 fb		( .
	ret m			;02c4	f8		.
	ret pe			;02c5	e8		.
	res 5,h			;02c6	cb ac		. .
	inc (hl)		;02c8	34		4
	inc hl			;02c9	23		#
	ld (hl),l		;02ca	75		u
	ret			;02cb	c9		.
	rst 30h			;02cc	f7		.
	xor a			;02cd	af		.
	ld bc,0c907h		;02ce	01 07 c9	. . .
	call sub_02ddh		;02d1	cd dd 02	. . .
	set 6,(hl)		;02d4	cb f6		. .
	ret			;02d6	c9		.
	call sub_02ddh		;02d7	cd dd 02	. . .
	res 6,(hl)		;02da	cb b6		. .
	ret			;02dc	c9		.
sub_02ddh:
	xor a			;02dd	af		.
	ld hl,0e02dh		;02de	21 2d e0	! - .
	bit 7,(hl)		;02e1	cb 7e		. ~
	jr nz,l02e7h		;02e3	20 02		  .
	inc a			;02e5	3c		<
	pop bc			;02e6	c1		.
l02e7h:
	ld (0dd7fh),a		;02e7	32 7f dd	2 . .
	ret			;02ea	c9		.
	call sub_032ah		;02eb	cd 2a 03	. * .
	rst 30h			;02ee	f7		.
	dec sp			;02ef	3b		;
	inc b			;02f0	04		.
	dec b			;02f1	05		.
	jr l0310h		;02f2	18 1c		. .
	ld hl,0ea49h		;02f4	21 49 ea	! I .
	ld (0e9f3h),hl		;02f7	22 f3 e9	" . .
	ld hl,0ffdch		;02fa	21 dc ff	! . .
	bit 5,(hl)		;02fd	cb 6e		. n
	jr z,l0309h		;02ff	28 08		( .
	res 5,(hl)		;0301	cb ae		. .
	ld a,(0e9f2h)		;0303	3a f2 e9	: . .
	call sub_2c4dh		;0306	cd 4d 2c	. M ,
l0309h:
	call sub_032ah		;0309	cd 2a 03	. * .
	rst 30h			;030c	f7		.
	add hl,hl		;030d	29		)
	ld (bc),a		;030e	02		.
	rlca			;030f	07		.
l0310h:
	ld (0dd77h),bc		;0310	ed 43 77 dd	. C w .
	ld (0dd75h),de		;0314	ed 53 75 dd	. S u .
l0318h:
	ld (RegisterFile),hl	;0318	22 73 dd	" s .
	push af			;031b	f5		.
	pop hl			;031c	e1		.
	ld (0dd7fh),hl		;031d	22 7f dd	" . .
	ret			;0320	c9		.
	call sub_032ah		;0321	cd 2a 03	. * .
	rst 30h			;0324	f7		.
	dec c			;0325	0d		.
	inc e			;0326	1c		.
	dec b			;0327	05		.
	jr l0310h		;0328	18 e6		. .
sub_032ah:
	ld hl,(RegisterFile)	;032a	2a 73 dd	* s .
	ld de,(0dd75h)		;032d	ed 5b 75 dd	. [ u .
	ld bc,(0dd77h)		;0331	ed 4b 77 dd	. K w .
	ld a,(0dd80h)		;0335	3a 80 dd	: . .
	ret			;0338	c9		.
	ld de,l0357h		;0339	11 57 03	. W .
	ld hl,(0dd75h)		;033c	2a 75 dd	* u .
	add hl,de		;033f	19		.
	ld e,(hl)		;0340	5e		^
	inc hl			;0341	23		#
	ld d,(hl)		;0342	56		V
	ld hl,(0dd77h)		;0343	2a 77 dd	* w .
	add hl,de		;0346	19		.
	ld a,(0dd7fh)		;0347	3a 7f dd	: . .
	rrca			;034a	0f		.
	jr c,l0352h		;034b	38 05		8 .
	ld a,(hl)		;034d	7e		~
	ld (0dd80h),a		;034e	32 80 dd	2 . .
	ret			;0351	c9		.
l0352h:
	ld a,(0dd80h)		;0352	3a 80 dd	: . .
	ld (hl),a		;0355	77		w
	ret			;0356	c9		.
l0357h:
	inc d			;0357	14		.
	ret po			;0358	e0		.
	ld c,(hl)		;0359	4e		N
	ret po			;035a	e0		.
	ret nz			;035b	c0		.
	rst 38h			;035c	ff		.
	defb 0edh ;next byte illegal after ed	;035d	ed		.
	jp pe,l0fcdh		;035e	ea cd 0f	. . .
	jr nc,l039dh		;0361	30 3a		0 :
	add a,b			;0363	80		.
	defb 0ddh,0c3h,012h ;illegal sequence	;0364	dd c3 12	. . .
	dec hl			;0367	2b		+
	ld (0c063h),a		;0368	32 63 c0	2 c .
	res 7,(iy+014h)		;036b	fd cb 14 be	. . . .
	ret			;036f	c9		.
sub_0370h:
	push af			;0370	f5		.
	jr l0388h		;0371	18 15		. .
sub_0373h:
	push af			;0373	f5		.
	ld hl,0e9f5h		;0374	21 f5 e9	! . .
	ld (0e9f3h),hl		;0377	22 f3 e9	" . .
	ld hl,0ffdch		;037a	21 dc ff	! . .
	set 5,(hl)		;037d	cb ee		. .
	ld a,(0e9f1h)		;037f	3a f1 e9	: . .
	call sub_2c4dh		;0382	cd 4d 2c	. M ,
	call sub_0550h		;0385	cd 50 05	. P .
l0388h:
	ld a,(l00fbh)		;0388	3a fb 00	: . .
	ld hl,0e00dh		;038b	21 0d e0	! . .
	cp (hl)			;038e	be		.
	call nz,KeyClick	;038f	c4 a8 2b	. . +
	pop af			;0392	f1		.
	pop hl			;0393	e1		.
	ld sp,MonitorStack	;0394	31 36 e3	1 6 .
	jp (hl)			;0397	e9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AY-3-8910 helpers: register select on #FFFD, data on #BFFD.
; AyReadRegister (here $0398) gates on IY+$14 bit 0, selects
; register 7, saves the port value at $E004 and leaves $FF in
; the register; AyReadData ($03B6) returns any register's data
; byte.  The wrapper at $03C2 (AyWriteData) writes the saved
; $E004 value back into register 7 - the read-modify-restore
; pair behind the mouse buttons and hardware probes.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AyReadRegister:
	bit 0,(iy+014h)		;0398	fd cb 14 46	. . . F
	ret nz			;039c	c0		.
l039dh:
	ld b,007h		;039d	06 07		. .
	call AyReadData		;039f	cd b6 03	. . .
	ld (0e004h),a		;03a2	32 04 e0	2 . .
	ld a,0ffh		;03a5	3e ff		> .
l03a7h:
	push bc			;03a7	c5		.
	push af			;03a8	f5		.
	ld a,b			;03a9	78		x
	ld bc,0fffdh		;03aa	01 fd ff	. . .
	out (c),a		;03ad	ed 79		. y
	pop af			;03af	f1		.
	ld b,0bfh		;03b0	06 bf		. .
	out (c),a		;03b2	ed 79		. y
	pop bc			;03b4	c1		.
	ret			;03b5	c9		.
AyReadData:
	push bc			;03b6	c5		.
	ld a,b			;03b7	78		x
	ld bc,0fffdh		;03b8	01 fd ff	. . .
	out (c),a		;03bb	ed 79		. y
	nop			;03bd	00		.
	in a,(c)		;03be	ed 78		. x
	pop bc			;03c0	c1		.
	ret			;03c1	c9		.
AyWriteData:
	bit 0,(iy+014h)		;03c2	fd cb 14 46	. . . F
	ret nz			;03c6	c0		.
	ld b,007h		;03c7	06 07		. .
	ld a,(0e004h)		;03c9	3a 04 e0	: . .
	jr l03a7h		;03cc	18 d9		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SaveContext - snapshot the interrupted CPU.  AF, AF' (ex af
; twice), then BC/DE/HL of both sets, IY, IX are pushed onto the
; context stack below $DD83; IY <- IyWorkBase (monitor flags).
; The user PC is recovered from the stack, user SP derived (+11
; stacked words), registers copied to RegisterFile and the backup
; at RegisterBackup, I and IM captured (IM decoded from the
; stacked I byte), paging mirror updated, monitor mapping
; ($12/$10) re-asserted.  v2.95 and v4.01 (this ROM) additionally beep here.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SaveContext:
	ld bc,l1ffdh		;03ce	01 fd 1f	. . .
	ld (0dda7h),sp		;03d1	ed 73 a7 dd	. s . .
	ld sp,UserIff		;03d5	31 83 dd	1 . .
	ex af,af'		;03d8	08		.
	push af			;03d9	f5		.
	ex af,af'		;03da	08		.
	push af			;03db	f5		.
	exx			;03dc	d9		.
	push bc			;03dd	c5		.
	push de			;03de	d5		.
	push hl			;03df	e5		.
	exx			;03e0	d9		.
	push bc			;03e1	c5		.
	push de			;03e2	d5		.
	push hl			;03e3	e5		.
	push iy			;03e4	fd e5		. .
	push ix			;03e6	dd e5		. .
	ld iy,IyWorkBase	;03e8	fd 21 14 e0	. ! . .
	ld hl,(UserSp)		;03ec	2a 6d dd	* m .
	ld de,l0200h		;03ef	11 00 02	. . .
l03f2h:
	ld bc,07ffdh		;03f2	01 fd 7f	. . .
	out (c),e		;03f5	ed 59		. Y
	ld b,01fh		;03f7	06 1f		. .
	out (c),d		;03f9	ed 51		. Q
	ld a,(RamMagic1)	;03fb	3a 01 c0	: . .
	cp 055h			;03fe	fe 55		. U
l0400h:
	jr z,l0412h		;0400	28 10		( .
l0402h:
	inc e			;0402	1c		.
	bit 3,e			;0403	cb 5b		. [
	jr z,l03f2h		;0405	28 eb		( .
	ld e,000h		;0407	1e 00		. .
	bit 4,d			;0409	cb 62		. b
	jp nz,l0589h		;040b	c2 89 05	. . .
	ld d,012h		;040e	16 12		. .
	jr l03f2h		;0410	18 e0		. .
l0412h:
	ld a,(RamMagic2)	;0412	3a 02 c0	: . .
	cp 0aah			;0415	fe aa		. .
	jr nz,l0402h		;0417	20 e9		  .
	ld sp,hl		;0419	f9		.
	pop hl			;041a	e1		.
	ld (RamMagic1),hl	;041b	22 01 c0	" . .
	ld b,01fh		;041e	06 1f		. .
	ld a,012h		;0420	3e 12		> .
	out (c),a		;0422	ed 79		. y
	ld b,07fh		;0424	06 7f		. .
	ld a,010h		;0426	3e 10		> .
	out (c),a		;0428	ed 79		. y
	res 1,d			;042a	cb 8a		. .
	ld (PagingState),de	;042c	ed 53 12 e0	. S . .
	ld a,015h		;0430	3e 15		> .
	out (c),a		;0432	ed 79		. y
	ld hl,(RamMagic1)	;0434	2a 01 c0	* . .
	ld (RamMagic1),a	;0437	32 01 c0	2 . .
	ld a,010h		;043a	3e 10		> .
	out (c),a		;043c	ed 79		. y
	ld (RamMagic1),a	;043e	32 01 c0	2 . .
	ld a,015h		;0441	3e 15		> .
	out (c),a		;0443	ed 79		. y
	ld a,(RamMagic1)	;0445	3a 01 c0	: . .
	ld (RamMagic1),hl	;0448	22 01 c0	" . .
	cp 015h			;044b	fe 15		. .
	jr z,l0453h		;044d	28 04		( .
	ld a,e			;044f	7b		{
	or 030h			;0450	f6 30		. 0
	ld e,a			;0452	5f		_
l0453h:
	ld a,010h		;0453	3e 10		> .
	out (c),a		;0455	ed 79		. y
	ld sp,(0dda7h)		;0457	ed 7b a7 dd	. { . .
	ld a,(05b5ch)		;045b	3a 5c 5b	: \ [
	and 008h		;045e	e6 08		. .
	bit 5,e			;0460	cb 6b		. k
	jr nz,l0466h		;0462	20 02		  .
	or e			;0464	b3		.
	ld e,a			;0465	5f		_
l0466h:
	ld a,(PagingState)	;0466	3a 12 e0	: . .
	or e			;0469	b3		.
	ld (PagingState),a	;046a	32 12 e0	2 . .
	ld hl,(UserSp)		;046d	2a 6d dd	* m .
	push hl			;0470	e5		.
	ld bc,l000bh		;0471	01 0b 00	. . .
	add hl,bc		;0474	09		.
	ld (UserSp),hl		;0475	22 6d dd	" m .
	pop hl			;0478	e1		.
	ld de,RegisterBackup	;0479	11 99 dd	. . .
	call CopyAcrossBanks	;047c	cd 53 07	. S .
	ld hl,(0dda2h)		;047f	2a a2 dd	* . .
	ld (UserPc),hl		;0482	22 6b dd	" k .
	ld hl,(0dda0h)		;0485	2a a0 dd	* . .
	ld (0dd7fh),hl		;0488	22 7f dd	" . .
	ld hl,(0dd9eh)		;048b	2a 9e dd	* . .
	ld a,h			;048e	7c		|
	sub 004h		;048f	d6 04		. .
	rlca			;0491	07		.
	sla h			;0492	cb 24		. $
	rra			;0494	1f		.
	ld h,a			;0495	67		g
	ld (UserIff),hl		;0496	22 83 dd	" . .
	ld hl,(0dd9bh)		;0499	2a 9b dd	* . .
	ld (0dd77h),hl		;049c	22 77 dd	" w .
	ld a,i			;049f	ed 57		. W
	ld (UserI),a		;04a1	32 85 dd	2 . .
	xor a			;04a4	af		.
	ld i,a			;04a5	ed 47		. G
	ld a,(0dd9dh)		;04a7	3a 9d dd	: . .
	ld (UserIm),a		;04aa	32 86 dd	2 . .
	and 006h		;04ad	e6 06		. .
	ld a,(PagingState)	;04af	3a 12 e0	: . .
	jr z,l04b6h		;04b2	28 02		( .
	or 010h			;04b4	f6 10		. .
l04b6h:
	ld (PagingState),a	;04b6	32 12 e0	2 . .
	ld hl,StepFlags		;04b9	21 26 e0	! & .
	bit 7,(hl)		;04bc	cb 7e		. ~
	set 7,(hl)		;04be	cb fe		. .
	jr z,l04cdh		;04c0	28 0b		( .
	ld b,000h		;04c2	06 00		. .
l04c4h:
	ld a,006h		;04c4	3e 06		> .
	out (0feh),a		;04c6	d3 fe		. .
	xor a			;04c8	af		.
	out (0feh),a		;04c9	d3 fe		. .
	djnz l04c4h		;04cb	10 f7		. .
l04cdh:
	ret			;04cd	c9		.
sub_04ceh:
	ld a,(0e02dh)		;04ce	3a 2d e0	: - .
	bit 6,a			;04d1	cb 77		. w
	jr z,ReadPort1FFD	;04d3	28 04		( .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Read the paging ports back: Scorpion #7FFD / #1FFD return their
; latched value (B=$7F / B=$1F with C=$FD).  Used by NMI/magic
; detection and by the step engine.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ReadPort7FFD:
	ld b,07fh		;04d5	06 7f		. .
	jr l04dbh		;04d7	18 02		. .
ReadPort1FFD:
	ld b,01fh		;04d9	06 1f		. .
l04dbh:
	ld c,0fdh		;04db	0e fd		. .
	in a,(c)		;04dd	ed 78		. x
	in a,(c)		;04df	ed 78		. x
	ret			;04e1	c9		.
sub_04e2h:
	ld hl,(0dd75h)		;04e2	2a 75 dd	* u .
	ld (RegisterBackup),hl	;04e5	22 99 dd	" . .
	ld hl,(0dd77h)		;04e8	2a 77 dd	* w .
	ld (0dd9bh),hl		;04eb	22 9b dd	" . .
	ld a,(UserI)		;04ee	3a 85 dd	: . .
	ld i,a			;04f1	ed 47		. G
	ld (0dd9dh),a		;04f3	32 9d dd	2 . .
	ld hl,(UserIff)		;04f6	2a 83 dd	* . .
	ld a,h			;04f9	7c		|
	sub 006h		;04fa	d6 06		. .
	rlca			;04fc	07		.
	sla h			;04fd	cb 24		. $
	rra			;04ff	1f		.
	ld h,a			;0500	67		g
	ld (0dd9eh),hl		;0501	22 9e dd	" . .
	ld hl,(0dd7fh)		;0504	2a 7f dd	* . .
	ld (0dda0h),hl		;0507	22 a0 dd	" . .
	ld hl,(UserPc)		;050a	2a 6b dd	* k .
	ld (0dda2h),hl		;050d	22 a2 dd	" . .
	ld hl,(UserSp)		;0510	2a 6d dd	* m .
	ld bc,0fff5h		;0513	01 f5 ff	. . .
	add hl,bc		;0516	09		.
	ld (UserSp),hl		;0517	22 6d dd	" m .
	ld de,RegisterBackup	;051a	11 99 dd	. . .
	ex de,hl		;051d	eb		.
	ld bc,l000bh		;051e	01 0b 00	. . .
	call sub_0749h		;0521	cd 49 07	. I .
	ld (0dda7h),sp		;0524	ed 73 a7 dd	. s . .
	ld sp,0dd6fh		;0528	31 6f dd	1 o .
	pop ix			;052b	dd e1		. .
	pop iy			;052d	fd e1		. .
	pop hl			;052f	e1		.
	pop de			;0530	d1		.
	pop bc			;0531	c1		.
	exx			;0532	d9		.
	pop hl			;0533	e1		.
	pop de			;0534	d1		.
	pop bc			;0535	c1		.
	exx			;0536	d9		.
	pop af			;0537	f1		.
	ex af,af'		;0538	08		.
	pop af			;0539	f1		.
	ex af,af'		;053a	08		.
	ld sp,(0dda7h)		;053b	ed 7b a7 dd	. { . .
	call sub_04ceh		;053f	cd ce 04	. . .
	ld de,(PagingState)	;0542	ed 5b 12 e0	. [ . .
	ld a,e			;0546	7b		{
	ld b,07fh		;0547	06 7f		. .
	and 010h		;0549	e6 10		. .
	out (c),a		;054b	ed 79		. y
	ld b,01fh		;054d	06 1f		. .
	ret			;054f	c9		.
sub_0550h:
	bit 3,(iy+012h)		;0550	fd cb 12 5e	. . . ^
	ret nz			;0554	c0		.
	jr SwapScreenBanks	;0555	18 04		. .
l0557h:
	ld sp,(0e38dh)		;0557	ed 7b 8d e3	. { . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SwapScreenBanks - exchange the user screen at $4000 with the
; private monitor screen at MonitorScreen ($C069, $1B00 bytes) via
; LDIR under #7FFD bit-4 toggling; direction chosen by carry and
; the shadow-screen flag (IY+12 bit 3).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SwapScreenBanks:
	ld hl,StepFlags		;055b	21 26 e0	! & .
	bit 1,(hl)		;055e	cb 4e		. N
	ret nz			;0560	c0		.
	set 3,(hl)		;0561	cb de		. .
	or a			;0563	b7		.
l0564h:
	ld bc,07ffdh		;0564	01 fd 7f	. . .
	ld a,010h		;0567	3e 10		> .
	out (c),a		;0569	ed 79		. y
	ld hl,EncodedTail_end	;056b	21 00 40	! . @
	ld de,MonitorScreen	;056e	11 69 c0	. i .
	jr nc,l0574h		;0571	30 01		0 .
	ex de,hl		;0573	eb		.
l0574h:
	ld bc,01b00h		;0574	01 00 1b	. . .
	ldir			;0577	ed b0		. .
	ld bc,07ffdh		;0579	01 fd 7f	. . .
	ld a,010h		;057c	3e 10		> .
	out (c),a		;057e	ed 79		. y
	ret			;0580	c9		.
sub_0581h:
	bit 1,(iy+012h)		;0581	fd cb 12 4e	. . . N
	ret nz			;0585	c0		.
	scf			;0586	37		7
	jr l0564h		;0587	18 db		. .
l0589h:
	ld a,004h		;0589	3e 04		> .
	out (0feh),a		;058b	d3 fe		. .
	halt			;058d	76		v
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
	jr nz,l05cdh		;058e	20 3d		  =
	ex af,af'		;0590	08		.
	push bc			;0591	c5		.
	push de			;0592	d5		.
	bit 6,h			;0593	cb 74		. t
	jr nz,l05a5h		;0595	20 0e		  .
	ld de,(PagingState)	;0597	ed 5b 12 e0	. [ . .
	ld bc,07ffdh		;059b	01 fd 7f	. . .
	jp 0e4cfh		;059e	c3 cf e4	. . .
l05a1h:
	pop de			;05a1	d1		.
	pop bc			;05a2	c1		.
	ex af,af'		;05a3	08		.
	ret			;05a4	c9		.
l05a5h:
	ld de,05b00h		;05a5	11 00 5b	. . [
	or a			;05a8	b7		.
	sbc hl,de		;05a9	ed 52		. R
	add hl,de		;05ab	19		.
	jr nc,l05fbh		;05ac	30 4d		0 M
	bit 3,(iy+012h)		;05ae	fd cb 12 5e	. . . ^
	jr z,l05fbh		;05b2	28 47		( G
	ld de,08069h		;05b4	11 69 80	. i .
	add hl,de		;05b7	19		.
	ld bc,07ffdh		;05b8	01 fd 7f	. . .
	ld a,010h		;05bb	3e 10		> .
	out (c),a		;05bd	ed 79		. y
	ex af,af'		;05bf	08		.
	jr c,l05c3h		;05c0	38 01		8 .
	ld a,(hl)		;05c2	7e		~
l05c3h:
	ld (hl),a		;05c3	77		w
	ex af,af'		;05c4	08		.
	and 010h		;05c5	e6 10		. .
	out (c),a		;05c7	ed 79		. y
	sbc hl,de		;05c9	ed 52		. R
	jr l05a1h		;05cb	18 d4		. .
l05cdh:
	bit 6,h			;05cd	cb 74		. t
	jr z,l05feh		;05cf	28 2d		( -
	ex af,af'		;05d1	08		.
	push bc			;05d2	c5		.
	push de			;05d3	d5		.
	ld de,(PagingState)	;05d4	ed 5b 12 e0	. [ . .
	ld bc,07ffdh		;05d8	01 fd 7f	. . .
	ld a,e			;05db	7b		{
	and 017h		;05dc	e6 17		. .
	out (c),a		;05de	ed 79		. y
	ld a,d			;05e0	7a		z
	and 010h		;05e1	e6 10		. .
	or 002h			;05e3	f6 02		. .
	ld b,01fh		;05e5	06 1f		. .
	out (c),a		;05e7	ed 79		. y
	ex af,af'		;05e9	08		.
	jr c,l05edh		;05ea	38 01		8 .
	ld a,(hl)		;05ec	7e		~
l05edh:
	ld (hl),a		;05ed	77		w
	ex af,af'		;05ee	08		.
	ld a,012h		;05ef	3e 12		> .
	out (c),a		;05f1	ed 79		. y
	ld b,07fh		;05f3	06 7f		. .
	ld a,010h		;05f5	3e 10		> .
	out (c),a		;05f7	ed 79		. y
	jr l05a1h		;05f9	18 a6		. .
l05fbh:
	pop de			;05fb	d1		.
	pop bc			;05fc	c1		.
	ex af,af'		;05fd	08		.
l05feh:
	jr c,l0601h		;05fe	38 01		8 .
	ld a,(hl)		;0600	7e		~
l0601h:
	ld (hl),a		;0601	77		w
	ret			;0602	c9		.
	scf			;0603	37		7
	jr l0607h		;0604	18 01		. .
	or a			;0606	b7		.
l0607h:
	ex af,af'		;0607	08		.
	push de			;0608	d5		.
	ld l,005h		;0609	2e 05		. .
	call sub_3707h		;060b	cd 07 37	. . 7
	jr z,l0644h		;060e	28 34		( 4
	ld de,(PagingState)	;0610	ed 5b 12 e0	. [ . .
	ld bc,07ffdh		;0614	01 fd 7f	. . .
	ld a,e			;0617	7b		{
	and 017h		;0618	e6 17		. .
	out (c),a		;061a	ed 79		. y
	ld a,d			;061c	7a		z
	and 010h		;061d	e6 10		. .
	or 002h			;061f	f6 02		. .
	ld b,01fh		;0621	06 1f		. .
	out (c),a		;0623	ed 79		. y
	ld hl,0c000h		;0625	21 00 c0	! . .
	ld de,08000h		;0628	11 00 80	. . .
	ld bc,EncodedTail_end	;062b	01 00 40	. . @
	ex af,af'		;062e	08		.
	jr nc,l0632h		;062f	30 01		0 .
	ex de,hl		;0631	eb		.
l0632h:
	ex af,af'		;0632	08		.
	ldir			;0633	ed b0		. .
	ld bc,l1ffdh		;0635	01 fd 1f	. . .
	ld a,012h		;0638	3e 12		> .
	out (c),a		;063a	ed 79		. y
	ld b,07fh		;063c	06 7f		. .
	ld a,010h		;063e	3e 10		> .
	out (c),a		;0640	ed 79		. y
	pop de			;0642	d1		.
	ret			;0643	c9		.
l0644h:
	ld hl,EncodedTail_end	;0644	21 00 40	! . @
	ld de,08000h		;0647	11 00 80	. . .
	ld c,l			;064a	4d		M
	ld b,h			;064b	44		D
	ex af,af'		;064c	08		.
	jr nc,l0650h		;064d	30 01		0 .
	ex de,hl		;064f	eb		.
l0650h:
	ex af,af'		;0650	08		.
	ldir			;0651	ed b0		. .
	ld hl,MonitorScreen	;0653	21 69 c0	! i .
	ld de,08000h		;0656	11 00 80	. . .
	ld bc,01b00h		;0659	01 00 1b	. . .
	ex af,af'		;065c	08		.
	jr nc,l0660h		;065d	30 01		0 .
	ex de,hl		;065f	eb		.
l0660h:
	ex af,af'		;0660	08		.
	ldir			;0661	ed b0		. .
	pop de			;0663	d1		.
	ret			;0664	c9		.
	ld bc,07ffdh		;0665	01 fd 7f	. . .
	ld a,030h		;0668	3e 30		> 0
	out (c),a		;066a	ed 79		. y
	ret			;066c	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FatalHalt: border flash and HALT forever - end of a failed
; hardware inventory (bad RAM under the monitor).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FatalHalt:
	ld a,004h		;066d	3e 04		> .
	out (0feh),a		;066f	d3 fe		. .
	halt			;0671	76		v
sub_0672h:
	ld iy,IyWorkBase	;0672	fd 21 14 e0	. ! . .
	call ReadPort7FFD	;0676	cd d5 04	. . .
	ld bc,l1ffdh		;0679	01 fd 1f	. . .
	ld a,012h		;067c	3e 12		> .
	out (c),a		;067e	ed 79		. y
	ld a,018h		;0680	3e 18		> .
	jr l0691h		;0682	18 0d		. .
l0684h:
	ld hl,0c000h		;0684	21 00 c0	! . .
	ld (hl),000h		;0687	36 00		6 .
	ld de,RamMagic1		;0689	11 01 c0	. . .
	ld bc,l3fffh		;068c	01 ff 3f	. . ?
	ldir			;068f	ed b0		. .
l0691h:
	dec a			;0691	3d		=
	ld bc,07ffdh		;0692	01 fd 7f	. . .
	out (c),a		;0695	ed 79		. y
	cp 010h			;0697	fe 10		. .
	jr nz,l0684h		;0699	20 e9		  .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BootContinue - boot bookkeeping at $C000..$C006: the signature
; must read byte i = i (walked down from $C006).  Broken -> RAM
; re-initialised and control loops back to the cold path; intact
; -> pass counter bumped.  the v2.95 sister adds a ROM checksum walk here.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BootContinue:
	xor a			;069b	af		.
	ld hl,MonitorScreen	;069c	21 69 c0	! i .
	push hl			;069f	e5		.
	ld (hl),a		;06a0	77		w
	ld de,0c06ah		;06a1	11 6a c0	. j .
	ld bc,l3f96h		;06a4	01 96 3f	. . ?
	ldir			;06a7	ed b0		. .
	pop hl			;06a9	e1		.
l06aah:
	cp (hl)			;06aa	be		.
	inc hl			;06ab	23		#
	jr nz,FatalHalt		;06ac	20 bf		  .
	cp h			;06ae	bc		.
	jr nz,l06aah		;06af	20 f9		  .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DecodeTables: decrypt the XOR-masked ROM tail into RAM with the
; keys at XorDecodeKeys ($00FC), via DecodeXorTable ($3837):
; first $3ED2 -> $EAED here, then the helper at $0739 does
; $3B92 -> $E3BE.  IX is loaded through the pointer cell at
; $F508 and control passes to it (jp (IX)) - the RAM-side boot
; configuration continuation.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DecodeTables:
	ld hl,l3ed2h		;06b1	21 d2 3e	! . >
	ld de,0eaedh		;06b4	11 ed ea	. . .
	call DecodeXorTable	;06b7	cd 37 38	. 7 8
	ld ix,(BootConfigTable)	;06ba	dd 2a 08 f5	. * . .
	call sub_0739h		;06be	cd 39 07	. 9 .
	ld bc,XorKeys_end	;06c1	01 00 01	. . .
	ldir			;06c4	ed b0		. .
	rst 30h			;06c6	f7		.
	rst 38h			;06c7	ff		.
	inc c			;06c8	0c		.
	rlca			;06c9	07		.
	rst 30h			;06ca	f7		.
	ld a,a			;06cb	7f		.
	dec b			;06cc	05		.
	inc b			;06cd	04		.
	rst 30h			;06ce	f7		.
	ld (l0400h+1),hl	;06cf	22 01 04	" . .
	call sub_3b7fh		;06d2	cd 7f 3b	. . ;
	ld (iy+008h),030h	;06d5	fd 36 08 30	. 6 . 0
	ld hl,0e17dh		;06d9	21 7d e1	! } .
	res 7,(hl)		;06dc	cb be		. .
	inc hl			;06de	23		#
	ld (0e3bch),hl		;06df	22 bc e3	" . .
	set 6,(iy+009h)		;06e2	fd cb 09 f6	. . . .
	ld c,000h		;06e6	0e 00		. .
	call sub_0732h		;06e8	cd 32 07	. 2 .
	rst 20h			;06eb	e7		.
	adc a,02eh		;06ec	ce 2e		. .
	call p,07e61h		;06ee	f4 61 7e	. a ~
	defb 0edh ;next byte illegal after ed	;06f1	ed		.
	ld l,h			;06f2	6c		l
	inc hl			;06f3	23		#
	ld h,(hl)		;06f4	66		f
	ld l,a			;06f5	6f		o
	call PrintHexWord	;06f6	cd de 16	. . .
	ld c,001h		;06f9	0e 01		. .
	call sub_0732h		;06fb	cd 32 07	. 2 .
	ld e,0fch		;06fe	1e fc		. .
	ld d,c			;0700	51		Q
	dec d			;0701	15		.
	ld a,(de)		;0702	1a		.
	inc de			;0703	13		.
	push de			;0704	d5		.
	ld b,002h		;0705	06 02		. .
	call PrintDecimal	;0707	cd 58 16	. X .
	rst 20h			;070a	e7		.
	xor l			;070b	ad		.
	pop de			;070c	d1		.
	ld a,(de)		;070d	1a		.
	ld b,002h		;070e	06 02		. .
	call PrintDecimal	;0710	cd 58 16	. X .
	ld c,002h		;0713	0e 02		. .
	call sub_0732h		;0715	cd 32 07	. 2 .
	rst 20h			;0718	e7		.
	sub 0e7h		;0719	d6 e7		. .
	inc (hl)		;071b	34		4
	xor (hl)		;071c	ae		.
	rst 20h			;071d	e7		.
	jr nc,$-77		;071e	30 b1		0 .
	ld hl,(0e3bch)		;0720	2a bc e3	* . .
	dec hl			;0723	2b		+
	set 7,(hl)		;0724	cb fe		. .
	res 6,(iy+009h)		;0726	fd cb 09 b6	. . . .
	call sub_1f25h		;072a	cd 25 1f	. % .
	ld hl,l3135h		;072d	21 35 31	! 5 1
	ex (sp),hl		;0730	e3		.
	jp (hl)			;0731	e9		.
sub_0732h:
	rst 20h			;0732	e7		.
	sbc a,e			;0733	9b		.
	ld a,c			;0734	79		y
	rst 10h			;0735	d7		.
	rst 20h			;0736	e7		.
	and h			;0737	a4		.
	ret			;0738	c9		.
sub_0739h:
	ld hl,l3b92h		;0739	21 92 3b	! . ;
	ld de,DecodedTables	;073c	11 be e3	. . .
	call DecodeXorTable	;073f	cd 37 38	. 7 8
	jp (ix)			;0742	dd e9		. .
	ld l,a			;0744	6f		o
	call p,0ebc9h		;0745	f4 c9 eb	. . .
	ret			;0748	c9		.
sub_0749h:
	scf			;0749	37		7
	ex de,hl		;074a	eb		.
	exx			;074b	d9		.
	ld hl,00747h		;074c	21 47 07	! G .
	push hl			;074f	e5		.
	exx			;0750	d9		.
	jr l0754h		;0751	18 01		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CopyAcrossBanks - LDIR with either side in a foreign bank:
; bytes travel one at a time through the bank window (PeekPoke-
; AnyBank style access); $4000-area sides go via the monitor
; screen buffer when the shadow-screen flag is set.  Used by all
; windows that display foreign memory (dumps, disassembly, sums).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CopyAcrossBanks:
	or a			;0753	b7		.
l0754h:
	ex af,af'		;0754	08		.
	bit 7,h			;0755	cb 7c		. |
	jr nz,l079dh		;0757	20 44		  D
	bit 6,h			;0759	cb 74		. t
	jr nz,l0775h		;075b	20 18		  .
	push hl			;075d	e5		.
	add hl,bc		;075e	09		.
	dec hl			;075f	2b		+
	bit 6,h			;0760	cb 74		. t
	pop hl			;0762	e1		.
	jp z,l07f1h		;0763	ca f1 07	. . .
	push bc			;0766	c5		.
	ld bc,EncodedTail_end	;0767	01 00 40	. . @
	ex de,hl		;076a	eb		.
	ex (sp),hl		;076b	e3		.
	call sub_07e0h		;076c	cd e0 07	. . .
	ex (sp),hl		;076f	e3		.
	ex de,hl		;0770	eb		.
	call l07f1h		;0771	cd f1 07	. . .
	pop bc			;0774	c1		.
l0775h:
	push hl			;0775	e5		.
	call sub_07d7h		;0776	cd d7 07	. . .
	pop hl			;0779	e1		.
	jr nc,l07a1h		;077a	30 25		0 %
	bit 3,(iy+012h)		;077c	fd cb 12 5e	. . . ^
	jr z,l07a1h		;0780	28 1f		( .
	push hl			;0782	e5		.
	add hl,bc		;0783	09		.
	dec hl			;0784	2b		+
	call sub_07d7h		;0785	cd d7 07	. . .
	pop hl			;0788	e1		.
	jp c,l0807h		;0789	da 07 08	. . .
	push bc			;078c	c5		.
	ld bc,05b00h		;078d	01 00 5b	. . [
	ex de,hl		;0790	eb		.
	ex (sp),hl		;0791	e3		.
	call sub_07e0h		;0792	cd e0 07	. . .
	ex (sp),hl		;0795	e3		.
	ex de,hl		;0796	eb		.
	call l0807h		;0797	cd 07 08	. . .
	pop bc			;079a	c1		.
	jr l07a1h		;079b	18 04		. .
l079dh:
	bit 6,h			;079d	cb 74		. t
	jr nz,l07bch		;079f	20 1b		  .
l07a1h:
	push hl			;07a1	e5		.
	add hl,bc		;07a2	09		.
	dec hl			;07a3	2b		+
	ld a,h			;07a4	7c		|
	and 0c0h		;07a5	e6 c0		. .
	cp 0c0h			;07a7	fe c0		. .
	pop hl			;07a9	e1		.
	jp nz,l07fch		;07aa	c2 fc 07	. . .
	push bc			;07ad	c5		.
	ld bc,0c000h		;07ae	01 00 c0	. . .
	ex de,hl		;07b1	eb		.
	ex (sp),hl		;07b2	e3		.
	call sub_07e0h		;07b3	cd e0 07	. . .
	ex (sp),hl		;07b6	e3		.
	ex de,hl		;07b7	eb		.
	call l07fch		;07b8	cd fc 07	. . .
	pop bc			;07bb	c1		.
l07bch:
	push hl			;07bc	e5		.
	add hl,bc		;07bd	09		.
	dec hl			;07be	2b		+
	bit 6,h			;07bf	cb 74		. t
	pop hl			;07c1	e1		.
	jp nz,l0821h		;07c2	c2 21 08	. ! .
	push bc			;07c5	c5		.
	ld bc,WriteAnyBankByte	;07c6	01 00 00	. . .
	ex de,hl		;07c9	eb		.
	ex (sp),hl		;07ca	e3		.
	call sub_07e0h		;07cb	cd e0 07	. . .
	ex (sp),hl		;07ce	e3		.
	ex de,hl		;07cf	eb		.
	call l0821h		;07d0	cd 21 08	. ! .
	pop bc			;07d3	c1		.
	jp l07f1h		;07d4	c3 f1 07	. . .
sub_07d7h:
	push de			;07d7	d5		.
	ld de,05b00h		;07d8	11 00 5b	. . [
	or a			;07db	b7		.
	sbc hl,de		;07dc	ed 52		. R
	pop de			;07de	d1		.
	ret			;07df	c9		.
sub_07e0h:
	ex de,hl		;07e0	eb		.
	or a			;07e1	b7		.
	push hl			;07e2	e5		.
	ld l,c			;07e3	69		i
	ld h,b			;07e4	60		`
	pop bc			;07e5	c1		.
	sbc hl,bc		;07e6	ed 42		. B
	push bc			;07e8	c5		.
	ld c,l			;07e9	4d		M
	ld b,h			;07ea	44		D
	pop hl			;07eb	e1		.
	ex de,hl		;07ec	eb		.
	or a			;07ed	b7		.
	sbc hl,bc		;07ee	ed 42		. B
	ret			;07f0	c9		.
l07f1h:
	exx			;07f1	d9		.
	ld de,(PagingState)	;07f2	ed 5b 12 e0	. [ . .
	ld bc,07ffdh		;07f6	01 fd 7f	. . .
	jp 0e4f0h		;07f9	c3 f0 e4	. . .
l07fch:
	ex af,af'		;07fc	08		.
	jr nc,l0800h		;07fd	30 01		0 .
	ex de,hl		;07ff	eb		.
l0800h:
	ldir			;0800	ed b0		. .
	jr nc,l0805h		;0802	30 01		0 .
	ex de,hl		;0804	eb		.
l0805h:
	ex af,af'		;0805	08		.
	ret			;0806	c9		.
l0807h:
	push bc			;0807	c5		.
	ld bc,08069h		;0808	01 69 80	. i .
	add hl,bc		;080b	09		.
	pop bc			;080c	c1		.
	push hl			;080d	e5		.
	ld hl,l081ah		;080e	21 1a 08	! . .
	ex (sp),hl		;0811	e3		.
	push iy			;0812	fd e5		. .
	ld iy,01010h		;0814	fd 21 10 10	. ! . .
	jr l0827h		;0818	18 0d		. .
l081ah:
	push bc			;081a	c5		.
	ld bc,07f97h		;081b	01 97 7f	. . .
	add hl,bc		;081e	09		.
	pop bc			;081f	c1		.
	ret			;0820	c9		.
l0821h:
	push iy			;0821	fd e5		. .
	ld iy,(PagingState)	;0823	fd 2a 12 e0	. * . .
l0827h:
	push hl			;0827	e5		.
	ld hl,(08000h)		;0828	2a 00 80	* . .
	ex (sp),hl		;082b	e3		.
	push ix			;082c	dd e5		. .
	defb 0ddh,060h ;ld ixh,b	;082e	dd 60		. `
	defb 0ddh,069h ;ld ixl,c	;0830	dd 69		. i
	exx			;0832	d9		.
	ld bc,07ffdh		;0833	01 fd 7f	. . .
	ld a,010h		;0836	3e 10		> .
	out (c),a		;0838	ed 79		. y
	defb 0ddh,044h ;ld b,ixh	;083a	dd 44		. D
	defb 0ddh,04dh ;ld c,ixl	;083c	dd 4d		. M
	ld hl,08002h		;083e	21 02 80	! . .
	ld de,0db69h		;0841	11 69 db	. i .
	ldir			;0844	ed b0		. .
	ld hl,l1210h		;0846	21 10 12	! . .
	defb 0fdh,07dh ;ld a,iyl	;0849	fd 7d		. }
	and 017h		;084b	e6 17		. .
	ld e,a			;084d	5f		_
	defb 0fdh,07ch ;ld a,iyh	;084e	fd 7c		. |
	and 010h		;0850	e6 10		. .
	or 002h			;0852	f6 02		. .
	ld d,a			;0854	57		W
	ex af,af'		;0855	08		.
	jr nc,l0859h		;0856	30 01		0 .
	ex de,hl		;0858	eb		.
l0859h:
	ld bc,07ffdh		;0859	01 fd 7f	. . .
	out (c),e		;085c	ed 59		. Y
	ld b,01fh		;085e	06 1f		. .
	out (c),d		;0860	ed 51		. Q
	exx			;0862	d9		.
	jr nc,l0866h		;0863	30 01		0 .
	ex de,hl		;0865	eb		.
l0866h:
	ld (08000h),de		;0866	ed 53 00 80	. S . .
	ld de,08002h		;086a	11 02 80	. . .
	ldir			;086d	ed b0		. .
	ld de,(08000h)		;086f	ed 5b 00 80	. [ . .
	defb 0ddh,044h ;ld b,ixh	;0873	dd 44		. D
	defb 0ddh,04dh ;ld c,ixl	;0875	dd 4d		. M
	exx			;0877	d9		.
	out (c),h		;0878	ed 61		. a
	ld b,07fh		;087a	06 7f		. .
	out (c),l		;087c	ed 69		. i
	exx			;087e	d9		.
	ld (08000h),hl		;087f	22 00 80	" . .
	ld hl,08002h		;0882	21 02 80	! . .
	ldir			;0885	ed b0		. .
	ld hl,(08000h)		;0887	2a 00 80	* . .
	jr nc,l088dh		;088a	30 01		0 .
	ex de,hl		;088c	eb		.
l088dh:
	ex af,af'		;088d	08		.
	exx			;088e	d9		.
	ld a,010h		;088f	3e 10		> .
	out (c),a		;0891	ed 79		. y
	ld b,01fh		;0893	06 1f		. .
	ld a,012h		;0895	3e 12		> .
	out (c),a		;0897	ed 79		. y
	ld hl,0db69h		;0899	21 69 db	! i .
	ld de,08002h		;089c	11 02 80	. . .
	defb 0ddh,044h ;ld b,ixh	;089f	dd 44		. D
	defb 0ddh,04dh ;ld c,ixl	;08a1	dd 4d		. M
	ldir			;08a3	ed b0		. .
	ld bc,07ffdh		;08a5	01 fd 7f	. . .
	ld a,010h		;08a8	3e 10		> .
	out (c),a		;08aa	ed 79		. y
	exx			;08ac	d9		.
	pop ix			;08ad	dd e1		. .
	ex (sp),hl		;08af	e3		.
	ld (08000h),hl		;08b0	22 00 80	" . .
	pop hl			;08b3	e1		.
	pop iy			;08b4	fd e1		. .
	ret			;08b6	c9		.
l08b7h:
	ld de,0f409h		;08b7	11 09 f4	. . .
	ld (bc),a		;08ba	02		.
	add hl,sp		;08bb	39		9
	inc bc			;08bc	03		.
	ld e,e			;08bd	5b		[
	ld hl,(0035fh)		;08be	2a 5f 03	* _ .
	ld l,b			;08c1	68		h
	inc bc			;08c2	03		.
	out (008h),a		;08c3	d3 08		. .
	pop de			;08c5	d1		.
	ld (bc),a		;08c6	02		.
	rst 10h			;08c7	d7		.
	ld (bc),a		;08c8	02		.
	ld hl,0eb03h		;08c9	21 03 eb	! . .
	ld (bc),a		;08cc	02		.
	xor d			;08cd	aa		.
	ld (bc),a		;08ce	02		.
	call z,0ae02h		;08cf	cc 02 ae	. . .
	ld (bc),a		;08d2	02		.
	xor a			;08d3	af		.
	scf			;08d4	37		7
	jr l08d8h		;08d5	18 01		. .
	xor a			;08d7	af		.
l08d8h:
	ld hl,l08f7h		;08d8	21 f7 08	! . .
	ld (0de12h),hl		;08db	22 12 de	" . .
	ld hl,(WorkBufferPtr)	;08de	2a b7 e3	* . .
	push hl			;08e1	e5		.
	call sub_300ah		;08e2	cd 0a 30	. . 0
	push af			;08e5	f5		.
	ld (0dda7h),sp		;08e6	ed 73 a7 dd	. s . .
	set 4,(iy+009h)		;08ea	fd cb 09 e6	. . . .
	call nc,CallTrdos	;08ee	d4 21 3a	. ! :
	call sub_0a51h		;08f1	cd 51 0a	. Q .
	pop af			;08f4	f1		.
	jr l08feh		;08f5	18 07		. .
l08f7h:
	ld sp,(0dda7h)		;08f7	ed 7b a7 dd	. { . .
	pop af			;08fb	f1		.
	ld a,001h		;08fc	3e 01		> .
l08feh:
	ld hl,0e01dh		;08fe	21 1d e0	! . .
	res 4,(hl)		;0901	cb a6		. .
	res 5,(hl)		;0903	cb ae		. .
	pop ix			;0905	dd e1		. .
	ld (WorkBufferPtr),ix	;0907	dd 22 b7 e3	. " . .
	ret nc			;090b	d0		.
	or a			;090c	b7		.
	jr z,l0937h		;090d	28 28		( (
	jr l091eh		;090f	18 0d		. .
	ld hl,l091ah		;0911	21 1a 09	! . .
	ld (0dda7h),sp		;0914	ed 73 a7 dd	. s . .
	jr l0929h		;0918	18 0f		. .
l091ah:
	ld sp,(0dda7h)		;091a	ed 7b a7 dd	. { . .
l091eh:
	ld a,001h		;091e	3e 01		> .
	jr l0939h		;0920	18 17		. .
l0922h:
	ld hl,l0146h		;0922	21 46 01	! F .
	push hl			;0925	e5		.
	ld hl,l0941h		;0926	21 41 09	! A .
l0929h:
	ld (0de12h),hl		;0929	22 12 de	" . .
	call sub_300ah		;092c	cd 0a 30	. . 0
	set 4,(iy+009h)		;092f	fd cb 09 e6	. . . .
	ld a,(0dd80h)		;0933	3a 80 dd	: . .
	rst 10h			;0936	d7		.
l0937h:
	ld a,040h		;0937	3e 40		> @
l0939h:
	ld (0dd7fh),a		;0939	32 7f dd	2 . .
sub_093ch:
	res 4,(iy+009h)		;093c	fd cb 09 a6	. . . .
	ret			;0940	c9		.
l0941h:
	ld sp,MonitorStack	;0941	31 36 e3	1 6 .
	call sub_093ch		;0944	cd 3c 09	. < .
	ld hl,005e2h		;0947	21 e2 05	! . .
	ld (UserPc),hl		;094a	22 6b dd	" k .
	jp l0146h		;094d	c3 46 01	. F .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Tape output feeder: serialises bits to the tape port (OUT $FE
; border bit) with loop-timed pulses; drives SaveToTape.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FeedTapeOutput:
	bit 5,(iy+009h)		;0950	fd cb 09 6e	. . . n
	jr nz,MouseDriver	;0954	20 53		  S
	cp 00dh			;0956	fe 0d		. .
	jr nz,l0987h		;0958	20 2d		  -
	ld (ix+001h),000h	;095a	dd 36 01 00	. 6 . .
	call sub_0a05h		;095e	cd 05 0a	. . .
	bit 2,(ix+007h)		;0961	dd cb 07 56	. . . V
	jr z,l0970h		;0965	28 09		( .
	ld a,(0e055h)		;0967	3a 55 e0	: U .
	or a			;096a	b7		.
	jr z,l0970h		;096b	28 03		( .
	call MouseDriver	;096d	cd a9 09	. . .
l0970h:
	ld a,(ix+004h)		;0970	dd 7e 04	. ~ .
	or a			;0973	b7		.
	ret z			;0974	c8		.
	inc (ix+000h)		;0975	dd 34 00	. 4 .
	cp (ix+000h)		;0978	dd be 00	. . .
	ret nc			;097b	d0		.
	ld (ix+000h),000h	;097c	dd 36 00 00	. 6 . .
	ld a,(0e056h)		;0980	3a 56 e0	: V .
	or a			;0983	b7		.
	ret z			;0984	c8		.
	jr MouseDriver		;0985	18 22		. "
l0987h:
	cp 020h			;0987	fe 20		.  
	jr c,MouseDriver	;0989	38 1e		8 .
	cp 07fh			;098b	fe 7f		. .
	jr nz,l0991h		;098d	20 02		  .
	ld a,02eh		;098f	3e 2e		> .
l0991h:
	ld e,a			;0991	5f		_
	ld a,(ix+005h)		;0992	dd 7e 05	. ~ .
	or a			;0995	b7		.
	jr z,l09a8h		;0996	28 10		( .
	inc (ix+001h)		;0998	dd 34 01	. 4 .
	cp (ix+001h)		;099b	dd be 01	. . .
	jr nc,l09a8h		;099e	30 08		0 .
	push de			;09a0	d5		.
	rst 20h			;09a1	e7		.
	adc a,l			;09a2	8d		.
	pop de			;09a3	d1		.
	xor a			;09a4	af		.
	ld (ix+001h),a		;09a5	dd 77 01	. w .
l09a8h:
	ld a,e			;09a8	7b		{
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MouseDriver: AY-port mouse - buttons sampled via AY register 7
; inputs, motion from the PSG counters; state kept in RAM.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MouseDriver:
	ld d,(iy+015h)		;09a9	fd 56 15	. V .
	bit 7,d			;09ac	cb 7a		. z
	jr z,l09b1h		;09ae	28 01		( .
	cpl			;09b0	2f		/
l09b1h:
	bit 0,d			;09b1	cb 42		. B
	jr z,l0a10h		;09b3	28 5b		( [
	push af			;09b5	f5		.
	xor 0ffh		;09b6	ee ff		. .
	rlca			;09b8	07		.
	rlca			;09b9	07		.
	rlca			;09ba	07		.
	push af			;09bb	f5		.
	call ReadPort1FFD	;09bc	cd d9 04	. . .
	pop bc			;09bf	c1		.
	ld c,b			;09c0	48		H
l09c1h:
	call sub_0a33h		;09c1	cd 33 0a	. 3 .
	jr nc,l0a2fh		;09c4	30 69		0 i
	ld a,0ffh		;09c6	3e ff		> .
	in a,(0feh)		;09c8	db fe		. .
	bit 5,a			;09ca	cb 6f		. o
	jr z,l09c1h		;09cc	28 f3		( .
	ld b,008h		;09ce	06 08		. .
	bit 2,d			;09d0	cb 52		. R
	jr z,l09d5h		;09d2	28 01		( .
	dec b			;09d4	05		.
l09d5h:
	push bc			;09d5	c5		.
	ld bc,l1ffdh		;09d6	01 fd 1f	. . .
	ld a,01ah		;09d9	3e 1a		> .
	out (c),a		;09db	ed 79		. y
	pop bc			;09dd	c1		.
l09deh:
	call sub_0a3fh		;09de	cd 3f 0a	. ? .
	push bc			;09e1	c5		.
	ld a,c			;09e2	79		y
	ld bc,l1ffdh		;09e3	01 fd 1f	. . .
	and 008h		;09e6	e6 08		. .
	or 012h			;09e8	f6 12		. .
	out (c),a		;09ea	ed 79		. y
	pop bc			;09ec	c1		.
	rrc c			;09ed	cb 09		. .
	djnz l09deh		;09ef	10 ed		. .
	call sub_0a3fh		;09f1	cd 3f 0a	. ? .
	ld bc,l1ffdh		;09f4	01 fd 1f	. . .
	ld a,012h		;09f7	3e 12		> .
	out (c),a		;09f9	ed 79		. y
	call sub_0a3fh		;09fb	cd 3f 0a	. ? .
	bit 3,d			;09fe	cb 5a		. Z
l0a00h:
	call z,sub_0a3fh	;0a00	cc 3f 0a	. ? .
	pop af			;0a03	f1		.
	ret			;0a04	c9		.
sub_0a05h:
	ld a,(0e054h)		;0a05	3a 54 e0	: T .
	or a			;0a08	b7		.
	ret z			;0a09	c8		.
	push bc			;0a0a	c5		.
	call MouseDriver	;0a0b	cd a9 09	. . .
	pop bc			;0a0e	c1		.
	ret			;0a0f	c9		.
l0a10h:
	ld c,a			;0a10	4f		O
l0a11h:
	call sub_0a33h		;0a11	cd 33 0a	. 3 .
	jr nc,l0a2fh		;0a14	30 19		0 .
	ld a,0ffh		;0a16	3e ff		> .
	in a,(0feh)		;0a18	db fe		. .
	rlca			;0a1a	07		.
	jr c,l0a11h		;0a1b	38 f4		8 .
	ld a,c			;0a1d	79		y
	ld bc,0ffddh		;0a1e	01 dd ff	. . .
	out (c),a		;0a21	ed 79		. y
	ld bc,l1ffdh		;0a23	01 fd 1f	. . .
	ld a,032h		;0a26	3e 32		> 2
	out (c),a		;0a28	ed 79		. y
	ld a,012h		;0a2a	3e 12		> .
	out (c),a		;0a2c	ed 79		. y
	ret			;0a2e	c9		.
l0a2fh:
	ld hl,(0de12h)		;0a2f	2a 12 de	* . .
	jp (hl)			;0a32	e9		.
sub_0a33h:
	ld a,07fh		;0a33	3e 7f		> .
	in a,(0feh)		;0a35	db fe		. .
	rra			;0a37	1f		.
	ret c			;0a38	d8		.
	ld a,0feh		;0a39	3e fe		> .
	in a,(0feh)		;0a3b	db fe		. .
	rra			;0a3d	1f		.
	ret			;0a3e	c9		.
sub_0a3fh:
	push af			;0a3f	f5		.
	ld hl,(0e057h)		;0a40	2a 57 e0	* W .
	bit 1,d			;0a43	cb 4a		. J
	jr z,l0a4ah		;0a45	28 03		( .
	ld hl,(0e059h)		;0a47	2a 59 e0	* Y .
l0a4ah:
	dec hl			;0a4a	2b		+
	ld a,h			;0a4b	7c		|
	or l			;0a4c	b5		.
	jr nz,l0a4ah		;0a4d	20 fb		  .
	pop af			;0a4f	f1		.
	ret			;0a50	c9		.
sub_0a51h:
	ld bc,WriteAnyBankByte	;0a51	01 00 00	. . .
l0a54h:
	set 5,(iy+009h)		;0a54	fd cb 09 ee	. . . .
	bit 5,(iy+015h)		;0a58	fd cb 15 6e	. . . n
	ld a,001h		;0a5c	3e 01		> .
	jr z,l0a62h		;0a5e	28 02		( .
	ld a,003h		;0a60	3e 03		> .
l0a62h:
	ld (0e061h),a		;0a62	32 61 e0	2 a .
	push bc			;0a65	c5		.
	ld hl,0e05bh		;0a66	21 5b e0	! [ .
	call sub_0ad2h		;0a69	cd d2 0a	. . .
	pop bc			;0a6c	c1		.
l0a6dh:
	push bc			;0a6d	c5		.
	bit 5,(iy+015h)		;0a6e	fd cb 15 6e	. . . n
	ld d,008h		;0a72	16 08		. .
	jr z,l0a78h		;0a74	28 02		( .
	ld d,004h		;0a76	16 04		. .
l0a78h:
	push bc			;0a78	c5		.
	ld a,b			;0a79	78		x
	and a			;0a7a	a7		.
	rra			;0a7b	1f		.
	scf			;0a7c	37		7
	rra			;0a7d	1f		.
	and a			;0a7e	a7		.
	rra			;0a7f	1f		.
	xor b			;0a80	a8		.
	and 0f8h		;0a81	e6 f8		. .
	xor b			;0a83	a8		.
	ld h,a			;0a84	67		g
	ld a,c			;0a85	79		y
	rlca			;0a86	07		.
	rlca			;0a87	07		.
	rlca			;0a88	07		.
	xor b			;0a89	a8		.
	and 0c7h		;0a8a	e6 c7		. .
	xor b			;0a8c	a8		.
	rlca			;0a8d	07		.
	rlca			;0a8e	07		.
	ld l,a			;0a8f	6f		o
	ld a,c			;0a90	79		y
	and 007h		;0a91	e6 07		. .
	ld b,a			;0a93	47		G
	inc b			;0a94	04		.
	ld a,(hl)		;0a95	7e		~
l0a96h:
	rlca			;0a96	07		.
	djnz l0a96h		;0a97	10 fd		. .
	push af			;0a99	f5		.
	rl e			;0a9a	cb 13		. .
	pop af			;0a9c	f1		.
	bit 5,(iy+015h)		;0a9d	fd cb 15 6e	. . . n
	jr z,l0aa5h		;0aa1	28 02		( .
	rl e			;0aa3	cb 13		. .
l0aa5h:
	pop bc			;0aa5	c1		.
	inc b			;0aa6	04		.
	dec d			;0aa7	15		.
	jr nz,l0a78h		;0aa8	20 ce		  .
	ld a,e			;0aaa	7b		{
	bit 2,(iy+012h)		;0aab	fd cb 12 56	. . . V
	jr z,l0ab2h		;0aaf	28 01		( .
	cpl			;0ab1	2f		/
l0ab2h:
	rst 10h			;0ab2	d7		.
	bit 5,(iy+015h)		;0ab3	fd cb 15 6e	. . . n
	jr z,l0abbh		;0ab7	28 02		( .
	rst 10h			;0ab9	d7		.
	rst 10h			;0aba	d7		.
l0abbh:
	inc c			;0abb	0c		.
	ld a,c			;0abc	79		y
	jr z,l0ac3h		;0abd	28 04		( .
	pop bc			;0abf	c1		.
	ld c,a			;0ac0	4f		O
	jr l0a6dh		;0ac1	18 aa		. .
l0ac3h:
	pop de			;0ac3	d1		.
	res 5,(iy+009h)		;0ac4	fd cb 09 ae	. . . .
	rst 20h			;0ac8	e7		.
	adc a,l			;0ac9	8d		.
	ld a,0bfh		;0aca	3e bf		> .
	cp b			;0acc	b8		.
	jr nc,l0a54h		;0acd	30 85		0 .
	ld hl,0e063h		;0acf	21 63 e0	! c .
sub_0ad2h:
	ld b,(hl)		;0ad2	46		F
	inc hl			;0ad3	23		#
	jp l15b3h		;0ad4	c3 b3 15	. . .
l0ad7h:
	res 0,(iy+012h)		;0ad7	fd cb 12 86	. . . .
	rst 30h			;0adb	f7		.
	or l			;0adc	b5		.
	ld c,005h		;0add	0e 05		. .
	ret			;0adf	c9		.
l0ae0h:
	ld (iy+00ah),000h	;0ae0	fd 36 0a 00	. 6 . .
	push af			;0ae4	f5		.
	call sub_2ca8h		;0ae5	cd a8 2c	. . ,
	pop af			;0ae8	f1		.
	jr ExitToError		;0ae9	18 1b		. .
	ld sp,MonitorStack	;0aeb	31 36 e3	1 6 .
	set 0,(iy+012h)		;0aee	fd cb 12 c6	. . . .
l0af2h:
	call ReadPort7FFD	;0af2	cd d5 04	. . .
	xor a			;0af5	af		.
	ld (0e01eh),a		;0af6	32 1e e0	2 . .
	jr l0afch		;0af9	18 01		. .
l0afbh:
	scf			;0afb	37		7
l0afch:
	push af			;0afc	f5		.
	call sub_2ca8h		;0afd	cd a8 2c	. . ,
	pop af			;0b00	f1		.
	call nc,TapeMenu	;0b01	d4 cd 33	. . 3
	ld a,081h		;0b04	3e 81		> .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ExitToError: unwind to the command loop through the error
; window path (cf. InitErrorWindow $3A95).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ExitToError:
	ld sp,MonitorStack	;0b06	31 36 e3	1 6 .
	call sub_0550h		;0b09	cd 50 05	. P .
	ld hl,ExitToError	;0b0c	21 06 0b	! . .
	push hl			;0b0f	e5		.
	rst 30h			;0b10	f7		.
	ld (hl),e		;0b11	73		s
	inc e			;0b12	1c		.
	dec b			;0b13	05		.
	jr nc,DeadBytes0B23_end	;0b14	30 2a		0 *
	call sub_2c7eh		;0b16	cd 7e 2c	. ~ ,
	add a,a			;0b19	87		.
	inc a			;0b1a	3c		<
	jr nc,l0aa5h		;0b1b	30 88		0 .
	ccf			;0b1d	3f		?
	jr nc,$-117		;0b1e	30 89		0 .
	add hl,de		;0b20	19		.
	djnz $-116		;0b21	10 8a		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DeadBytes0B23: unreferenced 29-byte blob - a
; remnant of a routine removed when the RST 30h
; RAM-hook dispatcher replaced the older call
; path; nothing enters it.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'DeadBytes0B23' (start 0x0b23 end 0x0b40)
DeadBytes0B23_start:
	defb 0ffh		;0b23	ff		.
	defb 00fh		;0b24	0f		.
	defb 01ch		;0b25	1c		.
	defb 0f5h		;0b26	f5		.
	defb 00fh		;0b27	0f		.
	defb 01eh		;0b28	1e		.
	defb 0fah		;0b29	fa		.
	defb 00fh		;0b2a	0f		.
	defb 01dh		;0b2b	1d		.
	defb 0ebh		;0b2c	eb		.
	defb 00fh		;0b2d	0f		.
	defb 08bh		;0b2e	8b		.
	defb 0f0h		;0b2f	f0		.
	defb 00fh		;0b30	0f		.
	defb 08ch		;0b31	8c		.
	defb 017h		;0b32	17		.
	defb 035h		;0b33	35		5
	defb 08eh		;0b34	8e		.
	defb 0d8h		;0b35	d8		.
	defb 038h		;0b36	38		8
	defb 08dh		;0b37	8d		.
	defb 055h		;0b38	55		U
	defb 036h		;0b39	36		6
	defb 004h		;0b3a	04		.
	defb 001h		;0b3b	01		.
	defb 011h		;0b3c	11		.
	defb 0ffh		;0b3d	ff		.
	defb 0afh		;0b3e	af		.
	defb 0c9h		;0b3f	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ParseCommand: the command/expression reader.  Accepts a typed
; line into the IX work buffer, evaluates operands and reduces
; the input to tokens for DispatchMode.  v4.01 matches the verb
; against the linked-list token table at $23A3 (TokenTable)
; instead of the base compressed table walk.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DeadBytes0B23_end:
ParseCommand:
	call sub_0ee3h		;0b40	cd e3 0e	. . .
	ld e,000h		;0b43	1e 00		. .
	cp 00dh			;0b45	fe 0d		. .
	jp z,l0af2h		;0b47	ca f2 0a	. . .
	cp 05bh			;0b4a	fe 5b		. [
	jr z,l0b56h		;0b4c	28 08		( .
	cp 028h			;0b4e	fe 28		. (
	jr nz,l0b59h		;0b50	20 07		  .
	ld e,004h		;0b52	1e 04		. .
	jr l0b58h		;0b54	18 02		. .
l0b56h:
	ld e,084h		;0b56	1e 84		. .
l0b58h:
	inc hl			;0b58	23		#
l0b59h:
	ld (iy+000h),e		;0b59	fd 73 00	. s .
	bit 2,e			;0b5c	cb 53		. S
	jr nz,l0bcch		;0b5e	20 6c		  l
	ld de,l0fa5h		;0b60	11 a5 0f	. . .
	ld bc,l0f66h		;0b63	01 66 0f	. f .
	call sub_0f2eh		;0b66	cd 2e 0f	. . .
	jp z,DispatchMode	;0b69	ca 0f 0c	. . .
	ex de,hl		;0b6c	eb		.
	cp 021h			;0b6d	fe 21		. !
	jr nc,l0b7dh		;0b6f	30 0c		0 .
	cp 00dh			;0b71	fe 0d		. .
	jr nc,l0b7eh		;0b73	30 09		0 .
	set 3,(iy+000h)		;0b75	fd cb 00 de	. . . .
	sla c			;0b79	cb 21		. !
	jr l0b82h		;0b7b	18 05		. .
l0b7dh:
	inc bc			;0b7d	03		.
l0b7eh:
	ld a,c			;0b7e	79		y
	sub 007h		;0b7f	d6 07		. .
	ld c,a			;0b81	4f		O
l0b82h:
	ld a,(de)		;0b82	1a		.
	cp 03dh			;0b83	fe 3d		. =
	jp nz,DispatchMode	;0b85	c2 0f 0c	. . .
	inc de			;0b88	13		.
	ex de,hl		;0b89	eb		.
	push bc			;0b8a	c5		.
	call sub_0ebah		;0b8b	cd ba 0e	. . .
	pop bc			;0b8e	c1		.
	cp 001h			;0b8f	fe 01		. .
	ld a,00ah		;0b91	3e 0a		> .
	ret c			;0b93	d8		.
	ld a,00bh		;0b94	3e 0b		> .
	ret nz			;0b96	c0		.
	bit 2,(iy+000h)		;0b97	fd cb 00 56	. . . V
	jr nz,l0bf0h		;0b9b	20 53		  S
	ld hl,0dd69h		;0b9d	21 69 dd	! i .
	add hl,bc		;0ba0	09		.
	bit 3,(iy+000h)		;0ba1	fd cb 00 5e	. . . ^
	jr nz,l0bb1h		;0ba5	20 0a		  .
	ld a,d			;0ba7	7a		z
	or a			;0ba8	b7		.
	jr nz,l0bc9h		;0ba9	20 1e		  .
	call sub_0eaah		;0bab	cd aa 0e	. . .
	ld (hl),e		;0bae	73		s
	jr l0bc6h		;0baf	18 15		. .
l0bb1h:
	ld a,c			;0bb1	79		y
	cp 002h			;0bb2	fe 02		. .
	jr nz,l0bbah		;0bb4	20 04		  .
	res 6,(iy+00ah)		;0bb6	fd cb 0a b6	. . . .
l0bbah:
	or a			;0bba	b7		.
	push af			;0bbb	f5		.
	call z,sub_0eb1h	;0bbc	cc b1 0e	. . .
	pop af			;0bbf	f1		.
	call nz,sub_0eaah	;0bc0	c4 aa 0e	. . .
	ld (hl),e		;0bc3	73		s
	inc hl			;0bc4	23		#
	ld (hl),d		;0bc5	72		r
l0bc6h:
	jp l0afbh		;0bc6	c3 fb 0a	. . .
l0bc9h:
	ld a,00ch		;0bc9	3e 0c		> .
	ret			;0bcb	c9		.
l0bcch:
	call sub_0c98h		;0bcc	cd 98 0c	. . .
	ret c			;0bcf	d8		.
	ex de,hl		;0bd0	eb		.
	ld c,l			;0bd1	4d		M
	ld b,h			;0bd2	44		D
	ld a,(de)		;0bd3	1a		.
	inc de			;0bd4	13		.
	ld hl,IyWorkBase	;0bd5	21 14 e0	! . .
	cp 029h			;0bd8	fe 29		. )
	jr z,l0be9h		;0bda	28 0d		( .
	cp 05dh			;0bdc	fe 5d		. ]
	jp nz,DispatchMode	;0bde	c2 0f 0c	. . .
	bit 7,(hl)		;0be1	cb 7e		. ~
	jr z,l0bedh		;0be3	28 08		( .
	set 3,(hl)		;0be5	cb de		. .
	jr l0b82h		;0be7	18 99		. .
l0be9h:
	bit 7,(hl)		;0be9	cb 7e		. ~
	jr z,l0b82h		;0beb	28 95		( .
l0bedh:
	ld a,015h		;0bed	3e 15		> .
	ret			;0bef	c9		.
l0bf0h:
	bit 3,(iy+000h)		;0bf0	fd cb 00 5e	. . . ^
	ld l,c			;0bf4	69		i
	ld h,b			;0bf5	60		`
	jr nz,l0c08h		;0bf6	20 10		  .
	ld a,d			;0bf8	7a		z
	or a			;0bf9	b7		.
	jr nz,l0bc9h		;0bfa	20 cd		  .
	ld a,e			;0bfc	7b		{
	rst 0			;0bfd	c7		.
l0bfeh:
	ld a,(0e01eh)		;0bfe	3a 1e e0	: . .
	and 081h		;0c01	e6 81		. .
	ld (0e01eh),a		;0c03	32 1e e0	2 . .
	jr l0bc6h		;0c06	18 be		. .
l0c08h:
	ld a,e			;0c08	7b		{
	rst 0			;0c09	c7		.
	inc hl			;0c0a	23		#
	ld a,d			;0c0b	7a		z
	rst 0			;0c0c	c7		.
	jr l0bfeh		;0c0d	18 ef		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DispatchMode: routes between line editor, disassembler and
; register views according to the monitor flags (IY+12/IY+16
; bits), then falls into CommandLoop for the next command.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DispatchMode:
	ld hl,0e11eh		;0c0f	21 1e e1	! . .
	ld b,003h		;0c12	06 03		. .
l0c14h:
	ld e,(hl)		;0c14	5e		^
	inc hl			;0c15	23		#
	ld d,(hl)		;0c16	56		V
	inc hl			;0c17	23		#
	ld (0dda7h),de		;0c18	ed 53 a7 dd	. S . .
	ld a,e			;0c1c	7b		{
	or d			;0c1d	b2		.
	jr z,l0c32h		;0c1e	28 12		( .
	ld e,(hl)		;0c20	5e		^
	inc hl			;0c21	23		#
	ld d,(hl)		;0c22	56		V
	inc hl			;0c23	23		#
	push hl			;0c24	e5		.
	push bc			;0c25	c5		.
	call sub_0ee3h		;0c26	cd e3 0e	. . .
	call FindKeyword	;0c29	cd f0 0e	. . .
	jr nz,l0c37h		;0c2c	20 09		  .
	pop bc			;0c2e	c1		.
	pop hl			;0c2f	e1		.
	djnz l0c14h		;0c30	10 e2		. .
l0c32h:
	ld a,016h		;0c32	3e 16		> .
	jp ExitToError		;0c34	c3 06 0b	. . .
l0c37h:
	pop de			;0c37	d1		.
	ex (sp),hl		;0c38	e3		.
	add a,c			;0c39	81		.
	add a,c			;0c3a	81		.
	ld c,a			;0c3b	4f		O
	ld hl,(0dda7h)		;0c3c	2a a7 dd	* . .
	add hl,bc		;0c3f	09		.
	ld e,(hl)		;0c40	5e		^
	inc hl			;0c41	23		#
	ld d,(hl)		;0c42	56		V
	inc hl			;0c43	23		#
	ld c,(hl)		;0c44	4e		N
	pop hl			;0c45	e1		.
	push de			;0c46	d5		.
	bit 7,c			;0c47	cb 79		. y
	jr nz,l0c67h		;0c49	20 1c		  .
	bit 5,c			;0c4b	cb 69		. i
	ret nz			;0c4d	c0		.
	push bc			;0c4e	c5		.
	call sub_0ebah		;0c4f	cd ba 0e	. . .
	pop bc			;0c52	c1		.
	bit 6,c			;0c53	cb 71		. q
	jr nz,l0c62h		;0c55	20 0b		  .
	cp c			;0c57	b9		.
	jr z,l0c62h		;0c58	28 08		( .
	ld a,00ah		;0c5a	3e 0a		> .
	jr c,l0c5fh		;0c5c	38 01		8 .
	inc a			;0c5e	3c		<
l0c5fh:
	jp ExitToError		;0c5f	c3 06 0b	. . .
l0c62h:
	ld bc,(0dd9dh)		;0c62	ed 4b 9d dd	. K . .
	ret			;0c66	c9		.
l0c67h:
	ld b,000h		;0c67	06 00		. .
	push hl			;0c69	e5		.
l0c6ah:
	dec c			;0c6a	0d		.
	bit 7,c			;0c6b	cb 79		. y
	jr nz,l0c73h		;0c6d	20 04		  .
	set 1,(iy+000h)		;0c6f	fd cb 00 ce	. . . .
l0c73h:
	bit 1,(iy+000h)		;0c73	fd cb 00 4e	. . . N
	push bc			;0c77	c5		.
	jr nz,l0c7fh		;0c78	20 05		  .
	call sub_0c98h		;0c7a	cd 98 0c	. . .
	jr l0c82h		;0c7d	18 03		. .
l0c7fh:
	call sub_0d55h		;0c7f	cd 55 0d	. U .
l0c82h:
	pop bc			;0c82	c1		.
	jr z,l0c8ah		;0c83	28 05		( .
	jp nc,ExitToError	;0c85	d2 06 0b	. . .
	pop hl			;0c88	e1		.
	ret			;0c89	c9		.
l0c8ah:
	inc b			;0c8a	04		.
	bit 7,c			;0c8b	cb 79		. y
	jr nz,l0c6ah		;0c8d	20 db		  .
	ld a,d			;0c8f	7a		z
	or a			;0c90	b7		.
	jr z,l0c6ah		;0c91	28 d7		( .
	ld a,00ch		;0c93	3e 0c		> .
	jp ExitToError		;0c95	c3 06 0b	. . .
sub_0c98h:
	dec hl			;0c98	2b		+
l0c99h:
	inc hl			;0c99	23		#
	call sub_0d42h		;0c9a	cd 42 0d	. B .
	jr nz,l0ca7h		;0c9d	20 08		  .
	jr nc,l0c99h		;0c9f	30 f8		0 .
	xor a			;0ca1	af		.
	inc a			;0ca2	3c		<
	ld a,012h		;0ca3	3e 12		> .
	scf			;0ca5	37		7
	ret			;0ca6	c9		.
l0ca7h:
	ld (iy+00fh),000h	;0ca7	fd 36 0f 00	. 6 . .
l0cabh:
	ld de,WriteAnyBankByte	;0cab	11 00 00	. . .
	push de			;0cae	d5		.
	ld d,02bh		;0caf	16 2b		. +
	ld a,(hl)		;0cb1	7e		~
	call sub_0d38h		;0cb2	cd 38 0d	. 8 .
	jr nz,l0cb9h		;0cb5	20 02		  .
	inc hl			;0cb7	23		#
l0cb8h:
	ld d,a			;0cb8	57		W
l0cb9h:
	push de			;0cb9	d5		.
	ld a,(hl)		;0cba	7e		~
	cp 028h			;0cbb	fe 28		. (
	jr z,l0cc3h		;0cbd	28 04		( .
	cp 05bh			;0cbf	fe 5b		. [
	jr nz,l0ccch		;0cc1	20 09		  .
l0cc3h:
	inc hl			;0cc3	23		#
	inc (iy+00fh)		;0cc4	fd 34 0f	. 4 .
	pop de			;0cc7	d1		.
	ld e,a			;0cc8	5f		_
	push de			;0cc9	d5		.
	jr l0cabh		;0cca	18 df		. .
l0ccch:
	call sub_0d55h		;0ccc	cd 55 0d	. U .
	jr nz,l0cf8h		;0ccf	20 27		  '
l0cd1h:
	pop af			;0cd1	f1		.
	ex (sp),hl		;0cd2	e3		.
	cp 02dh			;0cd3	fe 2d		. -
	jr z,l0cdah		;0cd5	28 03		( .
	add hl,de		;0cd7	19		.
	jr l0cddh		;0cd8	18 03		. .
l0cdah:
	or a			;0cda	b7		.
	sbc hl,de		;0cdb	ed 52		. R
l0cddh:
	ex (sp),hl		;0cdd	e3		.
	ld a,(hl)		;0cde	7e		~
	inc hl			;0cdf	23		#
	call sub_0d38h		;0ce0	cd 38 0d	. 8 .
	jr z,l0cb8h		;0ce3	28 d3		( .
	cp 029h			;0ce5	fe 29		. )
	jr z,l0cfbh		;0ce7	28 12		( .
	cp 05dh			;0ce9	fe 5d		. ]
	jr z,l0d0eh		;0ceb	28 21		( !
	dec hl			;0ced	2b		+
	xor a			;0cee	af		.
	cp (iy+00fh)		;0cef	fd be 0f	. . .
	jr nz,l0cf6h		;0cf2	20 02		  .
	pop de			;0cf4	d1		.
	ret			;0cf5	c9		.
l0cf6h:
	ld a,013h		;0cf6	3e 13		> .
l0cf8h:
	jp ExitToError		;0cf8	c3 06 0b	. . .
l0cfbh:
	pop de			;0cfb	d1		.
	call sub_0d22h		;0cfc	cd 22 0d	. " .
	pop af			;0cff	f1		.
	push af			;0d00	f5		.
	ld a,015h		;0d01	3e 15		> .
	jr c,l0cf8h		;0d03	38 f3		8 .
	push hl			;0d05	e5		.
	ex de,hl		;0d06	eb		.
	rst 28h			;0d07	ef		.
	ld e,a			;0d08	5f		_
	ld d,000h		;0d09	16 00		. .
	pop hl			;0d0b	e1		.
	jr l0cd1h		;0d0c	18 c3		. .
l0d0eh:
	pop de			;0d0e	d1		.
	call sub_0d22h		;0d0f	cd 22 0d	. " .
	pop af			;0d12	f1		.
	push af			;0d13	f5		.
	ld a,015h		;0d14	3e 15		> .
	jr nc,l0cf8h		;0d16	30 e0		0 .
	push hl			;0d18	e5		.
	ex de,hl		;0d19	eb		.
	rst 28h			;0d1a	ef		.
	ld e,a			;0d1b	5f		_
	inc hl			;0d1c	23		#
	rst 28h			;0d1d	ef		.
	ld d,a			;0d1e	57		W
	pop hl			;0d1f	e1		.
	jr l0cd1h		;0d20	18 af		. .
sub_0d22h:
	xor a			;0d22	af		.
	cp (iy+00fh)		;0d23	fd be 0f	. . .
	jr z,l0d2ch		;0d26	28 04		( .
	dec (iy+00fh)		;0d28	fd 35 0f	. 5 .
	ret			;0d2b	c9		.
l0d2ch:
	bit 2,(iy+000h)		;0d2c	fd cb 00 56	. . . V
	ld a,014h		;0d30	3e 14		> .
	jr z,l0cf8h		;0d32	28 c4		( .
	pop bc			;0d34	c1		.
	dec hl			;0d35	2b		+
	xor a			;0d36	af		.
	ret			;0d37	c9		.
sub_0d38h:
	cp 02bh			;0d38	fe 2b		. +
	ret z			;0d3a	c8		.
	cp 02dh			;0d3b	fe 2d		. -
	ret			;0d3d	c9		.
sub_0d3eh:
	ld a,(hl)		;0d3e	7e		~
	cp 03ah			;0d3f	fe 3a		. :
	ret z			;0d41	c8		.
sub_0d42h:
	ld a,(hl)		;0d42	7e		~
	cp 020h			;0d43	fe 20		.  
	ret z			;0d45	c8		.
	ld a,(hl)		;0d46	7e		~
	cp 02ch			;0d47	fe 2c		. ,
	ret z			;0d49	c8		.
	cp 03bh			;0d4a	fe 3b		. ;
	ret z			;0d4c	c8		.
	or a			;0d4d	b7		.
	scf			;0d4e	37		7
	ret z			;0d4f	c8		.
	cp 00dh			;0d50	fe 0d		. .
	ret nz			;0d52	c0		.
	scf			;0d53	37		7
	ret			;0d54	c9		.
sub_0d55h:
	bit 0,(iy+000h)		;0d55	fd cb 00 46	. . . F
	ld a,010h		;0d59	3e 10		> .
	ld (0e024h),a		;0d5b	32 24 e0	2 $ .
	jr nz,l0d74h		;0d5e	20 14		  .
	dec hl			;0d60	2b		+
l0d61h:
	inc hl			;0d61	23		#
	res 0,(iy+000h)		;0d62	fd cb 00 86	. . . .
	call sub_0d42h		;0d66	cd 42 0d	. B .
	jr nz,l0d6fh		;0d69	20 04		  .
	jr c,l0da6h		;0d6b	38 39		8 9
	jr l0d61h		;0d6d	18 f2		. .
l0d6fh:
	cp 022h			;0d6f	fe 22		. "
	jr nz,l0dach		;0d71	20 39		  9
	inc hl			;0d73	23		#
l0d74h:
	ld a,(hl)		;0d74	7e		~
	cp 00dh			;0d75	fe 0d		. .
	jr z,l0da2h		;0d77	28 29		( )
	cp 022h			;0d79	fe 22		. "
	jr z,l0d9ch		;0d7b	28 1f		( .
	ld e,a			;0d7d	5f		_
	ld d,000h		;0d7e	16 00		. .
	bit 1,(iy+000h)		;0d80	fd cb 00 4e	. . . N
	jr nz,l0d8eh		;0d84	20 08		  .
	inc hl			;0d86	23		#
	ld a,022h		;0d87	3e 22		> "
	cp (hl)			;0d89	be		.
	jr nz,l0da2h		;0d8a	20 16		  .
	jr l0d92h		;0d8c	18 04		. .
l0d8eh:
	set 0,(iy+000h)		;0d8e	fd cb 00 c6	. . . .
l0d92h:
	inc hl			;0d92	23		#
	xor a			;0d93	af		.
	ld (0e024h),a		;0d94	32 24 e0	2 $ .
	ret			;0d97	c9		.
	ld a,00dh		;0d98	3e 0d		> .
	or a			;0d9a	b7		.
	ret			;0d9b	c9		.
l0d9ch:
	bit 1,(iy+000h)		;0d9c	fd cb 00 4e	. . . N
	jr nz,l0d61h		;0da0	20 bf		  .
l0da2h:
	ld a,00eh		;0da2	3e 0e		> .
	or a			;0da4	b7		.
	ret			;0da5	c9		.
l0da6h:
	xor a			;0da6	af		.
	inc a			;0da7	3c		<
	ld a,00fh		;0da8	3e 0f		> .
	scf			;0daa	37		7
	ret			;0dab	c9		.
l0dach:
	ld de,l0fa0h		;0dac	11 a0 0f	. . .
	ld bc,l0f41h		;0daf	01 41 0f	. A .
	call sub_0f2eh		;0db2	cd 2e 0f	. . .
	jr nz,l0dbdh		;0db5	20 06		  .
	call EditorSyntaxChars_end	;0db7	cd f4 0d	. . .
	ret z			;0dba	c8		.
	or a			;0dbb	b7		.
	ret			;0dbc	c9		.
l0dbdh:
	ld de,l0001h		;0dbd	11 01 00	. . .
	ld a,c			;0dc0	79		y
	or a			;0dc1	b7		.
	ret z			;0dc2	c8		.
	dec de			;0dc3	1b		.
	dec a			;0dc4	3d		=
	ret z			;0dc5	c8		.
	push hl			;0dc6	e5		.
	ld hl,0dd84h		;0dc7	21 84 dd	! . .
	ld a,c			;0dca	79		y
	sub 023h		;0dcb	d6 23		. #
	jr nc,l0de4h		;0dcd	30 15		0 .
	ld hl,0dd6fh		;0dcf	21 6f dd	! o .
	ld a,c			;0dd2	79		y
	sub 00fh		;0dd3	d6 0f		. .
	jr nc,l0de4h		;0dd5	30 0d		0 .
	dec bc			;0dd7	0b		.
	dec bc			;0dd8	0b		.
	ld hl,0dd69h		;0dd9	21 69 dd	! i .
	add hl,bc		;0ddc	09		.
	add hl,bc		;0ddd	09		.
	ld e,(hl)		;0dde	5e		^
	inc hl			;0ddf	23		#
	ld d,(hl)		;0de0	56		V
l0de1h:
	pop hl			;0de1	e1		.
	xor a			;0de2	af		.
	ret			;0de3	c9		.
l0de4h:
	add a,l			;0de4	85		.
	ld l,a			;0de5	6f		o
	jr nc,l0de9h		;0de6	30 01		0 .
	inc h			;0de8	24		$
l0de9h:
	ld e,(hl)		;0de9	5e		^
	jr l0de1h		;0dea	18 f5		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EditorSyntaxChars: four (character,code) pairs
; for the editor syntax tokens: '%' -> 2,
; '@' -> 8, '.' -> $0A, '#' -> $10; matched by the
; walker at $0DF4 (called from $0DB7/$2065) and
; walked four-at-a-time by the loader at $0E02.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'EditorSyntaxChars' (start 0x0dec end 0x0df4)
EditorSyntaxChars_start:
	defb 025h		;0dec	25		%
	defb 002h		;0ded	02		.
	defb 040h		;0dee	40		@
	defb 008h		;0def	08		.
	defb 02eh		;0df0	2e		.
	defb 00ah		;0df1	0a		.
	defb 023h		;0df2	23		#
	defb 010h		;0df3	10		.
EditorSyntaxChars_end:
	ld a,(0e02bh)		;0df4	3a 2b e0	: + .
	ld b,00ah		;0df7	06 0a		. .
	or a			;0df9	b7		.
	jr z,l0dfdh		;0dfa	28 01		( .
	ld b,a			;0dfc	47		G
l0dfdh:
	call sub_0e51h		;0dfd	cd 51 0e	. Q .
	jr nc,l0e17h		;0e00	30 15		0 .
	ld de,EditorSyntaxChars_start	;0e02	11 ec 0d	. . .
	ld b,004h		;0e05	06 04		. .
l0e07h:
	ld a,(de)		;0e07	1a		.
	inc de			;0e08	13		.
	cp (hl)			;0e09	be		.
	ld a,(de)		;0e0a	1a		.
	inc de			;0e0b	13		.
	jr z,l0e15h		;0e0c	28 07		( .
	djnz l0e07h		;0e0e	10 f7		. .
	ld b,(iy+017h)		;0e10	fd 46 17	. F .
	jr l0e17h		;0e13	18 02		. .
l0e15h:
	inc hl			;0e15	23		#
	ld b,a			;0e16	47		G
l0e17h:
	ld (iy+010h),b		;0e17	fd 70 10	. p .
	call sub_0e1fh		;0e1a	cd 1f 0e	. . .
	or a			;0e1d	b7		.
	ret			;0e1e	c9		.
sub_0e1fh:
	call sub_0e59h		;0e1f	cd 59 0e	. Y .
	ld a,010h		;0e22	3e 10		> .
	ret c			;0e24	d8		.
	ld b,000h		;0e25	06 00		. .
	ld d,b			;0e27	50		P
	ld e,c			;0e28	59		Y
	inc hl			;0e29	23		#
l0e2ah:
	call sub_0e59h		;0e2a	cd 59 0e	. Y .
	jr c,l0e45h		;0e2d	38 16		8 .
	push hl			;0e2f	e5		.
	ld hl,(0e024h)		;0e30	2a 24 e0	* $ .
	ld h,000h		;0e33	26 00		& .
	ex de,hl		;0e35	eb		.
	push bc			;0e36	c5		.
	call sub_0e85h		;0e37	cd 85 0e	. . .
	pop bc			;0e3a	c1		.
	jr nz,l0e4dh		;0e3b	20 10		  .
	add hl,bc		;0e3d	09		.
	jr c,l0e4dh		;0e3e	38 0d		8 .
	ex de,hl		;0e40	eb		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Rst18Handler - the v4.01 RST 18h backend: inline hexadecimal
; digit parser.  Pops the return address into HL, reads the
; character there (lower-case folded up), converts it to a nibble
; value and stacks the corrected resume address past the digit;
; carry/A >= 0Ah reports "not a digit" (the $0E45 wrapper turns
; that into error code $10).  $0E51 is the digit-range predicate,
; $0E59 the case-folding hex variant.  The base monitor spent this
; vector on RAM-extension calls (RamExtCall) instead.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst18Handler:
	pop hl			;0e41	e1		.
	inc hl			;0e42	23		#
	jr l0e2ah		;0e43	18 e5		. .
l0e45h:
	call sub_0e51h		;0e45	cd 51 0e	. Q .
	ld a,010h		;0e48	3e 10		> .
	ret nc			;0e4a	d0		.
	xor a			;0e4b	af		.
	ret			;0e4c	c9		.
l0e4dh:
	pop hl			;0e4d	e1		.
	ld a,011h		;0e4e	3e 11		> .
	ret			;0e50	c9		.
sub_0e51h:
	ld a,(hl)		;0e51	7e		~
	cp 030h			;0e52	fe 30		. 0
	ret c			;0e54	d8		.
	cp 03ah			;0e55	fe 3a		. :
	ccf			;0e57	3f		?
	ret			;0e58	c9		.
sub_0e59h:
	ld a,(hl)		;0e59	7e		~
	cp 061h			;0e5a	fe 61		. a
	jr c,l0e60h		;0e5c	38 02		8 .
	sub 020h		;0e5e	d6 20		.  
l0e60h:
	sub 030h		;0e60	d6 30		. 0
	ret c			;0e62	d8		.
	cp 00ah			;0e63	fe 0a		. .
	jr c,l0e6ch		;0e65	38 05		8 .
	cp 011h			;0e67	fe 11		. .
	ret c			;0e69	d8		.
	sub 007h		;0e6a	d6 07		. .
l0e6ch:
	ld c,a			;0e6c	4f		O
	ld a,(0e024h)		;0e6d	3a 24 e0	: $ .
	dec a			;0e70	3d		=
	cp c			;0e71	b9		.
	ret			;0e72	c9		.
sub_0e73h:
	res 2,(iy+00eh)		;0e73	fd cb 0e 96	. . . .
	ld a,d			;0e77	7a		z
	or e			;0e78	b3		.
	jr z,l0e80h		;0e79	28 05		( .
	ld a,h			;0e7b	7c		|
	ld c,l			;0e7c	4d		M
	jp Divide16		;0e7d	c3 aa 16	. . .
l0e80h:
	set 2,(iy+00eh)		;0e80	fd cb 0e d6	. . . .
	ret			;0e84	c9		.
sub_0e85h:
	res 0,(iy+00eh)		;0e85	fd cb 0e 86	. . . .
	ld c,h			;0e89	4c		L
	ld a,l			;0e8a	7d		}
	ld b,010h		;0e8b	06 10		. .
	ld hl,WriteAnyBankByte	;0e8d	21 00 00	! . .
l0e90h:
	add hl,hl		;0e90	29		)
	jr nc,l0e97h		;0e91	30 04		0 .
	set 0,(iy+00eh)		;0e93	fd cb 0e c6	. . . .
l0e97h:
	rla			;0e97	17		.
	rl c			;0e98	cb 11		. .
	jr nc,l0ea3h		;0e9a	30 07		0 .
	add hl,de		;0e9c	19		.
	jr nc,l0ea3h		;0e9d	30 04		0 .
	set 0,(iy+00eh)		;0e9f	fd cb 0e c6	. . . .
l0ea3h:
	djnz l0e90h		;0ea3	10 eb		. .
	bit 0,(iy+00eh)		;0ea5	fd cb 0e 46	. . . F
	ret			;0ea9	c9		.
sub_0eaah:
	ld a,(0e01eh)		;0eaa	3a 1e e0	: . .
	and 0f5h		;0ead	e6 f5		. .
	jr l0eb6h		;0eaf	18 05		. .
sub_0eb1h:
	ld a,(0e01eh)		;0eb1	3a 1e e0	: . .
	and 0cfh		;0eb4	e6 cf		. .
l0eb6h:
	ld (0e01eh),a		;0eb6	32 1e e0	2 . .
	ret			;0eb9	c9		.
sub_0ebah:
	xor a			;0eba	af		.
	ld bc,RegisterBackup	;0ebb	01 99 dd	. . .
l0ebeh:
	push af			;0ebe	f5		.
	push bc			;0ebf	c5		.
	call sub_0c98h		;0ec0	cd 98 0c	. . .
	pop bc			;0ec3	c1		.
	jr c,l0edah		;0ec4	38 14		8 .
	jp nz,ExitToError	;0ec6	c2 06 0b	. . .
	ld a,e			;0ec9	7b		{
	ld (bc),a		;0eca	02		.
	inc bc			;0ecb	03		.
	ld a,d			;0ecc	7a		z
	ld (bc),a		;0ecd	02		.
	inc bc			;0ece	03		.
	pop af			;0ecf	f1		.
	inc a			;0ed0	3c		<
	cp 006h			;0ed1	fe 06		. .
	jr nz,l0ebeh		;0ed3	20 e9		  .
	ld a,00bh		;0ed5	3e 0b		> .
	jp ExitToError		;0ed7	c3 06 0b	. . .
l0edah:
	pop af			;0eda	f1		.
	ld de,(RegisterBackup)	;0edb	ed 5b 99 dd	. [ . .
	ld hl,(0dd9bh)		;0edf	2a 9b dd	* . .
	ret			;0ee2	c9		.
sub_0ee3h:
	ld hl,0de17h		;0ee3	21 17 de	! . .
l0ee6h:
	ld a,(hl)		;0ee6	7e		~
	cp 020h			;0ee7	fe 20		.  
	inc hl			;0ee9	23		#
	jr z,l0ee6h		;0eea	28 fa		( .
	dec hl			;0eec	2b		+
	cp 00dh			;0eed	fe 0d		. .
	ret			;0eef	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FindKeyword: match (possibly abbreviated) input against the
; command/mnemonic token tables.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FindKeyword:
	ld c,000h		;0ef0	0e 00		. .
l0ef2h:
	push hl			;0ef2	e5		.
	ld a,(de)		;0ef3	1a		.
	cp 020h			;0ef4	fe 20		.  
	jr nc,l0efah		;0ef6	30 02		0 .
	inc de			;0ef8	13		.
	inc de			;0ef9	13		.
l0efah:
	ld a,(de)		;0efa	1a		.
	ld b,a			;0efb	47		G
	and 07fh		;0efc	e6 7f		. .
	cp 061h			;0efe	fe 61		. a
	jr c,l0f04h		;0f00	38 02		8 .
	set 7,c			;0f02	cb f9		. .
l0f04h:
	ld a,b			;0f04	78		x
	call KeyScanTableB_end	;0f05	cd 89 0f	. . .
	ld b,a			;0f08	47		G
	ld a,(hl)		;0f09	7e		~
	call KeyScanTableB_end	;0f0a	cd 89 0f	. . .
	cp b			;0f0d	b8		.
	jr nz,l0f1eh		;0f0e	20 0e		  .
	ld a,(de)		;0f10	1a		.
	bit 7,a			;0f11	cb 7f		. .
	inc hl			;0f13	23		#
	inc de			;0f14	13		.
	jr z,l0efah		;0f15	28 e3		( .
l0f17h:
	pop de			;0f17	d1		.
	res 7,c			;0f18	cb b9		. .
	ld a,c			;0f1a	79		y
	ld b,000h		;0f1b	06 00		. .
	ret			;0f1d	c9		.
l0f1eh:
	bit 7,c			;0f1e	cb 79		. y
	jr nz,l0f17h		;0f20	20 f5		  .
	pop hl			;0f22	e1		.
l0f23h:
	ld a,(de)		;0f23	1a		.
	rlca			;0f24	07		.
	inc de			;0f25	13		.
	jr nc,l0f23h		;0f26	30 fb		0 .
	inc c			;0f28	0c		.
	ld a,(de)		;0f29	1a		.
	or a			;0f2a	b7		.
	jr nz,l0ef2h		;0f2b	20 c5		  .
	ret			;0f2d	c9		.
sub_0f2eh:
	push bc			;0f2e	c5		.
	call FindKeyword	;0f2f	cd f0 0e	. . .
	pop de			;0f32	d1		.
	ret z			;0f33	c8		.
	push de			;0f34	d5		.
	ex (sp),hl		;0f35	e3		.
	push af			;0f36	f5		.
	add a,l			;0f37	85		.
	ld l,a			;0f38	6f		o
	jr nc,l0f3ch		;0f39	30 01		0 .
	inc h			;0f3b	24		$
l0f3ch:
	pop af			;0f3c	f1		.
	ld a,(hl)		;0f3d	7e		~
	ld c,a			;0f3e	4f		O
	pop hl			;0f3f	e1		.
	ret			;0f40	c9		.
l0f41h:
	nop			;0f41	00		.
	ld bc,Msg2302_start	;0f42	01 02 23	. . #
	ld c,00dh		;0f45	0e 0d		. .
	ld (02120h),hl		;0f47	22 20 21	"   !
	rra			;0f4a	1f		.
	rrca			;0f4b	0f		.
	djnz l0f5fh		;0f4c	10 11		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyScanTableA: first key-translate table of the
; editor (extended to 24 entries in 4.01) -
; internal key codes in scan order; continues in
; KeyScanTableB ($0F67).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'KeyScanTableA' (start 0x0f4e end 0x0f67)
KeyScanTableA_start:
	defb 012h		;0f4e	12		.
	defb 005h		;0f4f	05		.
	defb 006h		;0f50	06		.
	defb 003h		;0f51	03		.
	defb 004h		;0f52	04		.
	defb 00ah		;0f53	0a		.
	defb 00bh		;0f54	0b		.
	defb 00ch		;0f55	0c		.
	defb 007h		;0f56	07		.
	defb 008h		;0f57	08		.
	defb 009h		;0f58	09		.
	defb 019h		;0f59	19		.
	defb 01ah		;0f5a	1a		.
	defb 01bh		;0f5b	1b		.
	defb 01ch		;0f5c	1c		.
	defb 01dh		;0f5d	1d		.
	defb 01eh		;0f5e	1e		.
l0f5fh:
	defb 013h		;0f5f	13		.
	defb 014h		;0f60	14		.
	defb 015h		;0f61	15		.
	defb 016h		;0f62	16		.
	defb 017h		;0f63	17		.
	defb 018h		;0f64	18		.
	defb 024h		;0f65	24		$
l0f66h:
	defb 000h		;0f66	00		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyScanTableB: second key-translate table;
; the final $22 byte acts as a sentinel.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
KeyScanTableA_end:

; BLOCK 'KeyScanTableB' (start 0x0f67 end 0x0f89)
KeyScanTableB_start:
	defb 021h		;0f67	21		!
	defb 00ch		;0f68	0c		.
	defb 00bh		;0f69	0b		.
	defb 020h		;0f6a	20		 
	defb 01eh		;0f6b	1e		.
	defb 01fh		;0f6c	1f		.
	defb 01dh		;0f6d	1d		.
	defb 00dh		;0f6e	0d		.
	defb 00eh		;0f6f	0e		.
	defb 00fh		;0f70	0f		.
	defb 010h		;0f71	10		.
	defb 003h		;0f72	03		.
	defb 004h		;0f73	04		.
	defb 001h		;0f74	01		.
	defb 002h		;0f75	02		.
	defb 008h		;0f76	08		.
	defb 009h		;0f77	09		.
	defb 00ah		;0f78	0a		.
	defb 005h		;0f79	05		.
	defb 006h		;0f7a	06		.
	defb 007h		;0f7b	07		.
	defb 017h		;0f7c	17		.
	defb 018h		;0f7d	18		.
	defb 019h		;0f7e	19		.
	defb 01ah		;0f7f	1a		.
	defb 01bh		;0f80	1b		.
	defb 01ch		;0f81	1c		.
	defb 011h		;0f82	11		.
	defb 012h		;0f83	12		.
	defb 013h		;0f84	13		.
	defb 014h		;0f85	14		.
	defb 015h		;0f86	15		.
	defb 016h		;0f87	16		.
	defb 022h		;0f88	22		"
KeyScanTableB_end:
	and 07fh		;0f89	e6 7f		. .
	cp 040h			;0f8b	fe 40		. @
	ret c			;0f8d	d8		.
	and 05fh		;0f8e	e6 5f		. _
	ret			;0f90	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegisterNameList - the two register-name strings of the register
; dump (bit-7 terminated, printed by RST 20h; ShowRegisters at
; $2D70 loads HL here).  List 1 ($0F91) orders the main display
; PC,SP,IX,IY,HL,DE,BC; list 2 ($0FA0) enumerates every editable
; name ON,OFF,MEM,R,AF'... down to I at $0FE9.  Four cursor-delta
; trampolines follow at $0FEB (ld bc,-8/+8/-1/+1; jr $1005 -
; LEFT/RIGHT/DOWN/UP), converging on the cursor-move core.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RegisterNameList:

; BLOCK 'RegisterNameList' (start 0x0f91 end 0x0feb)
RegisterNameList_start:
	defb 050h		;0f91	50		P
	defb 0c3h		;0f92	c3		.
	defb 053h		;0f93	53		S
	defb 0d0h		;0f94	d0		.
	defb 049h		;0f95	49		I
	defb 0d8h		;0f96	d8		.
	defb 049h		;0f97	49		I
	defb 0d9h		;0f98	d9		.
	defb 048h		;0f99	48		H
	defb 0cch		;0f9a	cc		.
	defb 044h		;0f9b	44		D
	defb 0c5h		;0f9c	c5		.
	defb 042h		;0f9d	42		B
	defb 0c3h		;0f9e	c3		.
	defb 000h		;0f9f	00		.
l0fa0h:
	defb 04fh		;0fa0	4f		O
	defb 0ceh		;0fa1	ce		.
l0fa2h:
	defb 04fh		;0fa2	4f		O
	defb 046h		;0fa3	46		F
	defb 0c6h		;0fa4	c6		.
l0fa5h:
	defb 04dh		;0fa5	4d		M
	defb 045h		;0fa6	45		E
	defb 0cdh		;0fa7	cd		.
	defb 0d2h		;0fa8	d2		.
	defb 041h		;0fa9	41		A
	defb 046h		;0faa	46		F
	defb 0a7h		;0fab	a7		.
	defb 041h		;0fac	41		A
	defb 0c6h		;0fad	c6		.
	defb 041h		;0fae	41		A
	defb 0a7h		;0faf	a7		.
	defb 0c1h		;0fb0	c1		.
	defb 046h		;0fb1	46		F
	defb 0a7h		;0fb2	a7		.
	defb 0c6h		;0fb3	c6		.
	defb 049h		;0fb4	49		I
	defb 058h		;0fb5	58		X
	defb 0cch		;0fb6	cc		.
	defb 049h		;0fb7	49		I
	defb 058h		;0fb8	58		X
	defb 0c8h		;0fb9	c8		.
	defb 049h		;0fba	49		I
	defb 059h		;0fbb	59		Y
	defb 0cch		;0fbc	cc		.
	defb 049h		;0fbd	49		I
	defb 059h		;0fbe	59		Y
	defb 0c8h		;0fbf	c8		.
	defb 049h		;0fc0	49		I
	defb 0d8h		;0fc1	d8		.
	defb 049h		;0fc2	49		I
	defb 0d9h		;0fc3	d9		.
	defb 050h		;0fc4	50		P
	defb 0c3h		;0fc5	c3		.
	defb 053h		;0fc6	53		S
	defb 0d0h		;0fc7	d0		.
	defb 048h		;0fc8	48		H
	defb 04ch		;0fc9	4c		L
	defb 0a7h		;0fca	a7		.
	defb 044h		;0fcb	44		D
	defb 045h		;0fcc	45		E
l0fcdh:
	defb 0a7h		;0fcd	a7		.
	defb 042h		;0fce	42		B
	defb 043h		;0fcf	43		C
	defb 0a7h		;0fd0	a7		.
	defb 048h		;0fd1	48		H
	defb 0cch		;0fd2	cc		.
	defb 044h		;0fd3	44		D
	defb 0c5h		;0fd4	c5		.
	defb 042h		;0fd5	42		B
	defb 0c3h		;0fd6	c3		.
	defb 04ch		;0fd7	4c		L
	defb 0a7h		;0fd8	a7		.
	defb 048h		;0fd9	48		H
	defb 0a7h		;0fda	a7		.
	defb 045h		;0fdb	45		E
	defb 0a7h		;0fdc	a7		.
	defb 044h		;0fdd	44		D
	defb 0a7h		;0fde	a7		.
	defb 043h		;0fdf	43		C
	defb 0a7h		;0fe0	a7		.
	defb 042h		;0fe1	42		B
	defb 0a7h		;0fe2	a7		.
	defb 0cch		;0fe3	cc		.
	defb 0c8h		;0fe4	c8		.
	defb 0c5h		;0fe5	c5		.
	defb 0c4h		;0fe6	c4		.
	defb 0c3h		;0fe7	c3		.
	defb 0c2h		;0fe8	c2		.
	defb 0c9h		;0fe9	c9		.
	defb 000h		;0fea	00		.
RegisterNameList_end:
	ld bc,0fff8h		;0feb	01 f8 ff	. . .
	jr l1005h		;0fee	18 15		. .
	ld bc,Rst08Vector	;0ff0	01 08 00	. . .
l0ff3h:
	jr l1005h		;0ff3	18 10		. .
	ld bc,0ffffh		;0ff5	01 ff ff	. . .
	jr l1005h		;0ff8	18 0b		. .
	ld bc,l0001h		;0ffa	01 01 00	. . .
	jr l1005h		;0ffd	18 06		. .
	ld hl,(0dd69h)		;0fff	2a 69 dd	* i .
	call FetchPrefixBytes	;1002	cd 96 2f	. . /
l1005h:
	ld hl,(0dd69h)		;1005	2a 69 dd	* i .
	add hl,bc		;1008	09		.
	ld (0dd69h),hl		;1009	22 69 dd	" i .
	ld a,001h		;100c	3e 01		> .
	bit 0,(iy+00ah)		;100e	fd cb 0a 46	. . . F
	jr z,l102bh		;1012	28 17		( .
	call MemoryDumpWindow	;1014	cd 3d 2e	. = .
	xor a			;1017	af		.
	ret			;1018	c9		.
	ld hl,(UserPc)		;1019	2a 6b dd	* k .
	call FetchPrefixBytes	;101c	cd 96 2f	. . /
	ld (UserPc),hl		;101f	22 6b dd	" k .
	ld a,(iy+00ah)		;1022	fd 7e 0a	. ~ .
	and 0b5h		;1025	e6 b5		. .
	ld (iy+00ah),a		;1027	fd 77 0a	. w .
	xor a			;102a	af		.
l102bh:
	push af			;102b	f5		.
	call sub_2ca8h		;102c	cd a8 2c	. . ,
	pop af			;102f	f1		.
	ret			;1030	c9		.
l1031h:
	ld (0eb0dh),hl		;1031	22 0d eb	" . .
	jp l0040h		;1034	c3 40 00	. @ .
sub_1037h:
	call sub_1048h		;1037	cd 48 10	. H .
	set 6,(iy+014h)		;103a	fd cb 14 f6	. . . .
	ld b,000h		;103e	06 00		. .
	ld hl,HwConfigTable	;1040	21 f5 ea	! . .
l1043h:
	ld (hl),b		;1043	70		p
	inc hl			;1044	23		#
	djnz l1043h		;1045	10 fc		. .
	ret			;1047	c9		.
sub_1048h:
	ld iy,IyWorkBase	;1048	fd 21 14 e0	. ! . .
	call sub_0739h		;104c	cd 39 07	. 9 .
	ld bc,0fefeh		;104f	01 fe fe	. . .
	in a,(c)		;1052	ed 78		. x
	rrca			;1054	0f		.
	jr c,l105fh		;1055	38 08		8 .
	ld b,0f7h		;1057	06 f7		. .
	in a,(c)		;1059	ed 78		. x
	bit 4,a			;105b	cb 67		. g
	jr z,l1031h		;105d	28 d2		( .
l105fh:
	scf			;105f	37		7
	jr DeadBytes1062_end	;1060	18 18		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DeadBytes1062: unreferenced junk left between
; the register lists and MnemonicTable; nothing
; references it.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'DeadBytes1062' (start 0x1062 end 0x107a)
DeadBytes1062_start:
	defb 011h		;1062	11		.
	defb 006h		;1063	06		.
	defb 0c0h		;1064	c0		.
	defb 01ah		;1065	1a		.
	defb 093h		;1066	93		.
	defb 01bh		;1067	1b		.
	defb 028h		;1068	28		(
	defb 009h		;1069	09		.
	defb 0cdh		;106a	cd		.
	defb 025h		;106b	25		%
	defb 03eh		;106c	3e		>
	defb 037h		;106d	37		7
	defb 018h		;106e	18		.
	defb 00ah		;106f	0a		.
	defb 0ebh		;1070	eb		.
	defb 034h		;1071	34		4
	defb 0ebh		;1072	eb		.
	defb 01ah		;1073	1a		.
	defb 09bh		;1074	9b		.
	defb 0e6h		;1075	e6		.
	defb 007h		;1076	07		.
	defb 0cah		;1077	ca		.
	defb 01eh		;1078	1e		.
	defb 000h		;1079	00		.
DeadBytes1062_end:
	push af			;107a	f5		.
	ld hl,WriteAnyBankByte	;107b	21 00 00	! . .
	ld b,h			;107e	44		D
	ld c,0feh		;107f	0e fe		. .
	ld (PagingState),hl	;1081	22 12 e0	" . .
	ld h,005h		;1084	26 05		& .
l1086h:
	rst 28h			;1086	ef		.
	add a,b			;1087	80		.
	ld b,a			;1088	47		G
	inc hl			;1089	23		#
	ld a,h			;108a	7c		|
	sub 006h		;108b	d6 06		. .
	jr nz,l1086h		;108d	20 f7		  .
	ld a,(l2bb2h)		;108f	3a b2 2b	: . +
	sub b			;1092	90		.
	jp nz,Filler001B_end	;1093	c2 1e 00	. . .
	call sub_04ceh		;1096	cd ce 04	. . .
	rst 30h			;1099	f7		.
	ld e,d			;109a	5a		Z
	ld a,(bc)		;109b	0a		.
	inc b			;109c	04		.
	pop af			;109d	f1		.
	jr nc,l10b5h		;109e	30 15		0 .
	bit 1,(iy+014h)		;10a0	fd cb 14 4e	. . . N
	ret nz			;10a4	c0		.
	ld a,(0ffd0h)		;10a5	3a d0 ff	: . .
	push af			;10a8	f5		.
	call l10b5h		;10a9	cd b5 10	. . .
	rst 30h			;10ac	f7		.
	rst 28h			;10ad	ef		.
	ex af,af'		;10ae	08		.
	dec b			;10af	05		.
	pop af			;10b0	f1		.
	ld (0ffd0h),a		;10b1	32 d0 ff	2 . .
	ret			;10b4	c9		.
l10b5h:
	xor a			;10b5	af		.
	call sub_2c4dh		;10b6	cd 4d 2c	. M ,
	ld bc,0fefeh		;10b9	01 fe fe	. . .
	in a,(c)		;10bc	ed 78		. x
	rrca			;10be	0f		.
	jr c,l10c8h		;10bf	38 07		8 .
	ld b,0efh		;10c1	06 ef		. .
	in a,(c)		;10c3	ed 78		. x
	bit 2,a			;10c5	cb 57		. W
	ret z			;10c7	c8		.
l10c8h:
	bit 6,(iy+014h)		;10c8	fd cb 14 76	. . . v
	jr nz,l10d6h		;10cc	20 08		  .
	ld d,002h		;10ce	16 02		. .
	ld c,00eh		;10d0	0e 0e		. .
	rst 30h			;10d2	f7		.
	add hl,hl		;10d3	29		)
	ld (bc),a		;10d4	02		.
	rlca			;10d5	07		.
l10d6h:
	rst 30h			;10d6	f7		.
	ld a,e			;10d7	7b		{
	djnz $+9		;10d8	10 07		. .
	ret c			;10da	d8		.
	ld (ix+001h),000h	;10db	dd 36 01 00	. 6 . .
	ld bc,05d00h		;10df	01 00 5d	. . ]
	rst 8			;10e2	cf		.
	call PadColumns		;10e3	cd ed 2f	. . /
	ld hl,0db69h		;10e6	21 69 db	! i .
	push hl			;10e9	e5		.
	ld bc,l0105h		;10ea	01 05 01	. . .
	ld de,l000dh+2		;10ed	11 0f 00	. . .
	rst 30h			;10f0	f7		.
	add hl,hl		;10f1	29		)
	ld (bc),a		;10f2	02		.
	rlca			;10f3	07		.
	pop hl			;10f4	e1		.
	ret c			;10f5	d8		.
	ld a,041h		;10f6	3e 41		> A
	cpi			;10f8	ed a1		. .
	ret nz			;10fa	c0		.
	ld a,04dh		;10fb	3e 4d		> M
	cpi			;10fd	ed a1		. .
	ret nz			;10ff	c0		.
	jp (hl)			;1100	e9		.
	rst 30h			;1101	f7		.
	ld a,b			;1102	78		x
	ld (0af05h),hl		;1103	22 05 af	" . .
	ret			;1106	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MnemonicTable: packed mnemonic strings; placeholder
; characters carry bit 7 and mark operand splice points.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MnemonicTable:

; BLOCK 'MnemonicTable' (start 0x1107 end 0x14f6)
	defb 05ch		;1107	5c		\
	defb 001h		;1108	01		.
	defb 05ch		;1109	5c		\
	defb 002h		;110a	02		.
	defb 05ch		;110b	5c		\
	defb 003h		;110c	03		.
	defb 05ch		;110d	5c		\
	defb 004h		;110e	04		.
	defb 053h		;110f	53		S
	defb 015h		;1110	15		.
	defb 053h		;1111	53		S
	defb 014h		;1112	14		.
	defb 053h		;1113	53		S
	defb 00dh		;1114	0d		.
	defb 053h		;1115	53		S
	defb 016h		;1116	16		.
	defb 053h		;1117	53		S
	defb 017h		;1118	17		.
	defb 053h		;1119	53		S
	defb 01ah		;111a	1a		.
	defb 053h		;111b	53		S
	defb 019h		;111c	19		.
	defb 053h		;111d	53		S
	defb 018h		;111e	18		.
	defb 053h		;111f	53		S
	defb 022h		;1120	22		"
	defb 053h		;1121	53		S
	defb 023h		;1122	23		#
	defb 053h		;1123	53		S
	defb 024h		;1124	24		$
	defb 054h		;1125	54		T
	defb 015h		;1126	15		.
	defb 054h		;1127	54		T
	defb 014h		;1128	14		.
	defb 054h		;1129	54		T
	defb 00dh		;112a	0d		.
	defb 054h		;112b	54		T
	defb 016h		;112c	16		.
	defb 055h		;112d	55		U
	defb 015h		;112e	15		.
	defb 055h		;112f	55		U
	defb 014h		;1130	14		.
	defb 055h		;1131	55		U
	defb 00dh		;1132	0d		.
	defb 055h		;1133	55		U
	defb 016h		;1134	16		.
	defb 055h		;1135	55		U
	defb 017h		;1136	17		.
	defb 055h		;1137	55		U
	defb 01ah		;1138	1a		.
	defb 055h		;1139	55		U
	defb 019h		;113a	19		.
	defb 055h		;113b	55		U
	defb 018h		;113c	18		.
	defb 056h		;113d	56		V
	defb 00bh		;113e	0b		.
	defb 056h		;113f	56		V
	defb 007h		;1140	07		.
	defb 057h		;1141	57		W
	defb 00bh		;1142	0b		.
	defb 057h		;1143	57		W
	defb 007h		;1144	07		.
	defb 057h		;1145	57		W
	defb 012h		;1146	12		.
	defb 057h		;1147	57		W
	defb 013h		;1148	13		.
	defb 058h		;1149	58		X
	defb 00bh		;114a	0b		.
	defb 058h		;114b	58		X
	defb 007h		;114c	07		.
	defb 059h		;114d	59		Y
	defb 00bh		;114e	0b		.
	defb 059h		;114f	59		Y
	defb 00ch		;1150	0c		.
	defb 059h		;1151	59		Y
	defb 00dh		;1152	0d		.
	defb 059h		;1153	59		Y
	defb 00eh		;1154	0e		.
	defb 059h		;1155	59		Y
	defb 00fh		;1156	0f		.
	defb 059h		;1157	59		Y
	defb 010h		;1158	10		.
	defb 059h		;1159	59		Y
	defb 011h		;115a	11		.
	defb 05ah		;115b	5a		Z
	defb 026h		;115c	26		&
	defb 05ah		;115d	5a		Z
	defb 02bh		;115e	2b		+
	defb 05bh		;115f	5b		[
	defb 006h		;1160	06		.
	defb 05bh		;1161	5b		[
	defb 00ah		;1162	0a		.
	defb 05bh		;1163	5b		[
	defb 025h		;1164	25		%
	defb 05ch		;1165	5c		\
	defb 020h		;1166	20		 
	defb 05ch		;1167	5c		\
	defb 021h		;1168	21		!
	defb 05ch		;1169	5c		\
	defb 022h		;116a	22		"
	defb 05ch		;116b	5c		\
	defb 029h		;116c	29		)
	defb 05ch		;116d	5c		\
	defb 02ah		;116e	2a		*
	defb 05ch		;116f	5c		\
	defb 027h		;1170	27		'
	defb 05ch		;1171	5c		\
	defb 028h		;1172	28		(
	defb 05ch		;1173	5c		\
	defb 02bh		;1174	2b		+
	defb 05ch		;1175	5c		\
	defb 00bh		;1176	0b		.
	defb 05ch		;1177	5c		\
	defb 00ch		;1178	0c		.
	defb 05ch		;1179	5c		\
	defb 00dh		;117a	0d		.
	defb 05ch		;117b	5c		\
	defb 00eh		;117c	0e		.
	defb 05ch		;117d	5c		\
	defb 00fh		;117e	0f		.
	defb 05ch		;117f	5c		\
	defb 010h		;1180	10		.
	defb 05ch		;1181	5c		\
	defb 011h		;1182	11		.
	defb 05ch		;1183	5c		\
	defb 005h		;1184	05		.
	defb 05ch		;1185	5c		\
	defb 006h		;1186	06		.
	defb 05ch		;1187	5c		\
	defb 007h		;1188	07		.
	defb 05ch		;1189	5c		\
	defb 013h		;118a	13		.
	defb 05ch		;118b	5c		\
	defb 012h		;118c	12		.
	defb 05ch		;118d	5c		\
	defb 01bh		;118e	1b		.
	defb 05ch		;118f	5c		\
	defb 01ch		;1190	1c		.
	defb 05ch		;1191	5c		\
	defb 008h		;1192	08		.
	defb 059h		;1193	59		Y
	defb 022h		;1194	22		"
	defb 000h		;1195	00		.
l1196h:
	defb 049h		;1196	49		I
	defb 058h		;1197	58		X
	defb 0c8h		;1198	c8		.
	defb 049h		;1199	49		I
	defb 058h		;119a	58		X
	defb 0cch		;119b	cc		.
	defb 049h		;119c	49		I
	defb 059h		;119d	59		Y
	defb 0c8h		;119e	c8		.
	defb 049h		;119f	49		I
	defb 059h		;11a0	59		Y
	defb 0cch		;11a1	cc		.
	defb 042h		;11a2	42		B
	defb 0c3h		;11a3	c3		.
	defb 044h		;11a4	44		D
	defb 0c5h		;11a5	c5		.
	defb 048h		;11a6	48		H
	defb 0cch		;11a7	cc		.
	defb 053h		;11a8	53		S
	defb 0d0h		;11a9	d0		.
	defb 041h		;11aa	41		A
	defb 046h		;11ab	46		F
	defb 0a7h		;11ac	a7		.
	defb 041h		;11ad	41		A
	defb 0c6h		;11ae	c6		.
	defb 0c1h		;11af	c1		.
	defb 0c2h		;11b0	c2		.
	defb 0c3h		;11b1	c3		.
	defb 0c4h		;11b2	c4		.
	defb 0c5h		;11b3	c5		.
	defb 0c8h		;11b4	c8		.
	defb 0cch		;11b5	cc		.
	defb 049h		;11b6	49		I
	defb 0d8h		;11b7	d8		.
	defb 049h		;11b8	49		I
	defb 0d9h		;11b9	d9		.
	defb 04eh		;11ba	4e		N
	defb 0dah		;11bb	da		.
	defb 0dah		;11bc	da		.
	defb 04eh		;11bd	4e		N
	defb 0c3h		;11be	c3		.
	defb 0cdh		;11bf	cd		.
	defb 050h		;11c0	50		P
	defb 0cfh		;11c1	cf		.
	defb 050h		;11c2	50		P
	defb 0c5h		;11c3	c5		.
	defb 0d0h		;11c4	d0		.
	defb 0c9h		;11c5	c9		.
	defb 0d2h		;11c6	d2		.
	defb 0c6h		;11c7	c6		.
	defb 080h		;11c8	80		.
	defb 080h		;11c9	80		.
	defb 028h		;11ca	28		(
	defb 042h		;11cb	42		B
	defb 043h		;11cc	43		C
	defb 0a9h		;11cd	a9		.
	defb 028h		;11ce	28		(
	defb 044h		;11cf	44		D
	defb 045h		;11d0	45		E
	defb 0a9h		;11d1	a9		.
	defb 028h		;11d2	28		(
	defb 048h		;11d3	48		H
	defb 04ch		;11d4	4c		L
	defb 0a9h		;11d5	a9		.
	defb 028h		;11d6	28		(
	defb 049h		;11d7	49		I
	defb 058h		;11d8	58		X
	defb 0a9h		;11d9	a9		.
	defb 028h		;11da	28		(
	defb 049h		;11db	49		I
	defb 059h		;11dc	59		Y
	defb 0a9h		;11dd	a9		.
	defb 028h		;11de	28		(
	defb 053h		;11df	53		S
	defb 050h		;11e0	50		P
	defb 0a9h		;11e1	a9		.
	defb 028h		;11e2	28		(
	defb 043h		;11e3	43		C
	defb 0a9h		;11e4	a9		.
	defb 028h		;11e5	28		(
	defb 049h		;11e6	49		I
	defb 058h		;11e7	58		X
	defb 0abh		;11e8	ab		.
	defb 028h		;11e9	28		(
	defb 049h		;11ea	49		I
	defb 058h		;11eb	58		X
	defb 0adh		;11ec	ad		.
	defb 028h		;11ed	28		(
	defb 049h		;11ee	49		I
	defb 059h		;11ef	59		Y
	defb 0abh		;11f0	ab		.
	defb 028h		;11f1	28		(
	defb 049h		;11f2	49		I
	defb 059h		;11f3	59		Y
	defb 0adh		;11f4	ad		.
	defb 0a8h		;11f5	a8		.
	defb 000h		;11f6	00		.
l11f7h:
	defb 043h		;11f7	43		C
	defb 043h		;11f8	43		C
	defb 0c6h		;11f9	c6		.
	defb 043h		;11fa	43		C
	defb 050h		;11fb	50		P
	defb 0cch		;11fc	cc		.
	defb 044h		;11fd	44		D
	defb 041h		;11fe	41		A
	defb 0c1h		;11ff	c1		.
	defb 044h		;1200	44		D
	defb 0c9h		;1201	c9		.
	defb 045h		;1202	45		E
	defb 0c9h		;1203	c9		.
	defb 045h		;1204	45		E
	defb 058h		;1205	58		X
	defb 0d8h		;1206	d8		.
	defb 048h		;1207	48		H
	defb 041h		;1208	41		A
	defb 04ch		;1209	4c		L
	defb 0d4h		;120a	d4		.
	defb 04eh		;120b	4e		N
	defb 04fh		;120c	4f		O
	defb 0d0h		;120d	d0		.
	defb 052h		;120e	52		R
	defb 04ch		;120f	4c		L
l1210h:
	defb 0c1h		;1210	c1		.
	defb 052h		;1211	52		R
	defb 04ch		;1212	4c		L
	defb 043h		;1213	43		C
	defb 0c1h		;1214	c1		.
	defb 052h		;1215	52		R
	defb 052h		;1216	52		R
	defb 0c1h		;1217	c1		.
	defb 052h		;1218	52		R
	defb 052h		;1219	52		R
	defb 043h		;121a	43		C
	defb 0c1h		;121b	c1		.
	defb 053h		;121c	53		S
	defb 043h		;121d	43		C
	defb 0c6h		;121e	c6		.
	defb 04ch		;121f	4c		L
	defb 044h		;1220	44		D
	defb 049h		;1221	49		I
	defb 0d2h		;1222	d2		.
	defb 04ch		;1223	4c		L
	defb 044h		;1224	44		D
	defb 044h		;1225	44		D
	defb 0d2h		;1226	d2		.
	defb 043h		;1227	43		C
	defb 050h		;1228	50		P
	defb 049h		;1229	49		I
	defb 0d2h		;122a	d2		.
	defb 043h		;122b	43		C
	defb 050h		;122c	50		P
	defb 044h		;122d	44		D
	defb 0d2h		;122e	d2		.
	defb 049h		;122f	49		I
	defb 04eh		;1230	4e		N
	defb 049h		;1231	49		I
	defb 0d2h		;1232	d2		.
	defb 049h		;1233	49		I
	defb 04eh		;1234	4e		N
	defb 044h		;1235	44		D
	defb 0d2h		;1236	d2		.
	defb 04fh		;1237	4f		O
	defb 054h		;1238	54		T
	defb 049h		;1239	49		I
	defb 0d2h		;123a	d2		.
	defb 04fh		;123b	4f		O
	defb 054h		;123c	54		T
	defb 044h		;123d	44		D
	defb 0d2h		;123e	d2		.
	defb 04ch		;123f	4c		L
	defb 044h		;1240	44		D
	defb 0c9h		;1241	c9		.
	defb 04ch		;1242	4c		L
	defb 044h		;1243	44		D
	defb 0c4h		;1244	c4		.
	defb 043h		;1245	43		C
	defb 050h		;1246	50		P
	defb 0c9h		;1247	c9		.
	defb 043h		;1248	43		C
	defb 050h		;1249	50		P
	defb 0c4h		;124a	c4		.
	defb 049h		;124b	49		I
	defb 04eh		;124c	4e		N
	defb 0c9h		;124d	c9		.
	defb 049h		;124e	49		I
	defb 04eh		;124f	4e		N
	defb 0c4h		;1250	c4		.
	defb 04fh		;1251	4f		O
	defb 055h		;1252	55		U
	defb 054h		;1253	54		T
	defb 0c9h		;1254	c9		.
	defb 04fh		;1255	4f		O
	defb 055h		;1256	55		U
	defb 054h		;1257	54		T
	defb 0c4h		;1258	c4		.
	defb 052h		;1259	52		R
	defb 04ch		;125a	4c		L
	defb 0c4h		;125b	c4		.
	defb 052h		;125c	52		R
	defb 052h		;125d	52		R
	defb 0c4h		;125e	c4		.
	defb 052h		;125f	52		R
	defb 045h		;1260	45		E
	defb 054h		;1261	54		T
	defb 0c9h		;1262	c9		.
	defb 052h		;1263	52		R
	defb 045h		;1264	45		E
	defb 054h		;1265	54		T
	defb 0ceh		;1266	ce		.
	defb 04eh		;1267	4e		N
	defb 045h		;1268	45		E
	defb 0c7h		;1269	c7		.
	defb 080h		;126a	80		.
	defb 080h		;126b	80		.
	defb 080h		;126c	80		.
	defb 044h		;126d	44		D
	defb 045h		;126e	45		E
	defb 0c3h		;126f	c3		.
	defb 049h		;1270	49		I
	defb 04eh		;1271	4e		N
	defb 0c3h		;1272	c3		.
	defb 04ah		;1273	4a		J
	defb 0d0h		;1274	d0		.
	defb 04ah		;1275	4a		J
	defb 0d2h		;1276	d2		.
	defb 043h		;1277	43		C
	defb 041h		;1278	41		A
	defb 04ch		;1279	4c		L
	defb 0cch		;127a	cc		.
	defb 041h		;127b	41		A
	defb 044h		;127c	44		D
	defb 0c3h		;127d	c3		.
	defb 041h		;127e	41		A
	defb 044h		;127f	44		D
	defb 0c4h		;1280	c4		.
	defb 053h		;1281	53		S
	defb 042h		;1282	42		B
	defb 0c3h		;1283	c3		.
	defb 049h		;1284	49		I
	defb 0ceh		;1285	ce		.
	defb 04fh		;1286	4f		O
	defb 055h		;1287	55		U
	defb 0d4h		;1288	d4		.
	defb 045h		;1289	45		E
	defb 0d8h		;128a	d8		.
	defb 04ch		;128b	4c		L
	defb 0c4h		;128c	c4		.
	defb 04fh		;128d	4f		O
	defb 052h		;128e	52		R
	defb 0c7h		;128f	c7		.
	defb 041h		;1290	41		A
	defb 04eh		;1291	4e		N
	defb 0c4h		;1292	c4		.
	defb 04fh		;1293	4f		O
	defb 0d2h		;1294	d2		.
	defb 058h		;1295	58		X
	defb 04fh		;1296	4f		O
	defb 0d2h		;1297	d2		.
	defb 053h		;1298	53		S
	defb 055h		;1299	55		U
	defb 0c2h		;129a	c2		.
	defb 043h		;129b	43		C
	defb 0d0h		;129c	d0		.
	defb 050h		;129d	50		P
	defb 055h		;129e	55		U
	defb 053h		;129f	53		S
	defb 0c8h		;12a0	c8		.
	defb 050h		;12a1	50		P
	defb 04fh		;12a2	4f		O
	defb 0d0h		;12a3	d0		.
	defb 044h		;12a4	44		D
	defb 04ah		;12a5	4a		J
	defb 04eh		;12a6	4e		N
	defb 0dah		;12a7	da		.
	defb 052h		;12a8	52		R
	defb 045h		;12a9	45		E
	defb 0d4h		;12aa	d4		.
	defb 042h		;12ab	42		B
	defb 049h		;12ac	49		I
	defb 0d4h		;12ad	d4		.
	defb 053h		;12ae	53		S
	defb 045h		;12af	45		E
	defb 0d4h		;12b0	d4		.
	defb 052h		;12b1	52		R
	defb 045h		;12b2	45		E
	defb 0d3h		;12b3	d3		.
	defb 052h		;12b4	52		R
	defb 04ch		;12b5	4c		L
	defb 0c3h		;12b6	c3		.
	defb 052h		;12b7	52		R
	defb 0cch		;12b8	cc		.
	defb 052h		;12b9	52		R
	defb 052h		;12ba	52		R
	defb 0c3h		;12bb	c3		.
	defb 052h		;12bc	52		R
	defb 0d2h		;12bd	d2		.
	defb 053h		;12be	53		S
	defb 04ch		;12bf	4c		L
	defb 0c1h		;12c0	c1		.
	defb 053h		;12c1	53		S
	defb 052h		;12c2	52		R
	defb 0c1h		;12c3	c1		.
	defb 053h		;12c4	53		S
	defb 052h		;12c5	52		R
	defb 0cch		;12c6	cc		.
	defb 049h		;12c7	49		I
	defb 0cdh		;12c8	cd		.
	defb 052h		;12c9	52		R
	defb 053h		;12ca	53		S
	defb 0d4h		;12cb	d4		.
	defb 044h		;12cc	44		D
	defb 0c2h		;12cd	c2		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; OpcodeClassTable: class/length/control-flow bytes for every
; opcode (including the xxCB pages) - input to
; AnalyzeInstruction.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
OpcodeClassTable:
	defb 053h		;12ce	53		S
	defb 04ch		;12cf	4c		L
	defb 0c9h		;12d0	c9		.
	defb 000h		;12d1	00		.
	defb 007h		;12d2	07		.
	defb 001h		;12d3	01		.
	defb 05eh		;12d4	5e		^
	defb 013h		;12d5	13		.
	defb 038h		;12d6	38		8
	defb 001h		;12d7	01		.
	defb 05eh		;12d8	5e		^
	defb 013h		;12d9	13		.
	defb 038h		;12da	38		8
	defb 001h		;12db	01		.
	defb 066h		;12dc	66		f
	defb 013h		;12dd	13		.
	defb 030h		;12de	30		0
	defb 001h		;12df	01		.
	defb 06eh		;12e0	6e		n
	defb 013h		;12e1	13		.
	defb 030h		;12e2	30		0
	defb 001h		;12e3	01		.
	defb 072h		;12e4	72		r
	defb 013h		;12e5	13		.
	defb 038h		;12e6	38		8
	defb 001h		;12e7	01		.
	defb 076h		;12e8	76		v
	defb 013h		;12e9	13		.
	defb 038h		;12ea	38		8
	defb 001h		;12eb	01		.
	defb 07eh		;12ec	7e		~
	defb 013h		;12ed	13		.
	defb 038h		;12ee	38		8
	defb 001h		;12ef	01		.
	defb 086h		;12f0	86		.
	defb 013h		;12f1	13		.
	defb 0c0h		;12f2	c0		.
	defb 002h		;12f3	02		.
	defb 08eh		;12f4	8e		.
	defb 013h		;12f5	13		.
	defb 038h		;12f6	38		8
	defb 001h		;12f7	01		.
	defb 096h		;12f8	96		.
	defb 013h		;12f9	13		.
	defb 038h		;12fa	38		8
	defb 001h		;12fb	01		.
	defb 09eh		;12fc	9e		.
	defb 013h		;12fd	13		.
	defb 00fh		;12fe	0f		.
	defb 002h		;12ff	02		.
	defb 0a6h		;1300	a6		.
	defb 013h		;1301	13		.
	defb 038h		;1302	38		8
	defb 002h		;1303	02		.
	defb 0c6h		;1304	c6		.
	defb 013h		;1305	13		.
	defb 038h		;1306	38		8
	defb 001h		;1307	01		.
	defb 0d6h		;1308	d6		.
	defb 013h		;1309	13		.
	defb 030h		;130a	30		0
	defb 001h		;130b	01		.
	defb 0deh		;130c	de		.
	defb 013h		;130d	13		.
	defb 030h		;130e	30		0
	defb 002h		;130f	02		.
	defb 0e2h		;1310	e2		.
	defb 013h		;1311	13		.
	defb 030h		;1312	30		0
	defb 002h		;1313	02		.
	defb 0eah		;1314	ea		.
	defb 013h		;1315	13		.
	defb 00fh		;1316	0f		.
	defb 002h		;1317	02		.
	defb 0f2h		;1318	f2		.
	defb 013h		;1319	13		.
	defb 038h		;131a	38		8
	defb 002h		;131b	02		.
	defb 012h		;131c	12		.
	defb 014h		;131d	14		.
	defb 030h		;131e	30		0
	defb 002h		;131f	02		.
	defb 022h		;1320	22		"
	defb 014h		;1321	14		.
	defb 030h		;1322	30		0
	defb 002h		;1323	02		.
	defb 02ah		;1324	2a		*
	defb 014h		;1325	14		.
	defb 0c0h		;1326	c0		.
	defb 002h		;1327	02		.
	defb 032h		;1328	32		2
	defb 014h		;1329	14		.
	defb 038h		;132a	38		8
	defb 001h		;132b	01		.
	defb 03ah		;132c	3a		:
	defb 014h		;132d	14		.
	defb 0e0h		;132e	e0		.
	defb 001h		;132f	01		.
	defb 042h		;1330	42		B
	defb 014h		;1331	14		.
	defb 00fh		;1332	0f		.
	defb 002h		;1333	02		.
	defb 04ah		;1334	4a		J
	defb 014h		;1335	14		.
	defb 007h		;1336	07		.
	defb 001h		;1337	01		.
	defb 06ah		;1338	6a		j
	defb 014h		;1339	14		.
	defb 038h		;133a	38		8
	defb 001h		;133b	01		.
	defb 072h		;133c	72		r
	defb 014h		;133d	14		.
	defb 038h		;133e	38		8
	defb 001h		;133f	01		.
	defb 07ah		;1340	7a		z
	defb 014h		;1341	14		.
	defb 038h		;1342	38		8
	defb 001h		;1343	01		.
	defb 082h		;1344	82		.
	defb 014h		;1345	14		.
	defb 038h		;1346	38		8
	defb 001h		;1347	01		.
	defb 08ah		;1348	8a		.
	defb 014h		;1349	14		.
	defb 038h		;134a	38		8
	defb 002h		;134b	02		.
	defb 092h		;134c	92		.
	defb 014h		;134d	14		.
	defb 018h		;134e	18		.
	defb 001h		;134f	01		.
	defb 0a2h		;1350	a2		.
	defb 014h		;1351	14		.
	defb 018h		;1352	18		.
	defb 001h		;1353	01		.
	defb 0a6h		;1354	a6		.
	defb 014h		;1355	14		.
	defb 018h		;1356	18		.
	defb 001h		;1357	01		.
	defb 0aah		;1358	aa		.
	defb 014h		;1359	14		.
	defb 018h		;135a	18		.
	defb 001h		;135b	01		.
	defb 0aeh		;135c	ae		.
	defb 014h		;135d	14		.
	defb 00ch		;135e	0c		.
	defb 00dh		;135f	0d		.
	defb 00eh		;1360	0e		.
	defb 00fh		;1361	0f		.
	defb 010h		;1362	10		.
	defb 011h		;1363	11		.
	defb 022h		;1364	22		"
	defb 00bh		;1365	0b		.
	defb 00ch		;1366	0c		.
	defb 00dh		;1367	0d		.
	defb 00eh		;1368	0e		.
	defb 00fh		;1369	0f		.
	defb 010h		;136a	10		.
	defb 011h		;136b	11		.
	defb 0eah		;136c	ea		.
	defb 00bh		;136d	0b		.
	defb 005h		;136e	05		.
	defb 006h		;136f	06		.
	defb 007h		;1370	07		.
	defb 008h		;1371	08		.
	defb 005h		;1372	05		.
	defb 006h		;1373	06		.
	defb 007h		;1374	07		.
	defb 00ah		;1375	0a		.
	defb 014h		;1376	14		.
	defb 015h		;1377	15		.
	defb 016h		;1378	16		.
	defb 00dh		;1379	0d		.
	defb 018h		;137a	18		.
	defb 019h		;137b	19		.
	defb 01ah		;137c	1a		.
	defb 017h		;137d	17		.
	defb 083h		;137e	83		.
	defb 082h		;137f	82		.
	defb 085h		;1380	85		.
	defb 084h		;1381	84		.
	defb 089h		;1382	89		.
	defb 088h		;1383	88		.
	defb 087h		;1384	87		.
	defb 086h		;1385	86		.
	defb 092h		;1386	92		.
	defb 091h		;1387	91		.
	defb 094h		;1388	94		.
	defb 093h		;1389	93		.
	defb 098h		;138a	98		.
	defb 097h		;138b	97		.
	defb 096h		;138c	96		.
	defb 095h		;138d	95		.
	defb 0d2h		;138e	d2		.
	defb 000h		;138f	00		.
	defb 0d0h		;1390	d0		.
	defb 0c7h		;1391	c7		.
	defb 0d1h		;1392	d1		.
	defb 0c7h		;1393	c7		.
	defb 0d8h		;1394	d8		.
	defb 000h		;1395	00		.
	defb 0b6h		;1396	b6		.
	defb 0b7h		;1397	b7		.
	defb 0b8h		;1398	b8		.
	defb 0b9h		;1399	b9		.
	defb 0bah		;139a	ba		.
	defb 0bbh		;139b	bb		.
	defb 0afh		;139c	af		.
	defb 0b5h		;139d	b5		.
	defb 09bh		;139e	9b		.
	defb 099h		;139f	99		.
	defb 061h		;13a0	61		a
	defb 09fh		;13a1	9f		.
	defb 05eh		;13a2	5e		^
	defb 060h		;13a3	60		`
	defb 05fh		;13a4	5f		_
	defb 062h		;13a5	62		b
	defb 0d3h		;13a6	d3		.
	defb 000h		;13a7	00		.
	defb 0d5h		;13a8	d5		.
	defb 0ebh		;13a9	eb		.
	defb 0d6h		;13aa	d6		.
	defb 000h		;13ab	00		.
	defb 052h		;13ac	52		R
	defb 0cah		;13ad	ca		.
	defb 052h		;13ae	52		R
	defb 0c8h		;13af	c8		.
	defb 051h		;13b0	51		Q
	defb 0c8h		;13b1	c8		.
	defb 0d0h		;13b2	d0		.
	defb 0ech		;13b3	ec		.
	defb 0d4h		;13b4	d4		.
	defb 000h		;13b5	00		.
	defb 0d3h		;13b6	d3		.
	defb 000h		;13b7	00		.
	defb 09ch		;13b8	9c		.
	defb 0cah		;13b9	ca		.
	defb 0d7h		;13ba	d7		.
	defb 000h		;13bb	00		.
	defb 051h		;13bc	51		Q
	defb 0cah		;13bd	ca		.
	defb 052h		;13be	52		R
	defb 0c8h		;13bf	c8		.
	defb 051h		;13c0	51		Q
	defb 0c8h		;13c1	c8		.
	defb 0d0h		;13c2	d0		.
	defb 0ech		;13c3	ec		.
	defb 0d4h		;13c4	d4		.
	defb 000h		;13c5	00		.
	defb 033h		;13c6	33		3
	defb 000h		;13c7	00		.
	defb 0abh		;13c8	ab		.
	defb 009h		;13c9	09		.
	defb 065h		;13ca	65		e
	defb 0edh		;13cb	ed		.
	defb 054h		;13cc	54		T
	defb 0edh		;13cd	ed		.
	defb 08eh		;13ce	8e		.
	defb 0edh		;13cf	ed		.
	defb 08dh		;13d0	8d		.
	defb 0edh		;13d1	ed		.
	defb 090h		;13d2	90		.
	defb 0edh		;13d3	ed		.
	defb 08fh		;13d4	8f		.
	defb 0edh		;13d5	ed		.
	defb 035h		;13d6	35		5
	defb 037h		;13d7	37		7
	defb 034h		;13d8	34		4
	defb 036h		;13d9	36		6
	defb 02eh		;13da	2e		.
	defb 02dh		;13db	2d		-
	defb 038h		;13dc	38		8
	defb 02ch		;13dd	2c		,
	defb 0bch		;13de	bc		.
	defb 0bdh		;13df	bd		.
	defb 0beh		;13e0	be		.
	defb 0c3h		;13e1	c3		.
	defb 0adh		;13e2	ad		.
	defb 00bh		;13e3	0b		.
	defb 0aeh		;13e4	ae		.
	defb 00bh		;13e5	0b		.
	defb 0b4h		;13e6	b4		.
	defb 007h		;13e7	07		.
	defb 0b4h		;13e8	b4		.
	defb 00bh		;13e9	0b		.
	defb 0b5h		;13ea	b5		.
	defb 020h		;13eb	20		 
	defb 0b5h		;13ec	b5		.
	defb 021h		;13ed	21		!
	defb 0beh		;13ee	be		.
	defb 02bh		;13ef	2b		+
	defb 0b5h		;13f0	b5		.
	defb 02bh		;13f1	2b		+
	defb 066h		;13f2	66		f
	defb 0cch		;13f3	cc		.
	defb 064h		;13f4	64		d
	defb 0cbh		;13f5	cb		.
	defb 0cdh		;13f6	cd		.
	defb 0ebh		;13f7	eb		.
	defb 0d9h		;13f8	d9		.
	defb 000h		;13f9	00		.
	defb 0ceh		;13fa	ce		.
	defb 0ebh		;13fb	eb		.
	defb 063h		;13fc	63		c
	defb 0cbh		;13fd	cb		.
	defb 0d1h		;13fe	d1		.
	defb 0ech		;13ff	ec		.
	defb 072h		;1400	72		r
	defb 000h		;1401	00		.
	defb 066h		;1402	66		f
	defb 0cch		;1403	cc		.
	defb 0dah		;1404	da		.
	defb 000h		;1405	00		.
	defb 0cdh		;1406	cd		.
	defb 0ebh		;1407	eb		.
	defb 0d9h		;1408	d9		.
	defb 000h		;1409	00		.
	defb 0ceh		;140a	ce		.
	defb 0ebh		;140b	eb		.
	defb 0dbh		;140c	db		.
	defb 000h		;140d	00		.
	defb 0d1h		;140e	d1		.
	defb 0ech		;140f	ec		.
	defb 072h		;1410	72		r
	defb 000h		;1411	00		.
	defb 053h		;1412	53		S
	defb 0ebh		;1413	eb		.
	defb 0eah		;1414	ea		.
	defb 000h		;1415	00		.
	defb 0a9h		;1416	a9		.
	defb 00bh		;1417	0b		.
	defb 0a1h		;1418	a1		.
	defb 02bh		;1419	2b		+
	defb 0ach		;141a	ac		.
	defb 007h		;141b	07		.
	defb 0eah		;141c	ea		.
	defb 000h		;141d	00		.
	defb 02fh		;141e	2f		/
	defb 000h		;141f	00		.
	defb 030h		;1420	30		0
	defb 000h		;1421	00		.
	defb 066h		;1422	66		f
	defb 000h		;1423	00		.
	defb 031h		;1424	31		1
	defb 000h		;1425	00		.
	defb 08ah		;1426	8a		.
	defb 000h		;1427	00		.
	defb 0c3h		;1428	c3		.
	defb 007h		;1429	07		.
	defb 055h		;142a	55		U
	defb 0ebh		;142b	eb		.
	defb 0eah		;142c	ea		.
	defb 000h		;142d	00		.
	defb 0eah		;142e	ea		.
	defb 000h		;142f	00		.
	defb 0eah		;1430	ea		.
	defb 000h		;1431	00		.
	defb 0ddh		;1432	dd		.
	defb 0c7h		;1433	c7		.
	defb 067h		;1434	67		g
	defb 0c7h		;1435	c7		.
	defb 069h		;1436	69		i
	defb 0c7h		;1437	c7		.
	defb 068h		;1438	68		h
	defb 0c7h		;1439	c7		.
	defb 06ah		;143a	6a		j
	defb 06ch		;143b	6c		l
	defb 06bh		;143c	6b		k
	defb 06dh		;143d	6d		m
	defb 06eh		;143e	6e		n
	defb 06fh		;143f	6f		o
	defb 074h		;1440	74		t
	defb 070h		;1441	70		p
	defb 0eah		;1442	ea		.
	defb 0eah		;1443	ea		.
	defb 0dfh		;1444	df		.
	defb 0dfh		;1445	df		.
	defb 0eah		;1446	ea		.
	defb 0e0h		;1447	e0		.
	defb 0eah		;1448	ea		.
	defb 0eah		;1449	ea		.
	defb 0e1h		;144a	e1		.
	defb 026h		;144b	26		&
	defb 0a8h		;144c	a8		.
	defb 0c8h		;144d	c8		.
	defb 0a0h		;144e	a0		.
	defb 0cah		;144f	ca		.
	defb 0b4h		;1450	b4		.
	defb 0cah		;1451	ca		.
	defb 0e2h		;1452	e2		.
	defb 000h		;1453	00		.
	defb 0e3h		;1454	e3		.
	defb 000h		;1455	00		.
	defb 0e4h		;1456	e4		.
	defb 000h		;1457	00		.
	defb 0e5h		;1458	e5		.
	defb 000h		;1459	00		.
	defb 0e1h		;145a	e1		.
	defb 026h		;145b	26		&
	defb 0a8h		;145c	a8		.
	defb 0c8h		;145d	c8		.
	defb 09ah		;145e	9a		.
	defb 0cah		;145f	ca		.
	defb 0d5h		;1460	d5		.
	defb 02bh		;1461	2b		+
	defb 0e2h		;1462	e2		.
	defb 000h		;1463	00		.
	defb 0e3h		;1464	e3		.
	defb 000h		;1465	00		.
	defb 0e4h		;1466	e4		.
	defb 000h		;1467	00		.
	defb 0e5h		;1468	e5		.
	defb 000h		;1469	00		.
	defb 0e6h		;146a	e6		.
	defb 0e7h		;146b	e7		.
	defb 0e8h		;146c	e8		.
	defb 0e9h		;146d	e9		.
	defb 0eah		;146e	ea		.
	defb 0eah		;146f	ea		.
	defb 0eah		;1470	ea		.
	defb 0eah		;1471	ea		.
	defb 0a2h		;1472	a2		.
	defb 0a3h		;1473	a3		.
	defb 0a4h		;1474	a4		.
	defb 0a5h		;1475	a5		.
	defb 0a6h		;1476	a6		.
	defb 0a7h		;1477	a7		.
	defb 0c4h		;1478	c4		.
	defb 0a1h		;1479	a1		.
	defb 04dh		;147a	4d		M
	defb 04eh		;147b	4e		N
	defb 04eh		;147c	4e		N
	defb 04eh		;147d	4e		N
	defb 04eh		;147e	4e		N
	defb 04eh		;147f	4e		N
	defb 04eh		;1480	4e		N
	defb 04eh		;1481	4e		N
	defb 04ch		;1482	4c		L
	defb 04bh		;1483	4b		K
	defb 04fh		;1484	4f		O
	defb 04fh		;1485	4f		O
	defb 04fh		;1486	4f		O
	defb 04fh		;1487	4f		O
	defb 04fh		;1488	4f		O
	defb 04fh		;1489	4f		O
	defb 071h		;148a	71		q
	defb 071h		;148b	71		q
	defb 071h		;148c	71		q
	defb 071h		;148d	71		q
	defb 071h		;148e	71		q
	defb 071h		;148f	71		q
	defb 071h		;1490	71		q
	defb 071h		;1491	71		q
	defb 0c1h		;1492	c1		.
	defb 00bh		;1493	0b		.
	defb 0c2h		;1494	c2		.
	defb 00bh		;1495	0b		.
	defb 0b5h		;1496	b5		.
	defb 01bh		;1497	1b		.
	defb 0b5h		;1498	b5		.
	defb 01ch		;1499	1c		.
	defb 04ah		;149a	4a		J
	defb 000h		;149b	00		.
	defb 049h		;149c	49		I
	defb 000h		;149d	00		.
	defb 050h		;149e	50		P
	defb 000h		;149f	00		.
	defb 050h		;14a0	50		P
	defb 000h		;14a1	00		.
	defb 041h		;14a2	41		A
	defb 042h		;14a3	42		B
	defb 039h		;14a4	39		9
	defb 03ah		;14a5	3a		:
	defb 043h		;14a6	43		C
	defb 044h		;14a7	44		D
	defb 03bh		;14a8	3b		;
	defb 03ch		;14a9	3c		<
	defb 045h		;14aa	45		E
	defb 046h		;14ab	46		F
	defb 03dh		;14ac	3d		=
	defb 03eh		;14ad	3e		>
	defb 047h		;14ae	47		G
	defb 048h		;14af	48		H
	defb 03fh		;14b0	3f		?
	defb 040h		;14b1	40		@
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EditorKeyTable: key code -> editor action (see
; EditorKeyLoop $1AA7).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EditorKeyTable:
	defb 04eh		;14b2	4e		N
	defb 074h		;14b3	74		t
	defb 0bbh		;14b4	bb		.
	defb 0bah		;14b5	ba		.
	defb 011h		;14b6	11		.
	defb 010h		;14b7	10		.
	defb 031h		;14b8	31		1
	defb 0abh		;14b9	ab		.
	defb 018h		;14ba	18		.
	defb 019h		;14bb	19		.
	defb 01ah		;14bc	1a		.
	defb 017h		;14bd	17		.
	defb 016h		;14be	16		.
	defb 00dh		;14bf	0d		.
	defb 014h		;14c0	14		.
	defb 015h		;14c1	15		.
	defb 066h		;14c2	66		f
	defb 04ch		;14c3	4c		L
	defb 04bh		;14c4	4b		K
	defb 007h		;14c5	07		.
	defb 0beh		;14c6	be		.
	defb 08ah		;14c7	8a		.
	defb 09ch		;14c8	9c		.
	defb 022h		;14c9	22		"
	defb 0afh		;14ca	af		.
l14cbh:
	defb 071h		;14cb	71		q
	defb 072h		;14cc	72		r
	defb 067h		;14cd	67		g
	defb 068h		;14ce	68		h
	defb 069h		;14cf	69		i
	defb 02bh		;14d0	2b		+
	defb 0b4h		;14d1	b4		.
	defb 0a9h		;14d2	a9		.
	defb 0ech		;14d3	ec		.
	defb 0ebh		;14d4	eb		.
	defb 0edh		;14d5	ed		.
	defb 0b2h		;14d6	b2		.
	defb 027h		;14d7	27		'
	defb 09dh		;14d8	9d		.
	defb 08bh		;14d9	8b		.
	defb 0c0h		;14da	c0		.
	defb 012h		;14db	12		.
	defb 0b0h		;14dc	b0		.
	defb 029h		;14dd	29		)
	defb 09eh		;14de	9e		.
	defb 08ch		;14df	8c		.
	defb 0bfh		;14e0	bf		.
	defb 013h		;14e1	13		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegisterKeyTable: register-edit keys (see EditorRegKeys
; $1BD1).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RegisterKeyTable:
	defb 049h		;14e2	49		I
	defb 04ah		;14e3	4a		J
	defb 0b4h		;14e4	b4		.
	defb 0aeh		;14e5	ae		.
	defb 0adh		;14e6	ad		.
	defb 0afh		;14e7	af		.
	defb 02bh		;14e8	2b		+
	defb 021h		;14e9	21		!
	defb 020h		;14ea	20		 
	defb 022h		;14eb	22		"
l14ech:
	defb 052h		;14ec	52		R
	defb 051h		;14ed	51		Q
	defb 06ah		;14ee	6a		j
	defb 06ch		;14ef	6c		l
	defb 06bh		;14f0	6b		k
	defb 06dh		;14f1	6d		m
	defb 06eh		;14f2	6e		n
	defb 06fh		;14f3	6f		o
	defb 070h		;14f4	70		p
	defb 074h		;14f5	74		t
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CharClasses: character-class predicates (hex digit,
; separator, ...) used by parsers and the disassembler.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MnemonicTable_end:
CharClasses:
	cp 07eh			;14f6	fe 7e		. ~
	ret c			;14f8	d8		.
	cp 0c5h			;14f9	fe c5		. .
	ccf			;14fb	3f		?
	ret			;14fc	c9		.
sub_14fdh:
	cp 02ch			;14fd	fe 2c		. ,
	ret c			;14ff	d8		.
	cp 075h			;1500	fe 75		. u
	ccf			;1502	3f		?
	ret			;1503	c9		.
sub_1504h:
	cp 02ch			;1504	fe 2c		. ,
	ret c			;1506	d8		.
	cp 0c5h			;1507	fe c5		. .
	ccf			;1509	3f		?
	ret			;150a	c9		.
sub_150bh:
	cp 001h			;150b	fe 01		. .
	ret c			;150d	d8		.
	cp 02ch			;150e	fe 2c		. ,
	ccf			;1510	3f		?
	ret			;1511	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Disassemble - the mnemonic printer core.  Mnemonic strings
; sit in MnemonicTable ($1107) with high-bit placeholder
; characters marking where operands splice in; this walk
; decodes operands (16-bit, 8-bit, indexed, condition codes)
; and emits the line via RST 10h.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Disassemble:
	res 6,(iy+00eh)		;1512	fd cb 0e b6	. . . .
	call PrintSpace		;1516	cd 10 2b	. . +
	ld a,(hl)		;1519	7e		~
	call MnemonicTable_end	;151a	cd f6 14	. . .
	jr c,l1538h		;151d	38 19		8 .
	push hl			;151f	e5		.
	ld hl,MnemonicTable	;1520	21 07 11	! . .
	sub 07eh		;1523	d6 7e		. ~
	add a,a			;1525	87		.
	ld e,a			;1526	5f		_
	ld d,000h		;1527	16 00		. .
	add hl,de		;1529	19		.
	ld a,(hl)		;152a	7e		~
	call sub_1570h		;152b	cd 70 15	. p .
	ld a,(hl)		;152e	7e		~
	and 07fh		;152f	e6 7f		. .
	pop hl			;1531	e1		.
	inc hl			;1532	23		#
	call sub_157fh		;1533	cd 7f 15	. . .
	jr l1543h		;1536	18 0b		. .
l1538h:
	call sub_14fdh		;1538	cd fd 14	. . .
	jr c,l1548h		;153b	38 0b		8 .
	call sub_1570h		;153d	cd 70 15	. p .
l1540h:
	call sub_1599h		;1540	cd 99 15	. . .
l1543h:
	ld a,(hl)		;1543	7e		~
	call sub_1504h		;1544	cd 04 15	. . .
	ret nc			;1547	d0		.
l1548h:
	cp 0f1h			;1548	fe f1		. .
	ret z			;154a	c8		.
	cp 0f7h			;154b	fe f7		. .
	ret z			;154d	c8		.
	cp 0eeh			;154e	fe ee		. .
	jr nz,l1561h		;1550	20 0f		  .
	call PrintSpace		;1552	cd 10 2b	. . +
	ld a,03bh		;1555	3e 3b		> ;
	rst 10h			;1557	d7		.
	inc hl			;1558	23		#
l1559h:
	ld a,(hl)		;1559	7e		~
	inc hl			;155a	23		#
	cp 0f0h			;155b	fe f0		. .
	ret z			;155d	c8		.
	rst 10h			;155e	d7		.
	jr l1559h		;155f	18 f8		. .
l1561h:
	bit 6,(iy+00eh)		;1561	fd cb 0e 76	. . . v
	set 6,(iy+00eh)		;1565	fd cb 0e f6	. . . .
	jr nz,l1540h		;1569	20 d5		  .
	ld a,02ch		;156b	3e 2c		> ,
	rst 10h			;156d	d7		.
	jr l1540h		;156e	18 d0		. .
sub_1570h:
	ld c,(iy+006h)		;1570	fd 4e 06	. N .
	sub 02ch		;1573	d6 2c		. ,
	ld de,l11f7h		;1575	11 f7 11	. . .
	call sub_15fch		;1578	cd fc 15	. . .
	inc hl			;157b	23		#
	jp PrintSpace		;157c	c3 10 2b	. . +
sub_157fh:
	call sub_150bh		;157f	cd 0b 15	. . .
	jr c,l159dh		;1582	38 19		8 .
	push af			;1584	f5		.
	sub 001h		;1585	d6 01		. .
	ld de,l1196h		;1587	11 96 11	. . .
	call sub_15fah		;158a	cd fa 15	. . .
	pop af			;158d	f1		.
	cp 027h			;158e	fe 27		. '
	jr c,l15b1h		;1590	38 1f		8 .
	call sub_1599h		;1592	cd 99 15	. . .
	ld a,029h		;1595	3e 29		> )
	rst 10h			;1597	d7		.
	ret			;1598	c9		.
sub_1599h:
	ld a,(hl)		;1599	7e		~
	inc hl			;159a	23		#
	jr sub_157fh		;159b	18 e2		. .
l159dh:
	cp 0f8h			;159d	fe f8		. .
	jr c,l15a6h		;159f	38 05		8 .
	call PickNumberBase	;15a1	cd 26 16	. & .
	jr l15b1h		;15a4	18 0b		. .
l15a6h:
	cp 0ech			;15a6	fe ec		. .
	jr nz,l15afh		;15a8	20 05		  .
	call sub_15dbh		;15aa	cd db 15	. . .
	jr l15b1h		;15ad	18 02		. .
l15afh:
	dec hl			;15af	2b		+
	ret			;15b0	c9		.
l15b1h:
	ld a,(hl)		;15b1	7e		~
	ret			;15b2	c9		.
l15b3h:
	ld a,(hl)		;15b3	7e		~
	rst 10h			;15b4	d7		.
	inc hl			;15b5	23		#
	djnz l15b3h		;15b6	10 fb		. .
	ret			;15b8	c9		.
l15b9h:
	ld a,(hl)		;15b9	7e		~
	cp 020h			;15ba	fe 20		.  
	jr nc,l15c0h		;15bc	30 02		0 .
	ld a,03fh		;15be	3e 3f		> ?
l15c0h:
	call PrintCharBit7	;15c0	cd 0b 2b	. . +
	inc hl			;15c3	23		#
	djnz l15b9h		;15c4	10 f3		. .
	ret			;15c6	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintBit7Chars: print a string whose last character carries
; bit 7 (the RST 20h message convention).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintBit7Chars:
	ld a,(hl)		;15c7	7e		~
	call PrintCharBit7	;15c8	cd 0b 2b	. . +
	inc hl			;15cb	23		#
	rlca			;15cc	07		.
	ret c			;15cd	d8		.
	jr PrintBit7Chars	;15ce	18 f7		. .
	inc a			;15d0	3c		<
l15d1h:
	dec a			;15d1	3d		=
	jr z,sub_15dbh		;15d2	28 07		( .
l15d4h:
	bit 7,(hl)		;15d4	cb 7e		. ~
	inc hl			;15d6	23		#
	jr z,l15d4h		;15d7	28 fb		( .
	jr l15d1h		;15d9	18 f6		. .
sub_15dbh:
	ld b,001h		;15db	06 01		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintJustifiedHeader: window headers padded to window width.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintJustifiedHeader:
	or a			;15dd	b7		.
l15deh:
	ld a,(hl)		;15de	7e		~
	bit 7,a			;15df	cb 7f		. .
	call PrintCharBit7	;15e1	cd 0b 2b	. . +
	inc hl			;15e4	23		#
	jr z,l15ech		;15e5	28 05		( .
	ret c			;15e7	d8		.
	ld a,020h		;15e8	3e 20		>  
	jr l15f6h		;15ea	18 0a		. .
l15ech:
	djnz l15deh		;15ec	10 f0		. .
	scf			;15ee	37		7
	ld b,000h		;15ef	06 00		. .
	jr l15deh		;15f1	18 eb		. .
l15f3h:
	ld a,020h		;15f3	3e 20		>  
l15f5h:
	rst 10h			;15f5	d7		.
l15f6h:
	djnz l15f5h		;15f6	10 fd		. .
	or a			;15f8	b7		.
	ret			;15f9	c9		.
sub_15fah:
	ld c,001h		;15fa	0e 01		. .
sub_15fch:
	ex de,hl		;15fc	eb		.
	call sub_1606h		;15fd	cd 06 16	. . .
	ld b,c			;1600	41		A
	call PrintJustifiedHeader	;1601	cd dd 15	. . .
	ex de,hl		;1604	eb		.
	ret			;1605	c9		.
sub_1606h:
	or a			;1606	b7		.
	ret z			;1607	c8		.
	ld b,a			;1608	47		G
l1609h:
	bit 7,(hl)		;1609	cb 7e		. ~
	inc hl			;160b	23		#
	jp z,l1609h		;160c	ca 09 16	. . .
	djnz l1609h		;160f	10 f8		. .
	ret			;1611	c9		.
sub_1612h:
	ld c,l			;1612	4d		M
	ld b,h			;1613	44		D
sub_1614h:
	push de			;1614	d5		.
	bit 0,(iy+005h)		;1615	fd cb 05 46	. . . F
	ld de,l0a00h		;1619	11 00 0a	. . .
	jr nz,l1621h		;161c	20 03		  .
	ld de,01023h		;161e	11 23 10	. # .
l1621h:
	call sub_162ch		;1621	cd 2c 16	. , .
	pop de			;1624	d1		.
	ret			;1625	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PickNumberBase: choose hex/decimal/binary presentation from
; the monitor flags before printing numbers.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PickNumberBase:
	call sub_1638h		;1626	cd 38 16	. 8 .
sub_1629h:
	call sub_1642h		;1629	cd 42 16	. B .
sub_162ch:
	ld a,d			;162c	7a		z
	ld (0e024h),a		;162d	32 24 e0	2 $ .
	ld a,e			;1630	7b		{
	rst 10h			;1631	d7		.
	ld (iy+007h),001h	;1632	fd 36 07 01	. 6 . .
	jr l1673h		;1636	18 3b		. ;
sub_1638h:
	ld b,000h		;1638	06 00		. .
	ld c,(hl)		;163a	4e		N
	inc hl			;163b	23		#
	bit 0,a			;163c	cb 47		. G
	ret nz			;163e	c0		.
	ld b,(hl)		;163f	46		F
	inc hl			;1640	23		#
	ret			;1641	c9		.
sub_1642h:
	and 006h		;1642	e6 06		. .
	ld de,l0225h		;1644	11 25 02	. % .
	cp 002h			;1647	fe 02		. .
	ret c			;1649	d8		.
	ld de,00840h		;164a	11 40 08	. @ .
	ret z			;164d	c8		.
	cp 006h			;164e	fe 06		. .
	ld de,l0a00h		;1650	11 00 0a	. . .
	ret c			;1653	d8		.
	ld de,01023h		;1654	11 23 10	. # .
	ret			;1657	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintDecimal: recursive divider method - DE=$000A, divide,
; print remainders (recursion state at IY+07).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintDecimal:
	ld c,a			;1658	4f		O
	ld a,b			;1659	78		x
	ld b,000h		;165a	06 00		. .
	jr l166bh		;165c	18 0d		. .
	scf			;165e	37		7
	jr l1662h		;165f	18 01		. .
sub_1661h:
	or a			;1661	b7		.
l1662h:
	ld a,020h		;1662	3e 20		>  
	rst 10h			;1664	d7		.
	ld a,001h		;1665	3e 01		> .
	jr c,l166bh		;1667	38 02		8 .
sub_1669h:
	ld a,005h		;1669	3e 05		> .
l166bh:
	ld (iy+007h),a		;166b	fd 77 07	. w .
	ld de,Rst08Vector+2	;166e	11 0a 00	. . .
	jr l1677h		;1671	18 04		. .
l1673h:
	ld de,(0e024h)		;1673	ed 5b 24 e0	. [ $ .
l1677h:
	push hl			;1677	e5		.
	call sub_1681h		;1678	cd 81 16	. . .
	pop hl			;167b	e1		.
	ret			;167c	c9		.
sub_167dh:
	ld a,b			;167d	78		x
	or c			;167e	b1		.
	jr z,l169eh		;167f	28 1d		( .
sub_1681h:
	dec (iy+007h)		;1681	fd 35 07	. 5 .
	ld a,b			;1684	78		x
	call Divide16		;1685	cd aa 16	. . .
	push hl			;1688	e5		.
	call sub_167dh		;1689	cd 7d 16	. } .
	pop hl			;168c	e1		.
	ld a,l			;168d	7d		}
	add a,090h		;168e	c6 90		. .
	daa			;1690	27		'
	adc a,040h		;1691	ce 40		. @
	daa			;1693	27		'
	and 07fh		;1694	e6 7f		. .
	cp 020h			;1696	fe 20		.  
	jr nc,l169ch		;1698	30 02		0 .
	ld a,020h		;169a	3e 20		>  
l169ch:
	rst 10h			;169c	d7		.
	ret			;169d	c9		.
l169eh:
	dec (iy+007h)		;169e	fd 35 07	. 5 .
	ret m			;16a1	f8		.
	call sub_167dh		;16a2	cd 7d 16	. } .
	ld a,(iy+008h)		;16a5	fd 7e 08	. ~ .
	rst 10h			;16a8	d7		.
	ret			;16a9	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Divide16: 16-bit division helper behind all number output.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Divide16:
	ld hl,WriteAnyBankByte	;16aa	21 00 00	! . .
	ld b,010h		;16ad	06 10		. .
l16afh:
	rl c			;16af	cb 11		. .
	rla			;16b1	17		.
	adc hl,hl		;16b2	ed 6a		. j
	sbc hl,de		;16b4	ed 52		. R
	ccf			;16b6	3f		?
	jr nc,l16c8h		;16b7	30 0f		0 .
l16b9h:
	djnz l16afh		;16b9	10 f4		. .
	rl c			;16bb	cb 11		. .
	rla			;16bd	17		.
	ld b,a			;16be	47		G
	ret			;16bf	c9		.
l16c0h:
	rl c			;16c0	cb 11		. .
	rla			;16c2	17		.
	adc hl,hl		;16c3	ed 6a		. j
	add hl,de		;16c5	19		.
	jr c,l16b9h		;16c6	38 f1		8 .
l16c8h:
	djnz l16c0h		;16c8	10 f6		. .
	rl c			;16ca	cb 11		. .
	rla			;16cc	17		.
	add hl,de		;16cd	19		.
	ld b,a			;16ce	47		G
	ret			;16cf	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Rst20Handler - print inline message: EX (SP),HL swaps the
; return address into HL (pointing at the inline text), prints
; until a bit-7 character, then stacks the corrected resume
; address - the classic ZX-ROM inline-print trick, on RST 20h.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Rst20Handler:
	ex (sp),hl		;16d0	e3		.
	push af			;16d1	f5		.
l16d2h:
	ld a,(hl)		;16d2	7e		~
	call PrintCharBit7	;16d3	cd 0b 2b	. . +
	and 080h		;16d6	e6 80		. .
	inc hl			;16d8	23		#
	jr z,l16d2h		;16d9	28 f7		( .
	pop af			;16db	f1		.
	ex (sp),hl		;16dc	e3		.
	ret			;16dd	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintHexWord / PrintHexByte ($16E3): nibble conversion via
; the DAA trick (add $90, daa, adc $40, daa per nibble).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintHexWord:
	ld a,h			;16de	7c		|
	call PrintHexByte	;16df	cd e3 16	. . .
	ld a,l			;16e2	7d		}
PrintHexByte:
	push af			;16e3	f5		.
	rlca			;16e4	07		.
	rlca			;16e5	07		.
	rlca			;16e6	07		.
	rlca			;16e7	07		.
	call sub_16ech		;16e8	cd ec 16	. . .
	pop af			;16eb	f1		.
sub_16ech:
	and 00fh		;16ec	e6 0f		. .
	add a,090h		;16ee	c6 90		. .
	daa			;16f0	27		'
	adc a,040h		;16f1	ce 40		. @
	daa			;16f3	27		'
	rst 10h			;16f4	d7		.
	ret			;16f5	c9		.
sub_16f6h:
	ld l,(iy+002h)		;16f6	fd 6e 02	. n .
	jr l1744h		;16f9	18 49		. I
sub_16fbh:
	ld l,001h		;16fb	2e 01		. .
	jr l1744h		;16fd	18 45		. E
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintExtMenu: walk the extension menu table at ExtMenuTable
; ($E9A9) calling RAM hooks (zone $EB03/$EB0D) per entry - the
; documented way for RAM extensions to add monitor commands.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintExtMenu:
	bit 4,(iy+014h)		;16ff	fd cb 14 66	. . . f
	ret z			;1703	c8		.
	ld hl,ExtMenuTable	;1704	21 a9 e9	! . .
	xor a			;1707	af		.
l1708h:
	push af			;1708	f5		.
	ld e,(hl)		;1709	5e		^
	inc hl			;170a	23		#
	ld d,(hl)		;170b	56		V
	inc hl			;170c	23		#
	push hl			;170d	e5		.
	ld a,d			;170e	7a		z
	or e			;170f	b3		.
	call nz,sub_1de5h	;1710	c4 e5 1d	. . .
	pop hl			;1713	e1		.
	pop af			;1714	f1		.
	inc a			;1715	3c		<
	cp 00ah			;1716	fe 0a		. .
	jr nz,l1708h		;1718	20 ee		  .
	ret			;171a	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrepareStep: swap the user PC with the step-context copy at
; StepContext ($E11A), park the step engine on its own stack at
; $E38D (the v4.01 monitor itself keeps $E336), arm the trace
; enable (bit 5 of StepFlags at $E026), then let the class
; patchers below adjust the step context for the next
; instruction.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrepareStep:
	ld hl,(UserPc)		;171b	2a 6b dd	* k .
	push hl			;171e	e5		.
	ld hl,(StepContext)	;171f	2a 1a e1	* . .
	ld (UserPc),hl		;1722	22 6b dd	" k .
	ld hl,l173ah		;1725	21 3a 17	! : .
	push hl			;1728	e5		.
	ld (0e38dh),sp		;1729	ed 73 8d e3	. s . .
	ld hl,StepFlags		;172d	21 26 e0	! & .
	set 5,(hl)		;1730	cb ee		. .
	bit 3,(hl)		;1732	cb 5e		. ^
	call nz,sub_0581h	;1734	c4 81 05	. . .
	jp l0143h		;1737	c3 43 01	. C .
l173ah:
	res 5,(iy+012h)		;173a	fd cb 12 ae	. . . .
	pop hl			;173e	e1		.
	ld (UserPc),hl		;173f	22 6b dd	" k .
sub_1742h:
	ld l,0ffh		;1742	2e ff		. .
l1744h:
	ld a,(0dd84h)		;1744	3a 84 dd	: . .
	ld h,a			;1747	67		g
	add a,l			;1748	85		.
	rlca			;1749	07		.
	sla h			;174a	cb 24		. $
	rra			;174c	1f		.
	ld (0dd84h),a		;174d	32 84 dd	2 . .
	ret			;1750	c9		.
sub_1751h:
	ld hl,(StepContext)	;1751	2a 1a e1	* . .
	inc hl			;1754	23		#
	inc hl			;1755	23		#
	ld (hl),002h		;1756	36 02		6 .
	jr l1782h		;1758	18 28		. (
sub_175ah:
	ld hl,(0ddaah)		;175a	2a aa dd	* . .
	rst 28h			;175d	ef		.
	ld e,a			;175e	5f		_
	inc hl			;175f	23		#
	rst 28h			;1760	ef		.
	ld d,a			;1761	57		W
	inc hl			;1762	23		#
	ld (0ddaah),hl		;1763	22 aa dd	" . .
	ld hl,(StepContext)	;1766	2a 1a e1	* . .
	inc hl			;1769	23		#
	inc hl			;176a	23		#
	ld (hl),e		;176b	73		s
	inc hl			;176c	23		#
	ld (hl),d		;176d	72		r
	inc hl			;176e	23		#
	ld (hl),0cfh		;176f	36 cf		6 .
	jr PrepareStep		;1771	18 a8		. .
sub_1773h:
	ld hl,(StepContext)	;1773	2a 1a e1	* . .
	inc hl			;1776	23		#
	inc hl			;1777	23		#
	push hl			;1778	e5		.
	ld de,l0003h+1		;1779	11 04 00	. . .
	add hl,de		;177c	19		.
	ex de,hl		;177d	eb		.
	pop hl			;177e	e1		.
	ld (hl),e		;177f	73		s
	inc hl			;1780	23		#
	ld (hl),d		;1781	72		r
l1782h:
	inc hl			;1782	23		#
	ld (hl),0b7h		;1783	36 b7		6 .
	inc hl			;1785	23		#
	ld (hl),0cfh		;1786	36 cf		6 .
	inc hl			;1788	23		#
	ld (hl),037h		;1789	36 37		6 7
	inc hl			;178b	23		#
	ld (hl),0cfh		;178c	36 cf		6 .
	ld a,(0dd7fh)		;178e	3a 7f dd	: . .
	push af			;1791	f5		.
	call PrepareStep	;1792	cd 1b 17	. . .
	call sub_1742h		;1795	cd 42 17	. B .
	ld a,(0dd7fh)		;1798	3a 7f dd	: . .
	rrca			;179b	0f		.
	pop hl			;179c	e1		.
	ld a,h			;179d	7c		|
	ld (0dd7fh),a		;179e	32 7f dd	2 . .
	ret			;17a1	c9		.
SwapAltRegs:
	ld hl,RegisterFile	;17a2	21 73 dd	! s .
	ld de,0dd79h		;17a5	11 79 dd	. y .
	ld b,006h		;17a8	06 06		. .
l17aah:
	ld a,(de)		;17aa	1a		.
	ld c,(hl)		;17ab	4e		N
	ex de,hl		;17ac	eb		.
	ld (de),a		;17ad	12		.
	ld (hl),c		;17ae	71		q
	inc de			;17af	13		.
	inc hl			;17b0	23		#
	djnz l17aah		;17b1	10 f7		. .
	ret			;17b3	c9		.
sub_17b4h:
	ld hl,0dd7fh		;17b4	21 7f dd	! . .
	ld de,0dd81h		;17b7	11 81 dd	. . .
	ld b,002h		;17ba	06 02		. .
	jr l17aah		;17bc	18 ec		. .
l17beh:
	ld de,Rst08Vector	;17be	11 08 00	. . .
	ld hl,(StepPc)		;17c1	2a d0 dd	* . .
	or a			;17c4	b7		.
	sbc hl,de		;17c5	ed 52		. R
	jr nz,l1807h		;17c7	20 3e		  >
	ld hl,(UserPc)		;17c9	2a 6b dd	* k .
	ld e,0ffh		;17cc	1e ff		. .
	jp l01f5h		;17ce	c3 f5 01	. . .
l17d1h:
	ld a,(UserIff)		;17d1	3a 83 dd	: . .
	bit 2,a			;17d4	cb 57		. W
	jr nz,l17eah		;17d6	20 12		  .
	ld a,0abh		;17d8	3e ab		> .
	jp l0ae0h		;17da	c3 e0 0a	. . .
ToggleAltDisplay:
	ld a,(0ddb4h)		;17dd	3a b4 dd	: . .
	cp 008h			;17e0	fe 08		. .
	push af			;17e2	f5		.
	call z,sub_17b4h	;17e3	cc b4 17	. . .
	pop af			;17e6	f1		.
	call nz,SwapAltRegs	;17e7	c4 a2 17	. . .
l17eah:
	call sub_16fbh		;17ea	cd fb 16	. . .
	jp l18bdh		;17ed	c3 bd 18	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BuildStepStub: build the one-instruction trampoline at
; StepStub (E2B7) - DI (or EI, per the saved user IFF, DD83
; bit 2), the instruction bytes fetched across banks, then $CF
; (RST 08) as the trap.  Running the stub executes exactly one
; user instruction with interrupts in their saved state.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BuildStepStub:
	ld hl,(UserPc)		;17f0	2a 6b dd	* k .
	call sub_191eh		;17f3	cd 1e 19	. . .
	jp nc,l18f1h		;17f6	d2 f1 18	. . .
	call FetchPrefixBytes	;17f9	cd 96 2f	. . /
	ld a,(0ddb7h)		;17fc	3a b7 dd	: . .
	cp 032h			;17ff	fe 32		. 2
	jr z,l17d1h		;1801	28 ce		( .
	cp 072h			;1803	fe 72		. r
	jr z,l17beh		;1805	28 b7		( .
l1807h:
	ld hl,(StepContext)	;1807	2a 1a e1	* . .
	push hl			;180a	e5		.
	ld de,StepStub		;180b	11 9a e3	. . .
	ld bc,Rst08Vector	;180e	01 08 00	. . .
	ldir			;1811	ed b0		. .
	pop de			;1813	d1		.
	ld a,(UserIff)		;1814	3a 83 dd	: . .
	bit 2,a			;1817	cb 57		. W
	ld a,0f3h		;1819	3e f3		> .
	jr z,l181fh		;181b	28 02		( .
	ld a,0fbh		;181d	3e fb		> .
l181fh:
	ld (de),a		;181f	12		.
	inc de			;1820	13		.
	ld hl,(UserPc)		;1821	2a 6b dd	* k .
	ld c,(iy+002h)		;1824	fd 4e 02	. N .
	ld b,000h		;1827	06 00		. .
	call CopyAcrossBanks	;1829	cd 53 07	. S .
	ld a,0cfh		;182c	3e cf		> .
	ld (de),a		;182e	12		.
	ld a,(iy+003h)		;182f	fd 7e 03	. ~ .
	or a			;1832	b7		.
	jp z,l18bah		;1833	ca ba 18	. . .
	bit 7,a			;1836	cb 7f		. .
	jr nz,ToggleAltDisplay	;1838	20 a3		  .
	bit 1,a			;183a	cb 4f		. O
	jr z,l185ah		;183c	28 1c		( .
	bit 5,a			;183e	cb 6f		. o
	jr z,l1851h		;1840	28 0f		( .
	ld hl,(StepContext)	;1842	2a 1a e1	* . .
	inc hl			;1845	23		#
	ld a,(hl)		;1846	7e		~
	and 038h		;1847	e6 38		. 8
	or 0c2h			;1849	f6 c2		. .
	ld (hl),a		;184b	77		w
	call sub_1773h		;184c	cd 73 17	. s .
	jr nc,l1861h		;184f	30 10		0 .
l1851h:
	call nc,sub_16f6h	;1851	d4 f6 16	. . .
	call sub_3640h		;1854	cd 40 36	. @ 6
	ex de,hl		;1857	eb		.
	jr l18c0h		;1858	18 66		. f
l185ah:
	bit 2,a			;185a	cb 57		. W
	jr z,l1865h		;185c	28 07		( .
	call sub_1751h		;185e	cd 51 17	. Q .
l1861h:
	jr nc,l18bdh		;1861	30 5a		0 Z
	jr l18a7h		;1863	18 42		. B
l1865h:
	bit 0,a			;1865	cb 47		. G
	jr z,l1894h		;1867	28 2b		( +
	bit 7,(iy+016h)		;1869	fd cb 16 7e	. . . ~
	jr nz,l18d5h		;186d	20 66		  f
	call CheckTrace		;186f	cd 0f 19	. . .
	jr c,l18dah		;1872	38 66		8 f
l1874h:
	ld hl,(StepContext)	;1874	2a 1a e1	* . .
	inc hl			;1877	23		#
	ld a,(hl)		;1878	7e		~
	or a			;1879	b7		.
	bit 0,a			;187a	cb 47		. G
	jr nz,l1888h		;187c	20 0a		  .
	and 038h		;187e	e6 38		. 8
	or 0c2h			;1880	f6 c2		. .
	ld (hl),a		;1882	77		w
	call sub_1773h		;1883	cd 73 17	. s .
	jr nc,l18bdh		;1886	30 35		0 5
l1888h:
	call nc,sub_16fbh	;1888	d4 fb 16	. . .
	ld de,(0ddaah)		;188b	ed 5b aa dd	. [ . .
	call sub_3a14h		;188f	cd 14 3a	. . :
	jr l18a7h		;1892	18 13		. .
l1894h:
	bit 3,a			;1894	cb 5f		. _
	jr z,l18ach		;1896	28 14		( .
	call sub_1773h		;1898	cd 73 17	. s .
	jr nc,l18bdh		;189b	30 20		0  
	call CheckTrace		;189d	cd 0f 19	. . .
	push af			;18a0	f5		.
	call c,sub_1742h	;18a1	dc 42 17	. B .
	pop af			;18a4	f1		.
	jr c,l18f4h		;18a5	38 4d		8 M
l18a7h:
	ld hl,(StepPc)		;18a7	2a d0 dd	* . .
	jr l18c0h		;18aa	18 14		. .
l18ach:
	bit 4,a			;18ac	cb 67		. g
	jr z,l18b5h		;18ae	28 05		( .
	call sub_16fbh		;18b0	cd fb 16	. . .
	jr l18a7h		;18b3	18 f2		. .
l18b5h:
	ld a,0aah		;18b5	3e aa		> .
	jp l0ae0h		;18b7	c3 e0 0a	. . .
l18bah:
	call PrepareStep	;18ba	cd 1b 17	. . .
l18bdh:
	ld hl,(0ddaah)		;18bd	2a aa dd	* . .
l18c0h:
	ld (UserPc),hl		;18c0	22 6b dd	" k .
	push hl			;18c3	e5		.
	ld hl,StepStub		;18c4	21 9a e3	! . .
	ld de,(StepContext)	;18c7	ed 5b 1a e1	. [ . .
	ld bc,Rst08Vector	;18cb	01 08 00	. . .
	ldir			;18ce	ed b0		. .
	call PrintExtMenu	;18d0	cd ff 16	. . .
	pop hl			;18d3	e1		.
	ret			;18d4	c9		.
l18d5h:
	call sub_191bh		;18d5	cd 1b 19	. . .
	jr nc,l1874h		;18d8	30 9a		0 .
l18dah:
	ld hl,(StepPc)		;18da	2a d0 dd	* . .
	ld de,ReadAnyBankByte	;18dd	11 28 00	. ( .
	or a			;18e0	b7		.
	sbc hl,de		;18e1	ed 52		. R
	jr nz,l18bah		;18e3	20 d5		  .
	bit 4,(iy-002h)		;18e5	fd cb fe 66	. . . f
	jp nz,l1874h		;18e9	c2 74 18	. t .
	call sub_175ah		;18ec	cd 5a 17	. Z .
	jr l18bdh		;18ef	18 cc		. .
l18f1h:
	ld (StepPc),hl		;18f1	22 d0 dd	" . .
l18f4h:
	ld hl,(StepContext)	;18f4	2a 1a e1	* . .
	inc hl			;18f7	23		#
	ld (hl),0cdh		;18f8	36 cd		6 .
	inc hl			;18fa	23		#
	ld de,(StepPc)		;18fb	ed 5b d0 dd	. [ . .
	ld (hl),e		;18ff	73		s
	inc hl			;1900	23		#
	ld (hl),d		;1901	72		r
	inc hl			;1902	23		#
	ld (hl),0cfh		;1903	36 cf		6 .
	call sub_3640h		;1905	cd 40 36	. @ 6
	push de			;1908	d5		.
	call PrepareStep	;1909	cd 1b 17	. . .
	pop hl			;190c	e1		.
	jr l18c0h		;190d	18 b1		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckTrace: trace-mode gate evaluated after every step.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckTrace:
	or a			;190f	b7		.
	bit 3,(iy+005h)		;1910	fd cb 05 5e	. . . ^
	ret z			;1914	c8		.
	ld a,(0ddd1h)		;1915	3a d1 dd	: . .
	cp 040h			;1918	fe 40		. @
	ret nc			;191a	d0		.
sub_191bh:
	ld hl,(StepPc)		;191b	2a d0 dd	* . .
sub_191eh:
	bit 4,(iy-002h)		;191e	fd cb fe 66	. . . f
	scf			;1922	37		7
	ret z			;1923	c8		.
	ld de,l3d00h		;1924	11 00 3d	. . =
	or a			;1927	b7		.
	sbc hl,de		;1928	ed 52		. R
	add hl,de		;192a	19		.
	ret c			;192b	d8		.
	ld de,l3e00h		;192c	11 00 3e	. . >
	sbc hl,de		;192f	ed 52		. R
	add hl,de		;1931	19		.
	ccf			;1932	3f		?
	ret			;1933	c9		.
l1934h:
	ld (hl),e		;1934	73		s
	rst 38h			;1935	ff		.
	nop			;1936	00		.
	xor 042h		;1937	ee 42		. B
	ld b,c			;1939	41		A
	ld b,h			;193a	44		D
	ret p			;193b	f0		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AnalyzeInstruction - the static analyser: opcode class (EB/
; DD/FD/76/CB handled), length and control-flow effect from
; OpcodeClassTable ($12CE); results returned through the stack
; frame (SP=$DDCB on return).  Feeds StepDispatch and the
; step-over/breakpoint logic.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AnalyzeInstruction:
	ld (ix+000h),0f7h	;193c	dd 36 00 f7	. 6 . .
	pop ix			;1940	dd e1		. .
	ret			;1942	c9		.
l1943h:
	ld (ix+000h),0aah	;1943	dd 36 00 aa	. 6 . .
	inc ix			;1947	dd 23		. #
	ld (ix+000h),007h	;1949	dd 36 00 07	. 6 . .
l194dh:
	inc ix			;194d	dd 23		. #
l194fh:
	ld (iy+002h),001h	;194f	fd 36 02 01	. 6 . .
	jp l1a78h		;1953	c3 78 1a	. x .
l1956h:
	ld (ix+000h),032h	;1956	dd 36 00 32	. 6 . 2
	ld a,(hl)		;195a	7e		~
	or a			;195b	b7		.
	jr z,l194dh		;195c	28 ef		( .
l195eh:
	ld hl,l1934h		;195e	21 34 19	! 4 .
	ld de,0ddb7h		;1961	11 b7 dd	. . .
	ld bc,Rst08Vector	;1964	01 08 00	. . .
	ldir			;1967	ed b0		. .
	push de			;1969	d5		.
	pop ix			;196a	dd e1		. .
	xor a			;196c	af		.
	ld (iy+001h),a		;196d	fd 77 01	. w .
	ld (iy+004h),a		;1970	fd 77 04	. w .
	ld (iy+003h),040h	;1973	fd 36 03 40	. 6 . @
	ld a,(0ddach)		;1977	3a ac dd	: . .
	ld (0ddb9h),a		;197a	32 b9 dd	2 . .
	jr l194fh		;197d	18 d0		. .
sub_197fh:
	push ix			;197f	dd e5		. .
	ld ix,0ddb7h		;1981	dd 21 b7 dd	. ! . .
	ld de,(0ddaah)		;1985	ed 5b aa dd	. [ . .
	ld hl,AnalyzeInstruction	;1989	21 3c 19	! < .
	push hl			;198c	e5		.
	ld (0ddcbh),sp		;198d	ed 73 cb dd	. s . .
	bit 4,(iy+000h)		;1991	fd cb 00 66	. . . f
	jr z,l199fh		;1995	28 08		( .
	call FindBreakpoint	;1997	cd b3 1d	. . .
	ld b,0f1h		;199a	06 f1		. .
	call c,WordToHexBuf	;199c	dc 88 1d	. . .
l199fh:
	ld e,009h		;199f	1e 09		. .
	ld hl,0e015h		;19a1	21 15 e0	! . .
	xor a			;19a4	af		.
	ld d,a			;19a5	57		W
	ld (hl),a		;19a6	77		w
	ld (0ddcdh),a		;19a7	32 cd dd	2 . .
	ld (0e017h),a		;19aa	32 17 e0	2 . .
	ld (0e018h),a		;19ad	32 18 e0	2 . .
	ld bc,0ddach		;19b0	01 ac dd	. . .
	ld a,(bc)		;19b3	0a		.
	cp 0ebh			;19b4	fe eb		. .
	jr z,l1943h		;19b6	28 8b		( .
	cp 0ddh			;19b8	fe dd		. .
	jr nz,l19c2h		;19ba	20 06		  .
	ld (hl),002h		;19bc	36 02		6 .
	inc bc			;19be	03		.
	inc d			;19bf	14		.
	jr l19cah		;19c0	18 08		. .
l19c2h:
	cp 0fdh			;19c2	fe fd		. .
	jr nz,l19cah		;19c4	20 04		  .
	ld (hl),003h		;19c6	36 03		6 .
	inc bc			;19c8	03		.
	inc d			;19c9	14		.
l19cah:
	ld a,(bc)		;19ca	0a		.
	cp 076h			;19cb	fe 76		. v
	jr z,l1956h		;19cd	28 87		( .
	cp 0cbh			;19cf	fe cb		. .
	jr nz,l19dbh		;19d1	20 08		  .
	set 3,(hl)		;19d3	cb de		. .
	ld e,016h		;19d5	1e 16		. .
	inc bc			;19d7	03		.
	inc d			;19d8	14		.
	jr l19eah		;19d9	18 0f		. .
l19dbh:
	cp 0edh			;19db	fe ed		. .
	jr nz,l19eah		;19dd	20 0b		  .
	set 2,(hl)		;19df	cb d6		. .
	bit 1,(hl)		;19e1	cb 4e		. N
	jp nz,l195eh		;19e3	c2 5e 19	. ^ .
	inc bc			;19e6	03		.
	ld e,018h		;19e7	1e 18		. .
	inc d			;19e9	14		.
l19eah:
	inc d			;19ea	14		.
	ld (iy+002h),d		;19eb	fd 72 02	. r .
	push de			;19ee	d5		.
	ld de,0ddb4h		;19ef	11 b4 dd	. . .
	ld h,b			;19f2	60		`
	ld l,c			;19f3	69		i
	ld bc,l0003h		;19f4	01 03 00	. . .
	ldir			;19f7	ed b0		. .
	pop de			;19f9	d1		.
	ld a,(0e015h)		;19fa	3a 15 e0	: . .
	and 00ah		;19fd	e6 0a		. .
	cp 00ah			;19ff	fe 0a		. .
	jr nz,l1a09h		;1a01	20 06		  .
	ld a,(0ddb5h)		;1a03	3a b5 dd	: . .
	ld (0ddb4h),a		;1a06	32 b4 dd	2 . .
l1a09h:
	xor a			;1a09	af		.
	push af			;1a0a	f5		.
	push af			;1a0b	f5		.
	ld a,e			;1a0c	7b		{
l1a0dh:
	add a,a			;1a0d	87		.
	add a,a			;1a0e	87		.
	ld b,000h		;1a0f	06 00		. .
	ld c,a			;1a11	4f		O
	ld hl,OpcodeClassTable	;1a12	21 ce 12	! . .
	add hl,bc		;1a15	09		.
	ld c,(hl)		;1a16	4e		N
	inc hl			;1a17	23		#
	ld d,(hl)		;1a18	56		V
	inc hl			;1a19	23		#
	ld a,(hl)		;1a1a	7e		~
	inc hl			;1a1b	23		#
	ld h,(hl)		;1a1c	66		f
	ld l,a			;1a1d	6f		o
	ld a,(0ddb4h)		;1a1e	3a b4 dd	: . .
	ld b,000h		;1a21	06 00		. .
	and c			;1a23	a1		.
l1a24h:
	inc b			;1a24	04		.
	srl c			;1a25	cb 39		. 9
	jr nc,l1a24h		;1a27	30 fb		0 .
	or a			;1a29	b7		.
l1a2ah:
	dec b			;1a2a	05		.
	jr z,l1a30h		;1a2b	28 03		( .
	rrca			;1a2d	0f		.
	jr l1a2ah		;1a2e	18 fa		. .
l1a30h:
	bit 0,d			;1a30	cb 42		. B
	jr nz,l1a35h		;1a32	20 01		  .
	add a,a			;1a34	87		.
l1a35h:
	ld b,000h		;1a35	06 00		. .
	ld c,a			;1a37	4f		O
	add hl,bc		;1a38	09		.
l1a39h:
	ld a,(hl)		;1a39	7e		~
	inc hl			;1a3a	23		#
	dec d			;1a3b	15		.
	or a			;1a3c	b7		.
	jr z,l1a62h		;1a3d	28 23		( #
	cp 0eah			;1a3f	fe ea		. .
	jp z,l195eh		;1a41	ca 5e 19	. ^ .
	cp 0c5h			;1a44	fe c5		. .
	jr c,l1a94h		;1a46	38 4c		8 L
	cp 0eah			;1a48	fe ea		. .
	jr nc,l1a94h		;1a4a	30 48		0 H
	sub 0c6h		;1a4c	d6 c6		. .
	cp 017h			;1a4e	fe 17		. .
	jr nz,l1a56h		;1a50	20 04		  .
	set 6,(iy+004h)		;1a52	fd cb 04 f6	. . . .
l1a56h:
	bit 0,d			;1a56	cb 42		. B
	jr z,l1a0dh		;1a58	28 b3		( .
	push de			;1a5a	d5		.
	push hl			;1a5b	e5		.
	jr l1a0dh		;1a5c	18 af		. .
l1a5eh:
	ld a,d			;1a5e	7a		z
	or a			;1a5f	b7		.
	jr nz,l1a39h		;1a60	20 d7		  .
l1a62h:
	pop hl			;1a62	e1		.
	pop de			;1a63	d1		.
	ld a,d			;1a64	7a		z
	or a			;1a65	b7		.
	jr nz,l1a39h		;1a66	20 d1		  .
	ld hl,0e015h		;1a68	21 15 e0	! . .
	bit 1,(hl)		;1a6b	cb 4e		. N
	jr z,l1a78h		;1a6d	28 09		( .
	bit 4,(hl)		;1a6f	cb 66		. f
	jr nz,l1a78h		;1a71	20 05		  .
	bit 7,(hl)		;1a73	cb 7e		. ~
	jp z,l195eh		;1a75	ca 5e 19	. ^ .
l1a78h:
	ld bc,(0e016h)		;1a78	ed 4b 16 e0	. K . .
	ld b,000h		;1a7c	06 00		. .
	ld sp,(0ddcbh)		;1a7e	ed 7b cb dd	. { . .
	ret			;1a82	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CollectAsmText: capture assembler source from the editor
; into the work buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CollectAsmText:
	cp 020h			;1a83	fe 20		.  
	jr nc,EditorKeyLoop	;1a85	30 20		0  
	ld (ix+000h),a		;1a87	dd 77 00	. w .
	inc ix			;1a8a	dd 23		. #
	set 5,(iy+001h)		;1a8c	fd cb 01 ee	. . . .
	ld a,022h		;1a90	3e 22		> "
	jr EditorKeyLoop	;1a92	18 13		. .
l1a94h:
	push de			;1a94	d5		.
	push hl			;1a95	e5		.
	bit 1,(iy+001h)		;1a96	fd cb 01 4e	. . . N
	jr z,EditorKeyLoop	;1a9a	28 0b		( .
	ld hl,0e018h		;1a9c	21 18 e0	! . .
	bit 7,(hl)		;1a9f	cb 7e		. ~
	jr nz,CollectAsmText	;1aa1	20 e0		  .
	bit 6,(hl)		;1aa3	cb 76		. v
	jr nz,CollectAsmText	;1aa5	20 dc		  .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EditorKeyLoop: the line editor dispatcher - EditorKeyTable
; ($14B2, CPIR-searched, 0x24 entries) maps keys to cursor
; moves, insert/delete and history actions.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EditorKeyLoop:
	push af			;1aa7	f5		.
l1aa8h:
	ld hl,EditorKeyTable	;1aa8	21 b2 14	! . .
	ld bc,l0024h		;1aab	01 24 00	. $ .
	cpir			;1aae	ed b1		. .
	ld b,a			;1ab0	47		G
	jp nz,EditorRegKeys	;1ab1	c2 d1 1b	. . .
	ld a,c			;1ab4	79		y
	cp 01eh			;1ab5	fe 1e		. .
	jr nc,l1ad5h		;1ab7	30 1c		0 .
	cp 01ch			;1ab9	fe 1c		. .
	jr nc,l1b27h		;1abb	30 6a		0 j
	cp 00bh			;1abd	fe 0b		. .
	jp c,l1c5bh		;1abf	da 5b 1c	. [ .
	cp 014h			;1ac2	fe 14		. .
	ld hl,0e017h		;1ac4	21 17 e0	! . .
	jr nc,l1b2dh		;1ac7	30 64		0 d
	cp 011h			;1ac9	fe 11		. .
	jr c,l1b3ah		;1acb	38 6d		8 m
	set 1,(hl)		;1acd	cb ce		. .
	jp EditorRegKeys	;1acf	c3 d1 1b	. . .
l1ad2h:
	dec b			;1ad2	05		.
	jr l1b0eh		;1ad3	18 39		. 9
l1ad5h:
	ld hl,0e015h		;1ad5	21 15 e0	! . .
	cp 023h			;1ad8	fe 23		. #
	jr z,l1ad2h		;1ada	28 f6		( .
	jr c,l1ae9h		;1adc	38 0b		8 .
	cp 02ch			;1ade	fe 2c		. ,
	jr nc,l1b13h		;1ae0	30 31		0 1
	set 3,(iy+004h)		;1ae2	fd cb 04 de	. . . .
	jp EditorRegKeys	;1ae6	c3 d1 1b	. . .
l1ae9h:
	cp 022h			;1ae9	fe 22		. "
	jr z,l1b0eh		;1aeb	28 21		( !
	bit 1,(hl)		;1aed	cb 4e		. N
	jr z,l1b10h		;1aef	28 1f		( .
	bit 3,(hl)		;1af1	cb 5e		. ^
	jr nz,l1b10h		;1af3	20 1b		  .
	sub 01dh		;1af5	d6 1d		. .
	cp 003h			;1af7	fe 03		. .
	jr nc,l1b23h		;1af9	30 28		0 (
	bit 7,(hl)		;1afb	cb 7e		. ~
	jr nz,l1b10h		;1afd	20 11		  .
	bit 5,(iy+004h)		;1aff	fd cb 04 6e	. . . n
	jr nz,l1b10h		;1b03	20 0b		  .
l1b05h:
	bit 0,(hl)		;1b05	cb 46		. F
	jr z,l1b0bh		;1b07	28 02		( .
	add a,002h		;1b09	c6 02		. .
l1b0bh:
	ld b,a			;1b0b	47		G
	set 4,(hl)		;1b0c	cb e6		. .
l1b0eh:
	set 5,(hl)		;1b0e	cb ee		. .
l1b10h:
	jp EditorRegKeys	;1b10	c3 d1 1b	. . .
l1b13h:
	set 5,(hl)		;1b13	cb ee		. .
	cp 02dh			;1b15	fe 2d		. -
	jr nc,l1b1eh		;1b17	30 05		0 .
	ld a,066h		;1b19	3e 66		> f
	jp l1aa8h		;1b1b	c3 a8 1a	. . .
l1b1eh:
	ld b,033h		;1b1e	06 33		. 3
	jp EditorRegKeys	;1b20	c3 d1 1b	. . .
l1b23h:
	add a,07bh		;1b23	c6 7b		. {
	jr l1b05h		;1b25	18 de		. .
l1b27h:
	set 7,(iy+003h)		;1b27	fd cb 03 fe	. . . .
	jr l1b10h		;1b2b	18 e3		. .
l1b2dh:
	bit 1,(hl)		;1b2d	cb 4e		. N
	jr z,l1b10h		;1b2f	28 df		( .
	set 5,(hl)		;1b31	cb ee		. .
	sub 014h		;1b33	d6 14		. .
	ld (0e032h),a		;1b35	32 32 e0	2 2 .
	jr l1b10h		;1b38	18 d6		. .
l1b3ah:
	ld hl,(RegisterFile)	;1b3a	2a 73 dd	* s .
	ld (0ddceh),hl		;1b3d	22 ce dd	" . .
	cp 00eh			;1b40	fe 0e		. .
	jr nz,l1b60h		;1b42	20 1c		  .
	set 4,(iy+003h)		;1b44	fd cb 03 e6	. . . .
	bit 1,(iy+001h)		;1b48	fd cb 01 4e	. . . N
	jr z,l1b5dh		;1b4c	28 0f		( .
	ld hl,(0dd6fh)		;1b4e	2a 6f dd	* o .
	bit 0,(iy+001h)		;1b51	fd cb 01 46	. . . F
	jr z,l1b5ah		;1b55	28 03		( .
	ld hl,(0dd71h)		;1b57	2a 71 dd	* q .
l1b5ah:
	ld (0ddceh),hl		;1b5a	22 ce dd	" . .
l1b5dh:
	ld (StepPc),hl		;1b5d	22 d0 dd	" . .
l1b60h:
	ld hl,0e015h		;1b60	21 15 e0	! . .
	ld a,(hl)		;1b63	7e		~
	bit 1,a			;1b64	cb 4f		. O
	jr z,l1b10h		;1b66	28 a8		( .
	set 7,(hl)		;1b68	cb fe		. .
	ld e,c			;1b6a	59		Y
	ld hl,l14cbh		;1b6b	21 cb 14	! . .
	bit 0,a			;1b6e	cb 47		. G
	ld b,000h		;1b70	06 00		. .
	jr z,l1b78h		;1b72	28 04		( .
	ld a,c			;1b74	79		y
	add a,006h		;1b75	c6 06		. .
	ld c,a			;1b77	4f		O
l1b78h:
	add hl,bc		;1b78	09		.
	ld b,(hl)		;1b79	46		F
	ld (ix+000h),b		;1b7a	dd 70 00	. p .
	inc ix			;1b7d	dd 23		. #
	ld a,e			;1b7f	7b		{
	cp 00dh			;1b80	fe 0d		. .
	jr nc,l1bd6h		;1b82	30 52		0 R
	cp 00ch			;1b84	fe 0c		. .
	jr nz,l1ba0h		;1b86	20 18		  .
	bit 4,(iy+001h)		;1b88	fd cb 01 66	. . . f
	jr z,l1ba0h		;1b8c	28 12		( .
	ld a,001h		;1b8e	3e 01		> .
	and (ix-002h)		;1b90	dd a6 fe	. . .
	add a,0bah		;1b93	c6 ba		. .
	ld (ix-002h),a		;1b95	dd 77 fe	. w .
	res 4,(iy+001h)		;1b98	fd cb 01 a6	. . . .
	res 5,(iy+001h)		;1b9c	fd cb 01 ae	. . . .
l1ba0h:
	inc (iy+002h)		;1ba0	fd 34 02	. 4 .
	ld a,(0ddaeh)		;1ba3	3a ae dd	: . .
	or a			;1ba6	b7		.
	jp p,l1bafh		;1ba7	f2 af 1b	. . .
	inc (ix-001h)		;1baa	dd 34 ff	. 4 .
	neg			;1bad	ed 44		. D
l1bafh:
	ld (ix+000h),0fdh	;1baf	dd 36 00 fd	. 6 . .
	inc ix			;1bb3	dd 23		. #
	ld b,a			;1bb5	47		G
	ld hl,(0dd6fh)		;1bb6	2a 6f dd	* o .
	bit 0,(iy+001h)		;1bb9	fd cb 01 46	. . . F
	jr z,l1bc2h		;1bbd	28 03		( .
	ld hl,(0dd71h)		;1bbf	2a 71 dd	* q .
l1bc2h:
	ld de,(0ddaeh)		;1bc2	ed 5b ae dd	. [ . .
	ld d,000h		;1bc6	16 00		. .
	bit 7,e			;1bc8	cb 7b		. {
	jr z,l1bcdh		;1bca	28 01		( .
	dec d			;1bcc	15		.
l1bcdh:
	add hl,de		;1bcd	19		.
	ld (0ddceh),hl		;1bce	22 ce dd	" . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EditorRegKeys: register-edit keys (RegisterKeyTable $14E2)
; - hex entry into the highlighted register of the dump.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
EditorRegKeys:
	ld (ix+000h),b		;1bd1	dd 70 00	. p .
	inc ix			;1bd4	dd 23		. #
l1bd6h:
	pop af			;1bd6	f1		.
	push af			;1bd7	f5		.
	ld hl,RegisterKeyTable	;1bd8	21 e2 14	! . .
	ld bc,Rst08Vector+2	;1bdb	01 0a 00	. . .
	cpir			;1bde	ed b1		. .
	jr nz,l1c3ch		;1be0	20 5a		  Z
	ld a,c			;1be2	79		y
	cp 008h			;1be3	fe 08		. .
	jr nc,l1c45h		;1be5	30 5e		0 ^
	and 003h		;1be7	e6 03		. .
	jr z,l1c00h		;1be9	28 15		( .
	dec a			;1beb	3d		=
	jr z,l1bf6h		;1bec	28 08		( .
	dec a			;1bee	3d		=
	jr z,l1bfbh		;1bef	28 0a		( .
	ld hl,(StepPc)		;1bf1	2a d0 dd	* . .
	jr l1c03h		;1bf4	18 0d		. .
l1bf6h:
	ld hl,(0dd77h)		;1bf6	2a 77 dd	* w .
	jr l1c03h		;1bf9	18 08		. .
l1bfbh:
	ld hl,(0dd75h)		;1bfb	2a 75 dd	* u .
	jr l1c03h		;1bfe	18 03		. .
l1c00h:
	ld hl,(0ddceh)		;1c00	2a ce dd	* . .
l1c03h:
	ld (0ddd2h),hl		;1c03	22 d2 dd	" . .
	ld a,c			;1c06	79		y
	cp 004h			;1c07	fe 04		. .
	ld b,002h		;1c09	06 02		. .
	jr nc,l1c35h		;1c0b	30 28		0 (
	dec b			;1c0d	05		.
	cp 003h			;1c0e	fe 03		. .
	jr nz,l1c1bh		;1c10	20 09		  .
	ld a,(0ddcdh)		;1c12	3a cd dd	: . .
	cp 0a1h			;1c15	fe a1		. .
	jr nz,l1c35h		;1c17	20 1c		  .
	jr l1c3ch		;1c19	18 21		. !
l1c1bh:
	or a			;1c1b	b7		.
	jr nz,l1c35h		;1c1c	20 17		  .
	ld a,(0ddcdh)		;1c1e	3a cd dd	: . .
	ld hl,l14ech		;1c21	21 ec 14	! . .
	ld bc,Rst08Vector+2	;1c24	01 0a 00	. . .
	cpir			;1c27	ed b1		. .
	jr z,l1c33h		;1c29	28 08		( .
	bit 7,(iy+004h)		;1c2b	fd cb 04 7e	. . . ~
	ld b,001h		;1c2f	06 01		. .
	jr z,l1c35h		;1c31	28 02		( .
l1c33h:
	ld b,003h		;1c33	06 03		. .
l1c35h:
	ld a,b			;1c35	78		x
	or (iy+004h)		;1c36	fd b6 04	. . .
	ld (iy+004h),a		;1c39	fd 77 04	. w .
l1c3ch:
	pop af			;1c3c	f1		.
	ld (0ddcdh),a		;1c3d	32 cd dd	2 . .
	pop hl			;1c40	e1		.
	pop de			;1c41	d1		.
	jp l1a5eh		;1c42	c3 5e 1a	. ^ .
l1c45h:
	ld b,003h		;1c45	06 03		. .
	ld hl,(RegisterFile)	;1c47	2a 73 dd	* s .
	ld (0ddd2h),hl		;1c4a	22 d2 dd	" . .
	jr l1c35h		;1c4d	18 e6		. .
l1c4fh:
	ld (StepPc),de		;1c4f	ed 53 d0 dd	. S . .
	ld b,0ech		;1c53	06 ec		. .
	call WordToHexBuf	;1c55	cd 88 1d	. . .
	jp l1bd6h		;1c58	c3 d6 1b	. . .
l1c5bh:
	cp 003h			;1c5b	fe 03		. .
	jr c,l1c64h		;1c5d	38 05		8 .
	ld (ix+000h),b		;1c5f	dd 70 00	. p .
	inc ix			;1c62	dd 23		. #
l1c64h:
	ld d,000h		;1c64	16 00		. .
	ld hl,0e017h		;1c66	21 17 e0	! . .
	cp 001h			;1c69	fe 01		. .
	jp c,l1d00h		;1c6b	da 00 1d	. . .
	jr z,l1c8eh		;1c6e	28 1e		( .
	cp 004h			;1c70	fe 04		. .
	jr c,l1cc9h		;1c72	38 55		8 U
	cp 006h			;1c74	fe 06		. .
	jr c,l1c8eh		;1c76	38 16		8 .
	cp 008h			;1c78	fe 08		. .
	jr c,l1ce4h		;1c7a	38 68		8 h
	jr z,l1ce8h		;1c7c	28 6a		( j
	cp 00ah			;1c7e	fe 0a		. .
	jp z,l1d58h		;1c80	ca 58 1d	. X .
	ld a,(0ddb4h)		;1c83	3a b4 dd	: . .
	and 038h		;1c86	e6 38		. 8
	set 0,(hl)		;1c88	cb c6		. .
	ld e,a			;1c8a	5f		_
	jp l1d44h		;1c8b	c3 44 1d	. D .
l1c8eh:
	ld a,(0ddcdh)		;1c8e	3a cd dd	: . .
	cp 0a1h			;1c91	fe a1		. .
	jr z,l1cc9h		;1c93	28 34		( 4
	cp 055h			;1c95	fe 55		. U
	jr z,l1cbbh		;1c97	28 22		( "
	cp 053h			;1c99	fe 53		. S
	jr z,l1cach		;1c9b	28 0f		( .
	cp 091h			;1c9d	fe 91		. .
	jr nc,l1cb0h		;1c9f	30 0f		0 .
	cp 082h			;1ca1	fe 82		. .
	jr c,l1cbdh		;1ca3	38 18		8 .
	set 5,(hl)		;1ca5	cb ee		. .
	sub 082h		;1ca7	d6 82		. .
	ld (0e032h),a		;1ca9	32 32 e0	2 2 .
l1cach:
	set 3,(hl)		;1cac	cb de		. .
	jr l1cbdh		;1cae	18 0d		. .
l1cb0h:
	cp 099h			;1cb0	fe 99		. .
	jr nc,l1cbdh		;1cb2	30 09		0 .
	set 5,(hl)		;1cb4	cb ee		. .
	sub 091h		;1cb6	d6 91		. .
	ld (0e032h),a		;1cb8	32 32 e0	2 2 .
l1cbbh:
	set 0,(hl)		;1cbb	cb c6		. .
l1cbdh:
	ld de,(0ddb5h)		;1cbd	ed 5b b5 dd	. [ . .
	inc (iy+002h)		;1cc1	fd 34 02	. 4 .
	inc (iy+002h)		;1cc4	fd 34 02	. 4 .
	jr l1d2ch		;1cc7	18 63		. c
l1cc9h:
	inc (iy+002h)		;1cc9	fd 34 02	. 4 .
	ld b,0ffh		;1ccc	06 ff		. .
	ld hl,0e015h		;1cce	21 15 e0	! . .
	bit 1,(hl)		;1cd1	cb 4e		. N
	jr z,l1cdeh		;1cd3	28 09		( .
	bit 4,(hl)		;1cd5	cb 66		. f
	jr nz,l1cdeh		;1cd7	20 05		  .
	ld a,(0ddb6h)		;1cd9	3a b6 dd	: . .
	jr l1ce1h		;1cdc	18 03		. .
l1cdeh:
	ld a,(0ddb5h)		;1cde	3a b5 dd	: . .
l1ce1h:
	ld e,a			;1ce1	5f		_
	jr l1cf7h		;1ce2	18 13		. .
l1ce4h:
	set 7,(iy+004h)		;1ce4	fd cb 04 fe	. . . .
l1ce8h:
	ld a,(0ddb4h)		;1ce8	3a b4 dd	: . .
	set 5,(iy+004h)		;1ceb	fd cb 04 ee	. . . .
	and 038h		;1cef	e6 38		. 8
	rrca			;1cf1	0f		.
	rrca			;1cf2	0f		.
	rrca			;1cf3	0f		.
	ld e,a			;1cf4	5f		_
l1cf5h:
	ld b,0fdh		;1cf5	06 fd		. .
l1cf7h:
	ld (ix+000h),b		;1cf7	dd 70 00	. p .
	inc ix			;1cfa	dd 23		. #
	ld b,e			;1cfc	43		C
	jp EditorRegKeys	;1cfd	c3 d1 1b	. . .
l1d00h:
	ld a,(0ddcdh)		;1d00	3a cd dd	: . .
	cp 065h			;1d03	fe 65		. e
	jr nz,l1d0bh		;1d05	20 04		  .
	set 2,(iy+004h)		;1d07	fd cb 04 d6	. . . .
l1d0bh:
	sub 08dh		;1d0b	d6 8d		. .
	jr c,l1d14h		;1d0d	38 05		8 .
	ld (0e032h),a		;1d0f	32 32 e0	2 2 .
	set 5,(hl)		;1d12	cb ee		. .
l1d14h:
	inc (iy+002h)		;1d14	fd 34 02	. 4 .
	set 2,(hl)		;1d17	cb d6		. .
	ld a,(0ddb5h)		;1d19	3a b5 dd	: . .
	ld d,000h		;1d1c	16 00		. .
	or a			;1d1e	b7		.
	jp p,l1d23h		;1d1f	f2 23 1d	. # .
	dec d			;1d22	15		.
l1d23h:
	ld e,a			;1d23	5f		_
	ld hl,(0ddaah)		;1d24	2a aa dd	* . .
	inc hl			;1d27	23		#
	inc hl			;1d28	23		#
	add hl,de		;1d29	19		.
	ld e,l			;1d2a	5d		]
	ld d,h			;1d2b	54		T
l1d2ch:
	set 6,(iy+001h)		;1d2c	fd cb 01 f6	. . . .
	bit 4,(iy+000h)		;1d30	fd cb 00 66	. . . f
	jr z,l1d3ch		;1d34	28 06		( .
	call FindBreakpoint	;1d36	cd b3 1d	. . .
	jp c,l1c4fh		;1d39	da 4f 1c	. O .
l1d3ch:
	ld b,0fch		;1d3c	06 fc		. .
	bit 0,(iy+005h)		;1d3e	fd cb 05 46	. . . F
	jr nz,l1d46h		;1d42	20 02		  .
l1d44h:
	ld b,0feh		;1d44	06 fe		. .
l1d46h:
	ld (ix+000h),b		;1d46	dd 70 00	. p .
	inc ix			;1d49	dd 23		. #
	ld (ix+000h),e		;1d4b	dd 73 00	. s .
	inc ix			;1d4e	dd 23		. #
	ld b,d			;1d50	42		B
	ld (StepPc),de		;1d51	ed 53 d0 dd	. S . .
	jp EditorRegKeys	;1d55	c3 d1 1b	. . .
l1d58h:
	ld e,000h		;1d58	1e 00		. .
	ld b,0fch		;1d5a	06 fc		. .
	ld a,(0ddb4h)		;1d5c	3a b4 dd	: . .
	cp 046h			;1d5f	fe 46		. F
	jr z,l1cf5h		;1d61	28 92		( .
	inc e			;1d63	1c		.
	cp 056h			;1d64	fe 56		. V
	jr z,l1cf5h		;1d66	28 8d		( .
	inc e			;1d68	1c		.
	cp 05eh			;1d69	fe 5e		. ^
	jr z,l1cf5h		;1d6b	28 88		( .
	set 5,(iy+001h)		;1d6d	fd cb 01 ee	. . . .
	ld e,000h		;1d71	1e 00		. .
	cp 04eh			;1d73	fe 4e		. N
	jr z,l1d85h		;1d75	28 0e		( .
	cp 066h			;1d77	fe 66		. f
	jr z,l1d85h		;1d79	28 0a		( .
	cp 06eh			;1d7b	fe 6e		. n
	jr z,l1d85h		;1d7d	28 06		( .
	inc e			;1d7f	1c		.
	cp 076h			;1d80	fe 76		. v
	jr z,l1d85h		;1d82	28 01		( .
	inc e			;1d84	1c		.
l1d85h:
	jp l1cf5h		;1d85	c3 f5 1c	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WordToHexBuf: format HL as 4 hex characters (bit-7
; terminated) in the work buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WordToHexBuf:
	ld (ix+000h),b		;1d88	dd 70 00	. p .
	ld (ix+001h),04ch	;1d8b	dd 36 01 4c	. 6 . L
	inc ix			;1d8f	dd 23		. #
	inc ix			;1d91	dd 23		. #
	ld c,004h		;1d93	0e 04		. .
l1d95h:
	ld l,004h		;1d95	2e 04		. .
	xor a			;1d97	af		.
l1d98h:
	sla e			;1d98	cb 23		. #
	rl d			;1d9a	cb 12		. .
	rla			;1d9c	17		.
	dec l			;1d9d	2d		-
	jr nz,l1d98h		;1d9e	20 f8		  .
	add a,090h		;1da0	c6 90		. .
	daa			;1da2	27		'
	adc a,040h		;1da3	ce 40		. @
	daa			;1da5	27		'
	ld (ix+000h),a		;1da6	dd 77 00	. w .
	inc ix			;1da9	dd 23		. #
	dec c			;1dab	0d		.
	jr nz,l1d95h		;1dac	20 e7		  .
	set 7,(ix-001h)		;1dae	dd cb ff fe	. . . .
	ret			;1db2	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FindBreakpoint: search BreakTable (DDD6, count at
; BreakCount DDD4) for an address.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FindBreakpoint:
	or a			;1db3	b7		.
	bit 4,(iy+000h)		;1db4	fd cb 00 66	. . . f
	ret z			;1db8	c8		.
	ld bc,(BreakCount)	;1db9	ed 4b d4 dd	. K . .
	ld hl,(BreakTable)	;1dbd	2a d6 dd	* . .
l1dc0h:
	ld a,b			;1dc0	78		x
	or c			;1dc1	b1		.
	ret z			;1dc2	c8		.
	ld a,(hl)		;1dc3	7e		~
	inc hl			;1dc4	23		#
	cp e			;1dc5	bb		.
	jr nz,l1dcch		;1dc6	20 04		  .
	ld a,(hl)		;1dc8	7e		~
	cp d			;1dc9	ba		.
	scf			;1dca	37		7
	ret z			;1dcb	c8		.
l1dcch:
	inc hl			;1dcc	23		#
	dec bc			;1dcd	0b		.
	jr l1dc0h		;1dce	18 f0		. .
	and h			;1dd0	a4		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg1DD1: command keyword FLOAD of the 4.01
; command scanner.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg1DD1' (start 0x1dd1 end 0x1dd6)
Msg1DD1_start:
	defb 046h		;1dd1	46		F
	defb 04ch		;1dd2	4c		L
	defb 04fh		;1dd3	4f		O
	defb 041h		;1dd4	41		A
	defb 0c4h		;1dd5	c4		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg1DD6: command keyword FSAVE of the 4.01
; command scanner.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Msg1DD1_end:

; BLOCK 'Msg1DD6' (start 0x1dd6 end 0x1ddb)
Msg1DD6_start:
	defb 046h		;1dd6	46		F
	defb 053h		;1dd7	53		S
	defb 041h		;1dd8	41		A
	defb 056h		;1dd9	56		V
	defb 0c5h		;1dda	c5		.
Msg1DD6_end:
	nop			;1ddb	00		.
	or b			;1ddc	b0		.
	rra			;1ddd	1f		.
	jr nz,$+71		;1dde	20 45		  E
	ld e,004h		;1de0	1e 04		. .
	ld c,a			;1de2	4f		O
	ld e,004h		;1de3	1e 04		. .
sub_1de5h:
	ld hl,(0e11ch)		;1de5	2a 1c e1	* . .
sub_1de8h:
	inc hl			;1de8	23		#
	ld (0e9d9h),hl		;1de9	22 d9 e9	" . .
	inc hl			;1dec	23		#
	ld (0e9dbh),hl		;1ded	22 db e9	" . .
	ld hl,WriteAnyBankByte	;1df0	21 00 00	! . .
	push hl			;1df3	e5		.
	push de			;1df4	d5		.
	ex de,hl		;1df5	eb		.
l1df6h:
	ld a,(hl)		;1df6	7e		~
	or a			;1df7	b7		.
	inc hl			;1df8	23		#
	jr z,l1e04h		;1df9	28 09		( .
l1dfbh:
	ld e,(hl)		;1dfb	5e		^
	inc hl			;1dfc	23		#
	ld d,(hl)		;1dfd	56		V
	inc hl			;1dfe	23		#
	push hl			;1dff	e5		.
	push de			;1e00	d5		.
	ex de,hl		;1e01	eb		.
	jr l1df6h		;1e02	18 f2		. .
l1e04h:
	ld (0e9ddh),sp		;1e04	ed 73 dd e9	. s . .
	call sub_1e0eh		;1e08	cd 0e 1e	. . .
	jp l2261h		;1e0b	c3 61 22	. a "
sub_1e0eh:
	jp (hl)			;1e0e	e9		.
sub_1e0fh:
	push hl			;1e0f	e5		.
	or a			;1e10	b7		.
	ld hl,0f7bfh		;1e11	21 bf f7	! . .
	sbc hl,de		;1e14	ed 52		. R
	jr z,l1e1ch		;1e16	28 04		( .
	ld (de),a		;1e18	12		.
	inc de			;1e19	13		.
	pop hl			;1e1a	e1		.
	ret			;1e1b	c9		.
l1e1ch:
	ld a,02dh		;1e1c	3e 2d		> -
	jp ExitToError		;1e1e	c3 06 0b	. . .
sub_1e21h:
	call sub_306fh		;1e21	cd 6f 30	. o 0
	ld (ix+017h),a		;1e24	dd 77 17	. w .
	ld h,e			;1e27	63		c
	ld (0e7b7h),hl		;1e28	22 b7 e7	" . .
	push bc			;1e2b	c5		.
	ld hl,WriteAnyBankByte	;1e2c	21 00 00	! . .
	ld (0e7bch),hl		;1e2f	22 bc e7	" . .
	dec l			;1e32	2d		-
	ld (ix+00dh),l		;1e33	dd 75 0d	. u .
	ld (ix+01bh),l		;1e36	dd 75 1b	. u .
	ld hl,0e8a9h		;1e39	21 a9 e8	! . .
	ld (0e7beh),hl		;1e3c	22 be e7	" . .
	pop hl			;1e3f	e1		.
	ld bc,(0dd9fh)		;1e40	ed 4b 9f dd	. K . .
	ret			;1e44	c9		.
	ld a,005h		;1e45	3e 05		> .
	call sub_1e21h		;1e47	cd 21 1e	. ! .
	call sub_30d8h		;1e4a	cd d8 30	. . 0
	jr l1e9bh		;1e4d	18 4c		. L
	ld a,007h		;1e4f	3e 07		> .
	call sub_1e21h		;1e51	cd 21 1e	. ! .
	call sub_3107h		;1e54	cd 07 31	. . 1
	jr l1e9bh		;1e57	18 42		. B
sub_1e59h:
	call sub_3065h		;1e59	cd 65 30	. e 0
	rst 30h			;1e5c	f7		.
	or b			;1e5d	b0		.
	ld de,l3807h		;1e5e	11 07 38	. . 8
	inc a			;1e61	3c		<
	call sub_0ebah		;1e62	cd ba 0e	. . .
	or a			;1e65	b7		.
	jr z,l1e6ch		;1e66	28 04		( .
	ld a,00bh		;1e68	3e 0b		> .
	jr l1ea2h		;1e6a	18 36		. 6
l1e6ch:
	rst 30h			;1e6c	f7		.
	rst 18h			;1e6d	df		.
	ld de,l3807h		;1e6e	11 07 38	. . 8
	inc l			;1e71	2c		,
	ld hl,(0e7b2h)		;1e72	2a b2 e7	* . .
	ret			;1e75	c9		.
	bit 3,(iy+014h)		;1e76	fd cb 14 5e	. . . ^
	ld a,02eh		;1e7a	3e 2e		> .
	ret z			;1e7c	c8		.
	ld b,002h		;1e7d	06 02		. .
	call sub_1e59h		;1e7f	cd 59 1e	. Y .
	ld hl,(0e11ch)		;1e82	2a 1c e1	* . .
	ld bc,0f6c0h		;1e85	01 c0 f6	. . .
	push bc			;1e88	c5		.
	sbc hl,bc		;1e89	ed 42		. B
	ld a,02eh		;1e8b	3e 2e		> .
	jr z,l1ea2h		;1e8d	28 13		( .
	ld b,h			;1e8f	44		D
	ld c,l			;1e90	4d		M
	pop hl			;1e91	e1		.
	call sub_1ed6h		;1e92	cd d6 1e	. . .
l1e95h:
	jr c,l1e9eh		;1e95	38 07		8 .
l1e97h:
	rst 30h			;1e97	f7		.
	rlca			;1e98	07		.
	inc de			;1e99	13		.
	rlca			;1e9a	07		.
l1e9bh:
	jp nc,l0afbh		;1e9b	d2 fb 0a	. . .
l1e9eh:
	rst 30h			;1e9e	f7		.
	ld l,h			;1e9f	6c		l
	ld b,006h		;1ea0	06 06		. .
l1ea2h:
	jp ExitToError		;1ea2	c3 06 0b	. . .
	ld b,001h		;1ea5	06 01		. .
	call sub_1e59h		;1ea7	cd 59 1e	. Y .
	ld de,0f6c0h		;1eaa	11 c0 f6	. . .
	push de			;1ead	d5		.
	sbc hl,de		;1eae	ed 52		. R
	pop hl			;1eb0	e1		.
	ld a,02eh		;1eb1	3e 2e		> .
	jr nz,l1ea2h		;1eb3	20 ed		  .
	dec bc			;1eb5	0b		.
	dec bc			;1eb6	0b		.
	ld a,b			;1eb7	78		x
	cp 004h			;1eb8	fe 04		. .
	ld a,02dh		;1eba	3e 2d		> -
	jr nc,l1ea2h		;1ebc	30 e4		0 .
	push bc			;1ebe	c5		.
	push bc			;1ebf	c5		.
	push hl			;1ec0	e5		.
	call sub_1f25h		;1ec1	cd 25 1f	. % .
	pop hl			;1ec4	e1		.
	pop bc			;1ec5	c1		.
	call sub_1efbh		;1ec6	cd fb 1e	. . .
	jr c,l1e95h		;1ec9	38 ca		8 .
	pop de			;1ecb	d1		.
	ld hl,0f6c0h		;1ecc	21 c0 f6	! . .
	add hl,de		;1ecf	19		.
	ex de,hl		;1ed0	eb		.
	call sub_1f28h		;1ed1	cd 28 1f	. ( .
	jr l1e97h		;1ed4	18 c1		. .
sub_1ed6h:
	ld (0e7b2h),hl		;1ed6	22 b2 e7	" . .
	ld a,041h		;1ed9	3e 41		> A
	call sub_1ef2h		;1edb	cd f2 1e	. . .
	ret c			;1ede	d8		.
	ld a,(Filler00F1_start)	;1edf	3a f1 00	: . .
	call sub_1ef2h		;1ee2	cd f2 1e	. . .
	ret c			;1ee5	d8		.
l1ee6h:
	ld a,(hl)		;1ee6	7e		~
	call sub_1ef2h		;1ee7	cd f2 1e	. . .
	ret c			;1eea	d8		.
	inc hl			;1eeb	23		#
	dec bc			;1eec	0b		.
	ld a,b			;1eed	78		x
	or c			;1eee	b1		.
	jr nz,l1ee6h		;1eef	20 f5		  .
	ret			;1ef1	c9		.
sub_1ef2h:
	push hl			;1ef2	e5		.
	push bc			;1ef3	c5		.
	rst 30h			;1ef4	f7		.
	adc a,h			;1ef5	8c		.
	ld (de),a		;1ef6	12		.
	rlca			;1ef7	07		.
	pop bc			;1ef8	c1		.
	pop hl			;1ef9	e1		.
	ret			;1efa	c9		.
sub_1efbh:
	call sub_1f1ch		;1efb	cd 1c 1f	. . .
	ret c			;1efe	d8		.
	cp 041h			;1eff	fe 41		. A
l1f01h:
	ld a,055h		;1f01	3e 55		> U
	jr nz,l1ea2h		;1f03	20 9d		  .
	call sub_1f1ch		;1f05	cd 1c 1f	. . .
	ret c			;1f08	d8		.
	ld e,a			;1f09	5f		_
	ld a,(Filler00F1_start)	;1f0a	3a f1 00	: . .
	cp e			;1f0d	bb		.
	jr nz,l1f01h		;1f0e	20 f1		  .
l1f10h:
	call sub_1f1ch		;1f10	cd 1c 1f	. . .
	ret c			;1f13	d8		.
	ld (hl),a		;1f14	77		w
	inc hl			;1f15	23		#
	dec bc			;1f16	0b		.
	ld a,b			;1f17	78		x
	or c			;1f18	b1		.
	jr nz,l1f10h		;1f19	20 f5		  .
	ret			;1f1b	c9		.
sub_1f1ch:
	push hl			;1f1c	e5		.
	push bc			;1f1d	c5		.
	rst 30h			;1f1e	f7		.
	ld b,b			;1f1f	40		@
	ld (de),a		;1f20	12		.
	rlca			;1f21	07		.
	pop bc			;1f22	c1		.
	pop hl			;1f23	e1		.
	ret			;1f24	c9		.
sub_1f25h:
	ld de,0f6c0h		;1f25	11 c0 f6	. . .
sub_1f28h:
	call sub_205fh		;1f28	cd 5f 20	. _  
	xor a			;1f2b	af		.
	call sub_1e0fh		;1f2c	cd 0f 1e	. . .
	call sub_1e0fh		;1f2f	cd 0f 1e	. . .
	ld hl,ExtMenuTable	;1f32	21 a9 e9	! . .
	ld b,028h		;1f35	06 28		. (
l1f37h:
	ld (hl),a		;1f37	77		w
	inc hl			;1f38	23		#
	djnz l1f37h		;1f39	10 fc		. .
l1f3bh:
	set 3,(iy+014h)		;1f3b	fd cb 14 de	. . . .
	ld a,081h		;1f3f	3e 81		> .
	ret			;1f41	c9		.
	ld hl,0e028h		;1f42	21 28 e0	! ( .
	bit 3,(hl)		;1f45	cb 5e		. ^
	ld a,02eh		;1f47	3e 2e		> .
	ret z			;1f49	c8		.
	ld a,e			;1f4a	7b		{
	and 0feh		;1f4b	e6 fe		. .
	or d			;1f4d	b2		.
	ld a,00ch		;1f4e	3e 0c		> .
	ret nz			;1f50	c0		.
	ld a,081h		;1f51	3e 81		> .
	res 4,(hl)		;1f53	cb a6		. .
	bit 0,e			;1f55	cb 43		. C
	ret z			;1f57	c8		.
	set 4,(hl)		;1f58	cb e6		. .
	ret			;1f5a	c9		.
sub_1f5bh:
	call l0ee6h		;1f5b	cd e6 0e	. . .
	push hl			;1f5e	e5		.
l1f5fh:
	call sub_1f9bh		;1f5f	cd 9b 1f	. . .
	ld a,02fh		;1f62	3e 2f		> /
	jp c,ExitToError	;1f64	da 06 0b	. . .
	inc hl			;1f67	23		#
	call sub_0d3eh		;1f68	cd 3e 0d	. > .
	jr nz,l1f5fh		;1f6b	20 f2		  .
	pop hl			;1f6d	e1		.
sub_1f6eh:
	ld de,TokenTable	;1f6e	11 a3 23	. . #
	call sub_1f78h		;1f71	cd 78 1f	. x .
	ret nz			;1f74	c0		.
	ld de,0f6c0h		;1f75	11 c0 f6	. . .
sub_1f78h:
	ld a,(de)		;1f78	1a		.
	inc de			;1f79	13		.
	ld c,a			;1f7a	4f		O
	ld a,(de)		;1f7b	1a		.
	inc de			;1f7c	13		.
	ld b,a			;1f7d	47		G
	or c			;1f7e	b1		.
	ret z			;1f7f	c8		.
	push hl			;1f80	e5		.
l1f81h:
	ld a,(de)		;1f81	1a		.
	and 07fh		;1f82	e6 7f		. .
	cp 020h			;1f84	fe 20		.  
	jr c,l1f92h		;1f86	38 0a		8 .
	cp (hl)			;1f88	be		.
	inc de			;1f89	13		.
	inc hl			;1f8a	23		#
	jr z,l1f81h		;1f8b	28 f4		( .
l1f8dh:
	pop hl			;1f8d	e1		.
	ld e,c			;1f8e	59		Y
	ld d,b			;1f8f	50		P
	jr sub_1f78h		;1f90	18 e6		. .
l1f92h:
	call sub_0d3eh		;1f92	cd 3e 0d	. > .
	jr nz,l1f8dh		;1f95	20 f6		  .
	inc hl			;1f97	23		#
	pop bc			;1f98	c1		.
	or a			;1f99	b7		.
	ret			;1f9a	c9		.
sub_1f9bh:
	cp 030h			;1f9b	fe 30		. 0
	ret c			;1f9d	d8		.
	cp 03ah			;1f9e	fe 3a		. :
	ccf			;1fa0	3f		?
	ret nc			;1fa1	d0		.
	cp 041h			;1fa2	fe 41		. A
	ret c			;1fa4	d8		.
	cp 05bh			;1fa5	fe 5b		. [
	ccf			;1fa7	3f		?
	ret nc			;1fa8	d0		.
	cp 061h			;1fa9	fe 61		. a
	ret c			;1fab	d8		.
	cp 07bh			;1fac	fe 7b		. {
	ccf			;1fae	3f		?
	ret			;1faf	c9		.
	call sub_1f5bh		;1fb0	cd 5b 1f	. [ .
	jp nz,l2148h		;1fb3	c2 48 21	. H !
sub_1fb6h:
	ld de,(0e11ch)		;1fb6	ed 5b 1c e1	. [ . .
	inc de			;1fba	13		.
	inc de			;1fbb	13		.
	push hl			;1fbc	e5		.
	ld bc,ExtMenuTable	;1fbd	01 a9 e9	. . .
	ld a,073h		;1fc0	3e 73		> s
	cp (hl)			;1fc2	be		.
	jr nz,l1fcdh		;1fc3	20 08		  .
	ld a,074h		;1fc5	3e 74		> t
l1fc7h:
	inc hl			;1fc7	23		#
	cp (hl)			;1fc8	be		.
	jr nz,l2001h		;1fc9	20 36		  6
	jr l1fd9h		;1fcb	18 0c		. .
l1fcdh:
	ld bc,CommandTable	;1fcd	01 bd e9	. . .
	ld a,062h		;1fd0	3e 62		> b
	cp (hl)			;1fd2	be		.
	jr nz,l2001h		;1fd3	20 2c		  ,
	ld a,072h		;1fd5	3e 72		> r
	jr l1fc7h		;1fd7	18 ee		. .
l1fd9h:
	inc hl			;1fd9	23		#
	ld a,(hl)		;1fda	7e		~
	sub 030h		;1fdb	d6 30		. 0
	jr c,l2001h		;1fdd	38 22		8 "
	cp 00ah			;1fdf	fe 0a		. .
	jr nc,l2001h		;1fe1	30 1e		0 .
	inc hl			;1fe3	23		#
	push bc			;1fe4	c5		.
	ld c,a			;1fe5	4f		O
	call sub_0d3eh		;1fe6	cd 3e 0d	. > .
	jr nz,l2000h		;1fe9	20 15		  .
	sla c			;1feb	cb 21		. !
	ld b,000h		;1fed	06 00		. .
	pop hl			;1fef	e1		.
	add hl,bc		;1ff0	09		.
	ex (sp),hl		;1ff1	e3		.
	call sub_2002h		;1ff2	cd 02 20	. .  
	pop hl			;1ff5	e1		.
	ld de,(0e9d1h)		;1ff6	ed 5b d1 e9	. [ . .
	ld (hl),e		;1ffa	73		s
	inc hl			;1ffb	23		#
	ld (hl),d		;1ffc	72		r
l1ffdh:
	jp l1f3bh		;1ffd	c3 3b 1f	. ; .
l2000h:
	pop hl			;2000	e1		.
l2001h:
	pop hl			;2001	e1		.
sub_2002h:
	ld a,(hl)		;2002	7e		~
	or 080h			;2003	f6 80		. .
	call sub_1e0fh		;2005	cd 0f 1e	. . .
l2008h:
	inc hl			;2008	23		#
	ld a,(hl)		;2009	7e		~
	call sub_1e0fh		;200a	cd 0f 1e	. . .
	call sub_0d3eh		;200d	cd 3e 0d	. > .
	jr nz,l2008h		;2010	20 f6		  .
	cp 00dh			;2012	fe 0d		. .
l2014h:
	ld a,030h		;2014	3e 30		> 0
	jp z,ExitToError	;2016	ca 06 0b	. . .
sub_2019h:
	ld a,001h		;2019	3e 01		> .
	dec de			;201b	1b		.
	ld (de),a		;201c	12		.
	ld (0e9d1h),de		;201d	ed 53 d1 e9	. S . .
	inc de			;2021	13		.
	inc hl			;2022	23		#
	call l0ee6h		;2023	cd e6 0e	. . .
	jr z,l2014h		;2026	28 ec		( .
l2028h:
	push de			;2028	d5		.
	call sub_1f6eh		;2029	cd 6e 1f	. n .
	ld b,a			;202c	47		G
	ld a,e			;202d	7b		{
	ld c,d			;202e	4a		J
	pop de			;202f	d1		.
	jr z,l2064h		;2030	28 32		( 2
	call sub_1e0fh		;2032	cd 0f 1e	. . .
	ld a,c			;2035	79		y
	call sub_1e0fh		;2036	cd 0f 1e	. . .
	ld a,b			;2039	78		x
	cp 00dh			;203a	fe 0d		. .
	jr z,l2043h		;203c	28 05		( .
l203eh:
	call l0ee6h		;203e	cd e6 0e	. . .
	jr nz,l2028h		;2041	20 e5		  .
l2043h:
	ld hl,l225dh		;2043	21 5d 22	! ] "
	ld a,l			;2046	7d		}
	call sub_1e0fh		;2047	cd 0f 1e	. . .
	ld a,h			;204a	7c		|
	call sub_1e0fh		;204b	cd 0f 1e	. . .
	xor a			;204e	af		.
	call sub_1e0fh		;204f	cd 0f 1e	. . .
	call sub_1e0fh		;2052	cd 0f 1e	. . .
	dec de			;2055	1b		.
	dec de			;2056	1b		.
	ld hl,(0e11ch)		;2057	2a 1c e1	* . .
	ld (hl),e		;205a	73		s
	inc hl			;205b	23		#
	ld (hl),d		;205c	72		r
	ld a,081h		;205d	3e 81		> .
sub_205fh:
	ld (0e11ch),de		;205f	ed 53 1c e1	. S . .
	ret			;2063	c9		.
l2064h:
	push de			;2064	d5		.
	call EditorSyntaxChars_end	;2065	cd f4 0d	. . .
	ld c,e			;2068	4b		K
	ld b,d			;2069	42		B
	pop de			;206a	d1		.
	jr z,l2076h		;206b	28 09		( .
	cp 00dh			;206d	fe 0d		. .
	jr nz,l2073h		;206f	20 02		  .
	ld a,031h		;2071	3e 31		> 1
l2073h:
	jp ExitToError		;2073	c3 06 0b	. . .
l2076h:
	push hl			;2076	e5		.
	ld hl,l2340h		;2077	21 40 23	! @ #
	ld a,l			;207a	7d		}
	call sub_1e0fh		;207b	cd 0f 1e	. . .
	ld a,h			;207e	7c		|
	call sub_1e0fh		;207f	cd 0f 1e	. . .
	ld a,(0e024h)		;2082	3a 24 e0	: $ .
	call sub_1e0fh		;2085	cd 0f 1e	. . .
	ld a,c			;2088	79		y
	call sub_1e0fh		;2089	cd 0f 1e	. . .
	ld a,b			;208c	78		x
	call sub_1e0fh		;208d	cd 0f 1e	. . .
	pop hl			;2090	e1		.
	jr l203eh		;2091	18 ab		. .
	call sub_1f5bh		;2093	cd 5b 1f	. [ .
	ld a,031h		;2096	3e 31		> 1
	ret z			;2098	c8		.
	dec hl			;2099	2b		+
	call l0ee6h		;209a	cd e6 0e	. . .
	ld a,00bh		;209d	3e 0b		> .
	ret nz			;209f	c0		.
	ex de,hl		;20a0	eb		.
	ld a,(hl)		;20a1	7e		~
	or a			;20a2	b7		.
	jr z,l20bdh		;20a3	28 18		( .
	ld a,032h		;20a5	3e 32		> 2
	ret z			;20a7	c8		.
	call sub_20aeh		;20a8	cd ae 20	. .  
	ld a,006h		;20ab	3e 06		> .
	ret			;20ad	c9		.
sub_20aeh:
	ld de,l20ffh		;20ae	11 ff 20	. .  
	push de			;20b1	d5		.
	ld de,0de17h		;20b2	11 17 de	. . .
	ld (0e3bch),de		;20b5	ed 53 bc e3	. S . .
	ld a,020h		;20b9	3e 20		>  
	ld b,02bh		;20bb	06 2b		. +
l20bdh:
	ld (de),a		;20bd	12		.
	inc de			;20be	13		.
	djnz l20bdh		;20bf	10 fc		. .
	set 6,(iy+009h)		;20c1	fd cb 09 f6	. . . .
	ld a,024h		;20c5	3e 24		> $
	rst 10h			;20c7	d7		.
sub_20c8h:
	call sub_2112h		;20c8	cd 12 21	. . !
	ld a,03ah		;20cb	3e 3a		> :
	rst 10h			;20cd	d7		.
l20ceh:
	ld a,020h		;20ce	3e 20		>  
	rst 10h			;20d0	d7		.
	call sub_2121h		;20d1	cd 21 21	. ! !
	push hl			;20d4	e5		.
	ld hl,l225dh		;20d5	21 5d 22	! ] "
	or a			;20d8	b7		.
	sbc hl,bc		;20d9	ed 42		. B
	jr z,l20edh		;20db	28 10		( .
	ld hl,l2340h		;20dd	21 40 23	! @ #
	or a			;20e0	b7		.
	sbc hl,bc		;20e1	ed 42		. B
	jr z,l20efh		;20e3	28 0a		( .
	ld l,c			;20e5	69		i
	ld h,b			;20e6	60		`
	call sub_2112h		;20e7	cd 12 21	. . !
l20eah:
	pop hl			;20ea	e1		.
	jr l20ceh		;20eb	18 e1		. .
l20edh:
	pop hl			;20ed	e1		.
	ret			;20ee	c9		.
l20efh:
	pop hl			;20ef	e1		.
	inc hl			;20f0	23		#
	ld e,(hl)		;20f1	5e		^
	call sub_2121h		;20f2	cd 21 21	. ! !
	ld a,e			;20f5	7b		{
	or a			;20f6	b7		.
	push hl			;20f7	e5		.
	jr z,l2126h		;20f8	28 2c		( ,
	call sub_2130h		;20fa	cd 30 21	. 0 !
	jr l20eah		;20fd	18 eb		. .
l20ffh:
	res 6,(iy+009h)		;20ff	fd cb 09 b6	. . . .
	ret			;2103	c9		.
l2104h:
	ld e,(hl)		;2104	5e		^
	inc hl			;2105	23		#
	ld d,(hl)		;2106	56		V
	inc hl			;2107	23		#
	ld a,e			;2108	7b		{
	or d			;2109	b2		.
	ret z			;210a	c8		.
	rst 20h			;210b	e7		.
	adc a,l			;210c	8d		.
	call sub_2117h		;210d	cd 17 21	. . !
	ex de,hl		;2110	eb		.
	ret			;2111	c9		.
sub_2112h:
	dec hl			;2112	2b		+
	bit 7,(hl)		;2113	cb 7e		. ~
	jr z,sub_2112h		;2115	28 fb		( .
sub_2117h:
	ld a,(hl)		;2117	7e		~
	and 07fh		;2118	e6 7f		. .
	cp 020h			;211a	fe 20		.  
	ret c			;211c	d8		.
	rst 10h			;211d	d7		.
	inc hl			;211e	23		#
	jr sub_2117h		;211f	18 f6		. .
sub_2121h:
	inc hl			;2121	23		#
	ld c,(hl)		;2122	4e		N
	inc hl			;2123	23		#
	ld b,(hl)		;2124	46		F
	ret			;2125	c9		.
l2126h:
	ld a,022h		;2126	3e 22		> "
	push af			;2128	f5		.
	rst 10h			;2129	d7		.
	ld a,c			;212a	79		y
	rst 10h			;212b	d7		.
	pop af			;212c	f1		.
	rst 10h			;212d	d7		.
	jr l20eah		;212e	18 ba		. .
sub_2130h:
	ld hl,sub_162ch		;2130	21 2c 16	! , .
	push hl			;2133	e5		.
	ld d,a			;2134	57		W
	ld e,025h		;2135	1e 25		. %
	cp 002h			;2137	fe 02		. .
	ret z			;2139	c8		.
	ld e,040h		;213a	1e 40		. @
	cp 008h			;213c	fe 08		. .
	ret z			;213e	c8		.
	ld e,02eh		;213f	1e 2e		. .
	cp 00ah			;2141	fe 0a		. .
	ret z			;2143	c8		.
	ld de,01023h		;2144	11 23 10	. # .
	ret			;2147	c9		.
l2148h:
	ld (0e9d3h),de		;2148	ed 53 d3 e9	. S . .
	ld hl,(0e11ch)		;214c	2a 1c e1	* . .
	push hl			;214f	e5		.
	ex de,hl		;2150	eb		.
l2151h:
	bit 7,(hl)		;2151	cb 7e		. ~
	dec hl			;2153	2b		+
	jr z,l2151h		;2154	28 fb		( .
	dec hl			;2156	2b		+
	ld (0e9d5h),hl		;2157	22 d5 e9	" . .
	ld h,b			;215a	60		`
	ld l,c			;215b	69		i
	call sub_1fb6h		;215c	cd b6 1f	. . .
	ld hl,(0e9d5h)		;215f	2a d5 e9	* . .
	ld e,(hl)		;2162	5e		^
	inc hl			;2163	23		#
	ld d,(hl)		;2164	56		V
	dec hl			;2165	2b		+
	ex de,hl		;2166	eb		.
	push de			;2167	d5		.
	push hl			;2168	e5		.
	push hl			;2169	e5		.
	ld b,h			;216a	44		D
	ld c,l			;216b	4d		M
	ld hl,(0e11ch)		;216c	2a 1c e1	* . .
	or a			;216f	b7		.
	sbc hl,bc		;2170	ed 42		. B
	ld b,h			;2172	44		D
	ld c,l			;2173	4d		M
	inc bc			;2174	03		.
	inc bc			;2175	03		.
	pop hl			;2176	e1		.
	ldir			;2177	ed b0		. .
	dec de			;2179	1b		.
	dec de			;217a	1b		.
	ld (0e11ch),de		;217b	ed 53 1c e1	. S . .
	pop de			;217f	d1		.
	pop hl			;2180	e1		.
	or a			;2181	b7		.
	sbc hl,de		;2182	ed 52		. R
	ld b,h			;2184	44		D
	ld c,l			;2185	4d		M
	pop hl			;2186	e1		.
	add hl,bc		;2187	09		.
	inc hl			;2188	23		#
l2189h:
	inc hl			;2189	23		#
	ld a,(hl)		;218a	7e		~
	cp 020h			;218b	fe 20		.  
	jr nc,l2189h		;218d	30 fa		0 .
	ld (0e9d7h),hl		;218f	22 d7 e9	" . .
	ld hl,0f6c0h		;2192	21 c0 f6	! . .
l2195h:
	ld e,(hl)		;2195	5e		^
	inc hl			;2196	23		#
	ld d,(hl)		;2197	56		V
	ld a,d			;2198	7a		z
	or e			;2199	b3		.
	jr z,l21d7h		;219a	28 3b		( ;
	push hl			;219c	e5		.
	ld hl,(0e9d5h)		;219d	2a d5 e9	* . .
	or a			;21a0	b7		.
	sbc hl,de		;21a1	ed 52		. R
	jr nc,l21aeh		;21a3	30 09		0 .
	ld h,b			;21a5	60		`
	ld l,c			;21a6	69		i
	add hl,de		;21a7	19		.
	ex de,hl		;21a8	eb		.
	pop hl			;21a9	e1		.
	push hl			;21aa	e5		.
	ld (hl),d		;21ab	72		r
	dec hl			;21ac	2b		+
	ld (hl),e		;21ad	73		s
l21aeh:
	pop hl			;21ae	e1		.
	inc hl			;21af	23		#
l21b0h:
	ld a,(hl)		;21b0	7e		~
	inc hl			;21b1	23		#
	cp 020h			;21b2	fe 20		.  
	jr nc,l21b0h		;21b4	30 fa		0 .
l21b6h:
	ld e,(hl)		;21b6	5e		^
	inc hl			;21b7	23		#
	ld d,(hl)		;21b8	56		V
	inc hl			;21b9	23		#
	push hl			;21ba	e5		.
	ld hl,l2340h		;21bb	21 40 23	! @ #
	or a			;21be	b7		.
	sbc hl,de		;21bf	ed 52		. R
	jr nz,l21c9h		;21c1	20 06		  .
	pop hl			;21c3	e1		.
	inc hl			;21c4	23		#
	inc hl			;21c5	23		#
	inc hl			;21c6	23		#
	jr l21b6h		;21c7	18 ed		. .
l21c9h:
	ld hl,l225dh		;21c9	21 5d 22	! ] "
	or a			;21cc	b7		.
	sbc hl,de		;21cd	ed 52		. R
	pop hl			;21cf	e1		.
	jr z,l2195h		;21d0	28 c3		( .
	call sub_21f3h		;21d2	cd f3 21	. . !
	jr l21b6h		;21d5	18 df		. .
l21d7h:
	ld hl,ExtMenuTable	;21d7	21 a9 e9	! . .
	call sub_21e0h		;21da	cd e0 21	. . !
	ld hl,CommandTable	;21dd	21 bd e9	! . .
sub_21e0h:
	ld a,00ah		;21e0	3e 0a		> .
l21e2h:
	push af			;21e2	f5		.
	ld e,(hl)		;21e3	5e		^
	inc hl			;21e4	23		#
	ld d,(hl)		;21e5	56		V
	inc hl			;21e6	23		#
	ld a,d			;21e7	7a		z
	or e			;21e8	b3		.
	call nz,sub_21f3h	;21e9	c4 f3 21	. . !
	pop af			;21ec	f1		.
	dec a			;21ed	3d		=
	jr nz,l21e2h		;21ee	20 f2		  .
	ld a,088h		;21f0	3e 88		> .
	ret			;21f2	c9		.
sub_21f3h:
	push hl			;21f3	e5		.
	ld hl,(0e9d3h)		;21f4	2a d3 e9	* . .
	or a			;21f7	b7		.
	sbc hl,de		;21f8	ed 52		. R
	jr z,l220eh		;21fa	28 12		( .
	jr nc,l220ch		;21fc	30 0e		0 .
	ld a,(de)		;21fe	1a		.
	or a			;21ff	b7		.
	jr z,l220ch		;2200	28 0a		( .
	ld h,b			;2202	60		`
	ld l,c			;2203	69		i
	add hl,de		;2204	19		.
	ex de,hl		;2205	eb		.
l2206h:
	pop hl			;2206	e1		.
	push hl			;2207	e5		.
	dec hl			;2208	2b		+
	ld (hl),d		;2209	72		r
	dec hl			;220a	2b		+
	ld (hl),e		;220b	73		s
l220ch:
	pop hl			;220c	e1		.
	ret			;220d	c9		.
l220eh:
	ld de,(0e9d7h)		;220e	ed 5b d7 e9	. [ . .
	jr l2206h		;2212	18 f2		. .
	set 4,(iy+009h)		;2214	fd cb 09 e6	. . . .
	call InitErrorWindow	;2218	cd 95 3a	. . :
	ld bc,l3300h		;221b	01 00 33	. . 3
	rst 8			;221e	cf		.
	rst 20h			;221f	e7		.
	dec c			;2220	0d		.
	adc a,l			;2221	8d		.
	ld hl,0f7bfh		;2222	21 bf f7	! . .
	ld de,(0e11ch)		;2225	ed 5b 1c e1	. [ . .
	scf			;2229	37		7
	sbc hl,de		;222a	ed 52		. R
	ld (0dda7h),hl		;222c	22 a7 dd	" . .
	ld bc,03400h		;222f	01 00 34	. . 4
	rst 8			;2232	cf		.
	ld a,020h		;2233	3e 20		>  
	rst 10h			;2235	d7		.
	ld bc,l3500h		;2236	01 00 35	. . 5
	rst 8			;2239	cf		.
	rst 20h			;223a	e7		.
	dec c			;223b	0d		.
	adc a,l			;223c	8d		.
	ld hl,0f6c0h		;223d	21 c0 f6	! . .
l2240h:
	ld c,(hl)		;2240	4e		N
	inc hl			;2241	23		#
	ld b,(hl)		;2242	46		F
	ld a,b			;2243	78		x
	or c			;2244	b1		.
	jr z,l2256h		;2245	28 0f		( .
	push bc			;2247	c5		.
	inc hl			;2248	23		#
	inc hl			;2249	23		#
	call sub_20c8h		;224a	cd c8 20	. .  
	ld a,00dh		;224d	3e 0d		> .
	rst 10h			;224f	d7		.
	call sub_37b9h		;2250	cd b9 37	. . 7
	pop hl			;2253	e1		.
	jr l2240h		;2254	18 ea		. .
l2256h:
	res 4,(iy+009h)		;2256	fd cb 09 a6	. . . .
	ld a,081h		;225a	3e 81		> .
	ret			;225c	c9		.
l225dh:
	nop			;225d	00		.
	pop hl			;225e	e1		.
	pop hl			;225f	e1		.
	pop hl			;2260	e1		.
l2261h:
	pop de			;2261	d1		.
	pop hl			;2262	e1		.
	ld a,l			;2263	7d		}
	or h			;2264	b4		.
	jp nz,l1dfbh		;2265	c2 fb 1d	. . .
	bit 5,(iy+014h)		;2268	fd cb 14 6e	. . . n
	ret nz			;226c	c0		.
	push hl			;226d	e5		.
	push de			;226e	d5		.
	ld (0e9ddh),sp		;226f	ed 73 dd e9	. s . .
	ld hl,(0e9dbh)		;2273	2a db e9	* . .
	ld de,(0e11ch)		;2276	ed 5b 1c e1	. [ . .
	inc de			;227a	13		.
	inc de			;227b	13		.
	inc de			;227c	13		.
	inc de			;227d	13		.
	or a			;227e	b7		.
	sbc hl,de		;227f	ed 52		. R
	ld a,036h		;2281	3e 36		> 6
	jp nz,l2307h		;2283	c2 07 23	. . #
	call sub_237fh		;2286	cd 7f 23	. . #
	ld a,c			;2289	79		y
	or b			;228a	b0		.
	pop hl			;228b	e1		.
	pop hl			;228c	e1		.
	ret z			;228d	c8		.
	pop hl			;228e	e1		.
	pop hl			;228f	e1		.
	pop af			;2290	f1		.
	ld l,a			;2291	6f		o
	ld a,085h		;2292	3e 85		> .
	jp l01d5h		;2294	c3 d5 01	. . .
	bit 3,(iy+014h)		;2297	fd cb 14 5e	. . . ^
	ld a,02eh		;229b	3e 2e		> .
	ret z			;229d	c8		.
	push hl			;229e	e5		.
	ld hl,(UserPc)		;229f	2a 6b dd	* k .
	call FetchPrefixBytes	;22a2	cd 96 2f	. . /
	pop hl			;22a5	e1		.
	ld de,(0e11ch)		;22a6	ed 5b 1c e1	. [ . .
	push de			;22aa	d5		.
	push hl			;22ab	e5		.
	inc de			;22ac	13		.
	inc de			;22ad	13		.
	ld hl,l2301h		;22ae	21 01 23	! . #
	ld b,007h		;22b1	06 07		. .
l22b3h:
	ld a,(hl)		;22b3	7e		~
	call sub_1e0fh		;22b4	cd 0f 1e	. . .
	inc hl			;22b7	23		#
	djnz l22b3h		;22b8	10 f9		. .
	pop hl			;22ba	e1		.
	dec hl			;22bb	2b		+
	push de			;22bc	d5		.
	call sub_2019h		;22bd	cd 19 20	. .  
	pop de			;22c0	d1		.
	dec de			;22c1	1b		.
	ld hl,(0e11ch)		;22c2	2a 1c e1	* . .
	ex (sp),hl		;22c5	e3		.
	ld (0e11ch),hl		;22c6	22 1c e1	" . .
	xor a			;22c9	af		.
	ld (hl),a		;22ca	77		w
	inc hl			;22cb	23		#
	ld (hl),a		;22cc	77		w
	set 5,(iy+014h)		;22cd	fd cb 14 ee	. . . .
	pop hl			;22d1	e1		.
	call sub_1de8h		;22d2	cd e8 1d	. . .
	res 5,(iy+014h)		;22d5	fd cb 14 ae	. . . .
	call InitErrorWindow	;22d9	cd 95 3a	. . :
	ld bc,l3700h		;22dc	01 00 37	. . 7
	rst 8			;22df	cf		.
	ld a,00dh		;22e0	3e 0d		> .
	rst 10h			;22e2	d7		.
	ld de,(0e9d9h)		;22e3	ed 5b d9 e9	. [ . .
	inc de			;22e7	13		.
l22e8h:
	ld hl,(0e9dbh)		;22e8	2a db e9	* . .
	or a			;22eb	b7		.
	sbc hl,de		;22ec	ed 52		. R
	ld a,081h		;22ee	3e 81		> .
	ret z			;22f0	c8		.
	push de			;22f1	d5		.
	ld a,00dh		;22f2	3e 0d		> .
	rst 10h			;22f4	d7		.
	call sub_237fh		;22f5	cd 7f 23	. . #
	call sub_1614h		;22f8	cd 14 16	. . .
	call sub_37b9h		;22fb	cd b9 37	. . 7
	pop de			;22fe	d1		.
	jr l22e8h		;22ff	18 e7		. .
l2301h:
	xor d			;2301	aa		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg2302: message fragment eval*2' of the nearby
; expression evaluator.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg2302' (start 0x2302 end 0x2309)
Msg2302_start:
	defb 065h		;2302	65		e
	defb 076h		;2303	76		v
	defb 061h		;2304	61		a
	defb 06ch		;2305	6c		l
	defb 02ah		;2306	2a		*
l2307h:
	defb 032h		;2307	32		2
	defb 0a7h		;2308	a7		.
Msg2302_end:
	defb 0ddh,0edh,07bh ;illegal sequence	;2309	dd ed 7b	. . {
	jp (ix)			;230c	dd e9		. .
	call sub_0550h		;230e	cd 50 05	. P .
	call InitErrorWindow	;2311	cd 95 3a	. . :
	ld bc,03800h		;2314	01 00 38	. . 8
	rst 8			;2317	cf		.
	rst 20h			;2318	e7		.
	dec c			;2319	0d		.
	adc a,l			;231a	8d		.
l231bh:
	pop hl			;231b	e1		.
	push hl			;231c	e5		.
	ld de,l2340h		;231d	11 40 23	. @ #
	or a			;2320	b7		.
	sbc hl,de		;2321	ed 52		. R
	pop hl			;2323	e1		.
	jr z,l2332h		;2324	28 0c		( .
	push hl			;2326	e5		.
	rst 20h			;2327	e7		.
	ld l,c			;2328	69		i
	ld l,(hl)		;2329	6e		n
	and b			;232a	a0		.
	pop hl			;232b	e1		.
	call sub_2112h		;232c	cd 12 21	. . !
	ld a,00dh		;232f	3e 0d		> .
	rst 10h			;2331	d7		.
l2332h:
	call sub_37b9h		;2332	cd b9 37	. . 7
	pop hl			;2335	e1		.
	ld a,h			;2336	7c		|
	or l			;2337	b5		.
	jr nz,l231bh		;2338	20 e1		  .
	ld a,(0dda7h)		;233a	3a a7 dd	: . .
	jp ExitToError		;233d	c3 06 0b	. . .
l2340h:
	nop			;2340	00		.
	pop ix			;2341	dd e1		. .
	pop de			;2343	d1		.
	pop hl			;2344	e1		.
	call sub_2121h		;2345	cd 21 21	. ! !
	inc hl			;2348	23		#
	push hl			;2349	e5		.
	push de			;234a	d5		.
	push ix			;234b	dd e5		. .
l234dh:
	push hl			;234d	e5		.
	push de			;234e	d5		.
	ld hl,(0e9dbh)		;234f	2a db e9	* . .
	push hl			;2352	e5		.
	inc hl			;2353	23		#
	inc hl			;2354	23		#
	ld de,0f7bfh		;2355	11 bf f7	. . .
	or a			;2358	b7		.
	sbc hl,de		;2359	ed 52		. R
	ld a,039h		;235b	3e 39		> 9
	jr nc,l2307h		;235d	30 a8		0 .
	pop hl			;235f	e1		.
	ld (hl),c		;2360	71		q
	inc hl			;2361	23		#
	ld (hl),b		;2362	70		p
	inc hl			;2363	23		#
l2364h:
	ld (0e9dbh),hl		;2364	22 db e9	" . .
	pop de			;2367	d1		.
	pop hl			;2368	e1		.
	ret			;2369	c9		.
l236ah:
	push hl			;236a	e5		.
	ld l,c			;236b	69		i
	ld h,b			;236c	60		`
	rst 28h			;236d	ef		.
	pop hl			;236e	e1		.
	ld c,a			;236f	4f		O
	ld b,000h		;2370	06 00		. .
	jr l234dh		;2372	18 d9		. .
l2374h:
	push hl			;2374	e5		.
	ld l,c			;2375	69		i
	ld h,b			;2376	60		`
	rst 28h			;2377	ef		.
	ld c,a			;2378	4f		O
	inc hl			;2379	23		#
	rst 28h			;237a	ef		.
	ld b,a			;237b	47		G
	pop hl			;237c	e1		.
	jr l234dh		;237d	18 ce		. .
sub_237fh:
	push hl			;237f	e5		.
	push de			;2380	d5		.
	ld hl,(0e9dbh)		;2381	2a db e9	* . .
	dec hl			;2384	2b		+
	push hl			;2385	e5		.
	or a			;2386	b7		.
	ld de,(0e9d9h)		;2387	ed 5b d9 e9	. [ . .
	sbc hl,de		;238b	ed 52		. R
	ld a,03ah		;238d	3e 3a		> :
	jp z,l2307h		;238f	ca 07 23	. . #
	pop hl			;2392	e1		.
	ld b,(hl)		;2393	46		F
	dec hl			;2394	2b		+
	ld c,(hl)		;2395	4e		N
	jr l2364h		;2396	18 cc		. .
sub_2398h:
	call sub_237fh		;2398	cd 7f 23	. . #
	push bc			;239b	c5		.
	call sub_237fh		;239c	cd 7f 23	. . #
	pop de			;239f	d1		.
	ld l,e			;23a0	6b		k
	ld h,d			;23a1	62		b
	ret			;23a2	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; TokenTable - the v4.01 command tokenizer: a linked list of
; records {u16 next; u8 token; name[] NUL-terminated; handler
; snippet}, headed here ($23A3; cf. ld hl,$23A3 at $3AC8) and
; chained up to $27DA.  Named records implement the display
; commands (MEM/REG/DIS/SCR/SYS1/SYS2), memory configuration
; (FF/AM/RAM/ALL/EM/EM!), port I/O (IN/OUT) and PAUSE (RST 30h
; RAM hook with inline args); the many empty-name records are
; single-character/suffix matchers (R, D, F, C, X, Y, E, L, N, P,
; ">", "@", "!", ND, OR, OT, IT, DDR, CF, VF, EG, YS1, YS2, WAP,
; VER, ROP, EY, UP, OP).  ScanTokens ($2C7F) walks the list;
; each Tok* block below is one record of this list.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
TokenTable:

; BLOCK 'Tok23A3' (start 0x23a3 end 0x23a9)
Tok23A3_start:
	defb 0aah		;23a3	aa		.
	defb 023h		;23a4	23		#
	defb 0ceh		;23a5	ce		.
	defb 04fh		;23a6	4f		O
	defb 050h		;23a7	50		P
	defb 000h		;23a8	00		.
Tok23A3_end:
	ret			;23a9	c9		.

; BLOCK 'Tok23AA' (start 0x23aa end 0x23b0)
Tok23AA_start:
	defb 0b8h		;23aa	b8		.
	defb 023h		;23ab	23		#
	defb 0c4h		;23ac	c4		.
	defb 055h		;23ad	55		U
	defb 050h		;23ae	50		P
	defb 000h		;23af	00		.
Tok23AA_end:
	call sub_237fh		;23b0	cd 7f 23	. . #
	call l234dh		;23b3	cd 4d 23	. M #
	jr l234dh		;23b6	18 95		. .

; BLOCK 'Tok23B8' (start 0x23b8 end 0x23bf)
Tok23B8_start:
	defb 0cch		;23b8	cc		.
	defb 023h		;23b9	23		#
	defb 0d3h		;23ba	d3		.
	defb 057h		;23bb	57		W
	defb 041h		;23bc	41		A
	defb 050h		;23bd	50		P
	defb 000h		;23be	00		.
Tok23B8_end:
	call sub_2398h		;23bf	cd 98 23	. . #
	push bc			;23c2	c5		.
	ld c,l			;23c3	4d		M
	ld b,h			;23c4	44		D
	call l234dh		;23c5	cd 4d 23	. M #
	pop bc			;23c8	c1		.
l23c9h:
	jp l234dh		;23c9	c3 4d 23	. M #

; BLOCK 'Tok23CC' (start 0x23cc end 0x23d3)
Tok23CC_start:
	defb 0e0h		;23cc	e0		.
	defb 023h		;23cd	23		#
	defb 0cfh		;23ce	cf		.
	defb 056h		;23cf	56		V
	defb 045h		;23d0	45		E
	defb 052h		;23d1	52		R
	defb 000h		;23d2	00		.
Tok23CC_end:
	call sub_237fh		;23d3	cd 7f 23	. . #
	push bc			;23d6	c5		.
	call Tok23AA_end	;23d7	cd b0 23	. . #
	pop bc			;23da	c1		.
	call l234dh		;23db	cd 4d 23	. M #
	jr Tok23B8_end		;23de	18 df		. .

; BLOCK 'Tok23E0' (start 0x23e0 end 0x23e6)
Tok23E0_start:
	defb 0feh		;23e0	fe		.
	defb 023h		;23e1	23		#
	defb 0d2h		;23e2	d2		.
	defb 04fh		;23e3	4f		O
	defb 054h		;23e4	54		T
	defb 000h		;23e5	00		.
Tok23E0_end:
	call sub_2398h		;23e6	cd 98 23	. . #
	push hl			;23e9	e5		.
	push bc			;23ea	c5		.
	call sub_237fh		;23eb	cd 7f 23	. . #
	pop hl			;23ee	e1		.
	push bc			;23ef	c5		.
	ld c,l			;23f0	4d		M
	ld b,h			;23f1	44		D
	call l234dh		;23f2	cd 4d 23	. M #
	pop de			;23f5	d1		.
	pop bc			;23f6	c1		.
	push de			;23f7	d5		.
	call l234dh		;23f8	cd 4d 23	. M #
	pop bc			;23fb	c1		.
	jr l23c9h		;23fc	18 cb		. .

; BLOCK 'Tok23FE' (start 0x23fe end 0x2405)
Tok23FE_start:
	defb 008h		;23fe	08		.
	defb 024h		;23ff	24		$
	defb 0c4h		;2400	c4		.
	defb 052h		;2401	52		R
	defb 04fh		;2402	4f		O
	defb 050h		;2403	50		P
	defb 000h		;2404	00		.
Tok23FE_end:
	jp sub_237fh		;2405	c3 7f 23	. . #

; BLOCK 'Tok2408' (start 0x2408 end 0x240c)
Tok2408_start:
	defb 014h		;2408	14		.
	defb 024h		;2409	24		$
	defb 0abh		;240a	ab		.
	defb 000h		;240b	00		.
Tok2408_end:
	call sub_2398h		;240c	cd 98 23	. . #
	add hl,bc		;240f	09		.
l2410h:
	ld c,l			;2410	4d		M
	ld b,h			;2411	44		D
	jr l23c9h		;2412	18 b5		. .

; BLOCK 'Tok2414' (start 0x2414 end 0x2418)
Tok2414_start:
	defb 022h		;2414	22		"
	defb 024h		;2415	24		$
	defb 0adh		;2416	ad		.
	defb 000h		;2417	00		.
Tok2414_end:
	call sub_2398h		;2418	cd 98 23	. . #
	ld h,b			;241b	60		`
	ld l,c			;241c	69		i
	or a			;241d	b7		.
	sbc hl,de		;241e	ed 52		. R
	jr l2410h		;2420	18 ee		. .

; BLOCK 'Tok2422' (start 0x2422 end 0x2426)
Tok2422_start:
	defb 039h		;2422	39		9
	defb 024h		;2423	24		$
	defb 0afh		;2424	af		.
	defb 000h		;2425	00		.
Tok2422_end:
	call sub_2398h		;2426	cd 98 23	. . #
	ld l,c			;2429	69		i
	ld h,b			;242a	60		`
	call sub_0e73h		;242b	cd 73 0e	. s .
	bit 2,(iy+00eh)		;242e	fd cb 0e 56	. . . V
	jr z,l2456h		;2432	28 22		( "
	ld a,040h		;2434	3e 40		> @
	jp l2307h		;2436	c3 07 23	. . #

; BLOCK 'Tok2439' (start 0x2439 end 0x243d)
Tok2439_start:
	defb 047h		;2439	47		G
	defb 024h		;243a	24		$
	defb 0aah		;243b	aa		.
	defb 000h		;243c	00		.
Tok2439_end:
	call sub_2398h		;243d	cd 98 23	. . #
	ld l,c			;2440	69		i
	ld h,b			;2441	60		`
	call sub_0e85h		;2442	cd 85 0e	. . .
	jr l2410h		;2445	18 c9		. .

; BLOCK 'Tok2447' (start 0x2447 end 0x244d)
Tok2447_start:
	defb 059h		;2447	59		Y
	defb 024h		;2448	24		$
	defb 0c1h		;2449	c1		.
	defb 04eh		;244a	4e		N
	defb 044h		;244b	44		D
	defb 000h		;244c	00		.
Tok2447_end:
	call sub_2398h		;244d	cd 98 23	. . #
	ld a,c			;2450	79		y
	and l			;2451	a5		.
	ld c,a			;2452	4f		O
	ld a,b			;2453	78		x
	and h			;2454	a4		.
	ld b,a			;2455	47		G
l2456h:
	jp l234dh		;2456	c3 4d 23	. M #

; BLOCK 'Tok2459' (start 0x2459 end 0x245e)
Tok2459_start:
	defb 069h		;2459	69		i
	defb 024h		;245a	24		$
	defb 0cfh		;245b	cf		.
	defb 052h		;245c	52		R
	defb 000h		;245d	00		.
Tok2459_end:
	call sub_2398h		;245e	cd 98 23	. . #
	ld a,c			;2461	79		y
	or l			;2462	b5		.
	ld c,a			;2463	4f		O
	ld a,b			;2464	78		x
	or h			;2465	b4		.
	ld b,a			;2466	47		G
	jr l2456h		;2467	18 ed		. .

; BLOCK 'Tok2469' (start 0x2469 end 0x246f)
Tok2469_start:
	defb 07ah		;2469	7a		z
	defb 024h		;246a	24		$
	defb 0d8h		;246b	d8		.
	defb 04fh		;246c	4f		O
	defb 052h		;246d	52		R
	defb 000h		;246e	00		.
Tok2469_end:
	call sub_2398h		;246f	cd 98 23	. . #
	ld a,c			;2472	79		y
	xor l			;2473	ad		.
	ld c,a			;2474	4f		O
	ld a,b			;2475	78		x
	xor h			;2476	ac		.
	ld b,a			;2477	47		G
	jr l2456h		;2478	18 dc		. .

; BLOCK 'Tok247A' (start 0x247a end 0x2480)
Tok247A_start:
	defb 08bh		;247a	8b		.
	defb 024h		;247b	24		$
	defb 0ceh		;247c	ce		.
	defb 04fh		;247d	4f		O
	defb 054h		;247e	54		T
	defb 000h		;247f	00		.
Tok247A_end:
	call sub_237fh		;2480	cd 7f 23	. . #
	ld a,b			;2483	78		x
	cpl			;2484	2f		/
	ld b,a			;2485	47		G
	ld a,c			;2486	79		y
	cpl			;2487	2f		/
	ld c,a			;2488	4f		O
	jr l2456h		;2489	18 cb		. .

; BLOCK 'Tok248B' (start 0x248b end 0x248f)
Tok248B_start:
	defb 094h		;248b	94		.
	defb 024h		;248c	24		$
	defb 0bdh		;248d	bd		.
	defb 000h		;248e	00		.
Tok248B_end:
	call Tok2414_end	;248f	cd 18 24	. . $
	jr Tok24C6_end		;2492	18 37		. 7

; BLOCK 'Tok2494' (start 0x2494 end 0x2499)
Tok2494_start:
	defb 0a7h		;2494	a7		.
	defb 024h		;2495	24		$
	defb 0b0h		;2496	b0		.
	defb 03eh		;2497	3e		>
	defb 000h		;2498	00		.
Tok2494_end:
	call sub_237fh		;2499	cd 7f 23	. . #
	ld a,c			;249c	79		y
	or b			;249d	b0		.
l249eh:
	ld bc,WriteAnyBankByte	;249e	01 00 00	. . .
	jr z,l24a4h		;24a1	28 01		( .
l24a3h:
	inc bc			;24a3	03		.
l24a4h:
	jp l234dh		;24a4	c3 4d 23	. M #

; BLOCK 'Tok24A7' (start 0x24a7 end 0x24ab)
Tok24A7_start:
	defb 0bah		;24a7	ba		.
	defb 024h		;24a8	24		$
	defb 0bch		;24a9	bc		.
	defb 000h		;24aa	00		.
Tok24A7_end:
	call sub_2398h		;24ab	cd 98 23	. . #
	ld l,c			;24ae	69		i
	ld h,b			;24af	60		`
	or a			;24b0	b7		.
	sbc hl,de		;24b1	ed 52		. R
l24b3h:
	ld bc,WriteAnyBankByte	;24b3	01 00 00	. . .
	jr c,l24a3h		;24b6	38 eb		8 .
	jr l24a4h		;24b8	18 ea		. .

; BLOCK 'Tok24BA' (start 0x24ba end 0x24be)
Tok24BA_start:
	defb 0c6h		;24ba	c6		.
	defb 024h		;24bb	24		$
	defb 0beh		;24bc	be		.
	defb 000h		;24bd	00		.
Tok24BA_end:
	call sub_2398h		;24be	cd 98 23	. . #
	or a			;24c1	b7		.
	sbc hl,bc		;24c2	ed 42		. B
	jr l24b3h		;24c4	18 ed		. .

; BLOCK 'Tok24C6' (start 0x24c6 end 0x24cb)
Tok24C6_start:
	defb 0d8h		;24c6	d8		.
	defb 024h		;24c7	24		$
	defb 0b0h		;24c8	b0		.
	defb 03dh		;24c9	3d		=
	defb 000h		;24ca	00		.
Tok24C6_end:
	call sub_237fh		;24cb	cd 7f 23	. . #
	ld a,c			;24ce	79		y
	or b			;24cf	b0		.
	inc bc			;24d0	03		.
	jr z,l24d6h		;24d1	28 03		( .
	ld bc,WriteAnyBankByte	;24d3	01 00 00	. . .
l24d6h:
	jr l24a4h		;24d6	18 cc		. .

; BLOCK 'Tok24D8' (start 0x24d8 end 0x24dd)
Tok24D8_start:
	defb 0e2h		;24d8	e2		.
	defb 024h		;24d9	24		$
	defb 0bch		;24da	bc		.
	defb 03dh		;24db	3d		=
	defb 000h		;24dc	00		.
Tok24D8_end:
	call Tok24BA_end	;24dd	cd be 24	. . $
	jr Tok24C6_end		;24e0	18 e9		. .

; BLOCK 'Tok24E2' (start 0x24e2 end 0x24e7)
Tok24E2_start:
	defb 0ech		;24e2	ec		.
	defb 024h		;24e3	24		$
	defb 0beh		;24e4	be		.
	defb 03dh		;24e5	3d		=
	defb 000h		;24e6	00		.
Tok24E2_end:
	call Tok24A7_end	;24e7	cd ab 24	. . $
	jr Tok24C6_end		;24ea	18 df		. .

; BLOCK 'Tok24EC' (start 0x24ec end 0x24f1)
Tok24EC_start:
	defb 0f6h		;24ec	f6		.
	defb 024h		;24ed	24		$
	defb 0bch		;24ee	bc		.
	defb 03eh		;24ef	3e		>
	defb 000h		;24f0	00		.
Tok24EC_end:
	call Tok2414_end	;24f1	cd 18 24	. . $
	jr Tok2494_end		;24f4	18 a3		. .

; BLOCK 'Tok24F6' (start 0x24f6 end 0x24fa)
Tok24F6_start:
	defb 005h		;24f6	05		.
	defb 025h		;24f7	25		%
	defb 0a6h		;24f8	a6		.
	defb 000h		;24f9	00		.
Tok24F6_end:
	call sub_2398h		;24fa	cd 98 23	. . #
	ld a,b			;24fd	78		x
	or c			;24fe	b1		.
	jr z,l249eh		;24ff	28 9d		( .
	ld a,l			;2501	7d		}
	or h			;2502	b4		.
	jr l249eh		;2503	18 99		. .

; BLOCK 'Tok2505' (start 0x2505 end 0x250a)
Tok2505_start:
	defb 010h		;2505	10		.
	defb 025h		;2506	25		%
	defb 0c3h		;2507	c3		.
	defb 040h		;2508	40		@
	defb 000h		;2509	00		.
Tok2505_end:
	call sub_237fh		;250a	cd 7f 23	. . #
	jp l236ah		;250d	c3 6a 23	. j #

; BLOCK 'Tok2510' (start 0x2510 end 0x2514)
Tok2510_start:
	defb 01ah		;2510	1a		.
	defb 025h		;2511	25		%
	defb 0c0h		;2512	c0		.
	defb 000h		;2513	00		.
Tok2510_end:
	call sub_237fh		;2514	cd 7f 23	. . #
	jp l2374h		;2517	c3 74 23	. t #

; BLOCK 'Tok251A' (start 0x251a end 0x2520)
Tok251A_start:
	defb 03dh		;251a	3d		=
	defb 025h		;251b	25		%
	defb 0c2h		;251c	c2		.
	defb 049h		;251d	49		I
	defb 054h		;251e	54		T
	defb 000h		;251f	00		.
Tok251A_end:
	call sub_2398h		;2520	cd 98 23	. . #
	ld h,000h		;2523	26 00		& .
	ld a,l			;2525	7d		}
	and 00fh		;2526	e6 0f		. .
	ld de,l0001h		;2528	11 01 00	. . .
	jr l2531h		;252b	18 04		. .
l252dh:
	sla e			;252d	cb 23		. #
	rl d			;252f	cb 12		. .
l2531h:
	dec a			;2531	3d		=
	jp p,l252dh		;2532	f2 2d 25	. - %
	ld a,e			;2535	7b		{
	and c			;2536	a1		.
	ld c,a			;2537	4f		O
	ld a,d			;2538	7a		z
	and b			;2539	a0		.
	or c			;253a	b1		.
	jr l255eh		;253b	18 21		. !

; BLOCK 'Tok253D' (start 0x253d end 0x2544)
Tok253D_start:
	defb 055h		;253d	55		U
	defb 025h		;253e	25		%
	defb 0c1h		;253f	c1		.
	defb 044h		;2540	44		D
	defb 044h		;2541	44		D
	defb 052h		;2542	52		R
	defb 000h		;2543	00		.
Tok253D_end:
	ld a,(iy+004h)		;2544	fd 7e 04	. ~ .
	ld bc,WriteAnyBankByte	;2547	01 00 00	. . .
	and 003h		;254a	e6 03		. .
	jr z,l2552h		;254c	28 04		( .
	ld bc,(0ddd2h)		;254e	ed 4b d2 dd	. K . .
l2552h:
	jp l234dh		;2552	c3 4d 23	. M #

; BLOCK 'Tok2555' (start 0x2555 end 0x255a)
Tok2555_start:
	defb 061h		;2555	61		a
	defb 025h		;2556	25		%
	defb 0d2h		;2557	d2		.
	defb 044h		;2558	44		D
	defb 000h		;2559	00		.
Tok2555_end:
	bit 0,(iy+004h)		;255a	fd cb 04 46	. . . F
l255eh:
	jp l249eh		;255e	c3 9e 24	. . $

; BLOCK 'Tok2561' (start 0x2561 end 0x2566)
Tok2561_start:
	defb 06ch		;2561	6c		l
	defb 025h		;2562	25		%
	defb 0d7h		;2563	d7		.
	defb 052h		;2564	52		R
	defb 000h		;2565	00		.
Tok2561_end:
	bit 1,(iy+004h)		;2566	fd cb 04 4e	. . . N
	jr l255eh		;256a	18 f2		. .

; BLOCK 'Tok256C' (start 0x256c end 0x2572)
Tok256C_start:
	defb 079h		;256c	79		y
	defb 025h		;256d	25		%
	defb 0c1h		;256e	c1		.
	defb 043h		;256f	43		C
	defb 046h		;2570	46		F
	defb 000h		;2571	00		.
Tok256C_end:
	ld a,(0e018h)		;2572	3a 18 e0	: . .
	and 003h		;2575	e6 03		. .
	jr l255eh		;2577	18 e5		. .

; BLOCK 'Tok2579' (start 0x2579 end 0x257e)
Tok2579_start:
	defb 086h		;2579	86		.
	defb 025h		;257a	25		%
	defb 0c3h		;257b	c3		.
	defb 046h		;257c	46		F
	defb 000h		;257d	00		.
Tok2579_end:
	ld a,001h		;257e	3e 01		> .
l2580h:
	ld hl,0dd7fh		;2580	21 7f dd	! . .
	and (hl)		;2583	a6		.
	jr l255eh		;2584	18 d8		. .

; BLOCK 'Tok2586' (start 0x2586 end 0x258b)
Tok2586_start:
	defb 08fh		;2586	8f		.
	defb 025h		;2587	25		%
	defb 0ceh		;2588	ce		.
	defb 046h		;2589	46		F
	defb 000h		;258a	00		.
Tok2586_end:
	ld a,002h		;258b	3e 02		> .
	jr l2580h		;258d	18 f1		. .

; BLOCK 'Tok258F' (start 0x258f end 0x2595)
Tok258F_start:
	defb 099h		;258f	99		.
	defb 025h		;2590	25		%
	defb 0d0h		;2591	d0		.
	defb 056h		;2592	56		V
	defb 046h		;2593	46		F
	defb 000h		;2594	00		.
Tok258F_end:
	ld a,004h		;2595	3e 04		> .
	jr l2580h		;2597	18 e7		. .

; BLOCK 'Tok2599' (start 0x2599 end 0x259e)
Tok2599_start:
	defb 0a2h		;2599	a2		.
	defb 025h		;259a	25		%
	defb 0c8h		;259b	c8		.
	defb 046h		;259c	46		F
	defb 000h		;259d	00		.
Tok2599_end:
	ld a,010h		;259e	3e 10		> .
	jr l2580h		;25a0	18 de		. .

; BLOCK 'Tok25A2' (start 0x25a2 end 0x25a7)
Tok25A2_start:
	defb 0abh		;25a2	ab		.
	defb 025h		;25a3	25		%
	defb 0dah		;25a4	da		.
	defb 046h		;25a5	46		F
	defb 000h		;25a6	00		.
Tok25A2_end:
	ld a,040h		;25a7	3e 40		> @
	jr l2580h		;25a9	18 d5		. .

; BLOCK 'Tok25AB' (start 0x25ab end 0x25b0)
Tok25AB_start:
	defb 0b4h		;25ab	b4		.
	defb 025h		;25ac	25		%
	defb 0d3h		;25ad	d3		.
	defb 046h		;25ae	46		F
	defb 000h		;25af	00		.
Tok25AB_end:
	ld a,080h		;25b0	3e 80		> .
	jr l2580h		;25b2	18 cc		. .

; BLOCK 'Tok25B4' (start 0x25b4 end 0x25ba)
Tok25B4_start:
	defb 0cch		;25b4	cc		.
	defb 025h		;25b5	25		%
	defb 0d2h		;25b6	d2		.
	defb 045h		;25b7	45		E
	defb 047h		;25b8	47		G
	defb 000h		;25b9	00		.
Tok25B4_end:
	call sub_237fh		;25ba	cd 7f 23	. . #
l25bdh:
	ld hl,0dd69h		;25bd	21 69 dd	! i .
	ld b,000h		;25c0	06 00		. .
	add hl,bc		;25c2	09		.
	ld c,(hl)		;25c3	4e		N
	inc hl			;25c4	23		#
	ld b,(hl)		;25c5	46		F
	jr l2552h		;25c6	18 8a		. .
sub_25c8h:
	pop hl			;25c8	e1		.
	ld c,(hl)		;25c9	4e		N
	jr l25bdh		;25ca	18 f1		. .

; BLOCK 'Tok25CC' (start 0x25cc end 0x25d3)
Tok25CC_start:
	defb 0e6h		;25cc	e6		.
	defb 025h		;25cd	25		%
	defb 0c3h		;25ce	c3		.
	defb 052h		;25cf	52		R
	defb 045h		;25d0	45		E
	defb 047h		;25d1	47		G
	defb 000h		;25d2	00		.
Tok25CC_end:
	call sub_237fh		;25d3	cd 7f 23	. . #
l25d6h:
	ld hl,0dd69h		;25d6	21 69 dd	! i .
	ld b,000h		;25d9	06 00		. .
	add hl,bc		;25db	09		.
	ld c,(hl)		;25dc	4e		N
	ld b,000h		;25dd	06 00		. .
	jp l234dh		;25df	c3 4d 23	. M #
sub_25e2h:
	pop hl			;25e2	e1		.
	ld c,(hl)		;25e3	4e		N
	jr l25d6h		;25e4	18 f0		. .

; BLOCK 'Tok25E6' (start 0x25e6 end 0x25eb)
Tok25E6_start:
	defb 0efh		;25e6	ef		.
	defb 025h		;25e7	25		%
	defb 0d3h		;25e8	d3		.
	defb 050h		;25e9	50		P
	defb 000h		;25ea	00		.
Tok25E6_end:
	call sub_25c8h		;25eb	cd c8 25	. . %
	inc b			;25ee	04		.

; BLOCK 'Tok25EF' (start 0x25ef end 0x25f4)
Tok25EF_start:
	defb 0f8h		;25ef	f8		.
	defb 025h		;25f0	25		%
	defb 0d0h		;25f1	d0		.
	defb 043h		;25f2	43		C
	defb 000h		;25f3	00		.
Tok25EF_end:
	call sub_25c8h		;25f4	cd c8 25	. . %
	ld (bc),a		;25f7	02		.

; BLOCK 'Tok25F8' (start 0x25f8 end 0x25fd)
Tok25F8_start:
	defb 001h		;25f8	01		.
	defb 026h		;25f9	26		&
	defb 0c9h		;25fa	c9		.
	defb 058h		;25fb	58		X
	defb 000h		;25fc	00		.
Tok25F8_end:
	call sub_25c8h		;25fd	cd c8 25	. . %
	defb 006h		;2600	06		.

; BLOCK 'Tok2601' (start 0x2601 end 0x2606)
Tok2601_start:
	defb 00ah		;2601	0a		.
	defb 026h		;2602	26		&
	defb 0c9h		;2603	c9		.
	defb 059h		;2604	59		Y
	defb 000h		;2605	00		.
Tok2601_end:
	call sub_25c8h		;2606	cd c8 25	. . %
	ex af,af'		;2609	08		.

; BLOCK 'Tok260A' (start 0x260a end 0x260f)
Tok260A_start:
	defb 013h		;260a	13		.
	defb 026h		;260b	26		&
	defb 0c2h		;260c	c2		.
	defb 043h		;260d	43		C
	defb 000h		;260e	00		.
Tok260A_end:
	call sub_25c8h		;260f	cd c8 25	. . %
	defb 00eh		;2612	0e		.

; BLOCK 'Tok2613' (start 0x2613 end 0x2618)
Tok2613_start:
	defb 01ch		;2613	1c		.
	defb 026h		;2614	26		&
	defb 0c4h		;2615	c4		.
	defb 045h		;2616	45		E
	defb 000h		;2617	00		.
Tok2613_end:
	call sub_25c8h		;2618	cd c8 25	. . %
	inc c			;261b	0c		.

; BLOCK 'Tok261C' (start 0x261c end 0x2621)
Tok261C_start:
	defb 025h		;261c	25		%
	defb 026h		;261d	26		&
	defb 0c8h		;261e	c8		.
	defb 04ch		;261f	4c		L
	defb 000h		;2620	00		.
Tok261C_end:
	call sub_25c8h		;2621	cd c8 25	. . %
	ld a,(bc)		;2624	0a		.

; BLOCK 'Tok2625' (start 0x2625 end 0x262a)
Tok2625_start:
	defb 02eh		;2625	2e		.
	defb 026h		;2626	26		&
	defb 0c1h		;2627	c1		.
	defb 046h		;2628	46		F
	defb 000h		;2629	00		.
Tok2625_end:
	call sub_25c8h		;262a	cd c8 25	. . %
	defb 016h		;262d	16		.

; BLOCK 'Tok262E' (start 0x262e end 0x2632)
Tok262E_start:
	defb 036h		;262e	36		6
	defb 026h		;262f	26		&
	defb 0c3h		;2630	c3		.
	defb 000h		;2631	00		.
Tok262E_end:
	call sub_25e2h		;2632	cd e2 25	. . %
	defb 00eh		;2635	0e		.

; BLOCK 'Tok2636' (start 0x2636 end 0x263a)
Tok2636_start:
	defb 03eh		;2636	3e		>
	defb 026h		;2637	26		&
	defb 0c2h		;2638	c2		.
	defb 000h		;2639	00		.
Tok2636_end:
	call sub_25e2h		;263a	cd e2 25	. . %
	rrca			;263d	0f		.

; BLOCK 'Tok263E' (start 0x263e end 0x2642)
Tok263E_start:
	defb 046h		;263e	46		F
	defb 026h		;263f	26		&
	defb 0c5h		;2640	c5		.
	defb 000h		;2641	00		.
Tok263E_end:
	call sub_25e2h		;2642	cd e2 25	. . %
	inc c			;2645	0c		.

; BLOCK 'Tok2646' (start 0x2646 end 0x264a)
Tok2646_start:
	defb 04eh		;2646	4e		N
	defb 026h		;2647	26		&
	defb 0c4h		;2648	c4		.
	defb 000h		;2649	00		.
Tok2646_end:
	call sub_25e2h		;264a	cd e2 25	. . %
	dec c			;264d	0d		.

; BLOCK 'Tok264E' (start 0x264e end 0x2652)
Tok264E_start:
	defb 056h		;264e	56		V
	defb 026h		;264f	26		&
	defb 0cch		;2650	cc		.
	defb 000h		;2651	00		.
Tok264E_end:
	call sub_25e2h		;2652	cd e2 25	. . %
	ld a,(bc)		;2655	0a		.

; BLOCK 'Tok2656' (start 0x2656 end 0x265a)
Tok2656_start:
	defb 05eh		;2656	5e		^
	defb 026h		;2657	26		&
	defb 0c8h		;2658	c8		.
	defb 000h		;2659	00		.
Tok2656_end:
	call sub_25e2h		;265a	cd e2 25	. . %
	dec bc			;265d	0b		.

; BLOCK 'Tok265E' (start 0x265e end 0x2662)
Tok265E_start:
	defb 066h		;265e	66		f
	defb 026h		;265f	26		&
	defb 0c6h		;2660	c6		.
	defb 000h		;2661	00		.
Tok265E_end:
	call sub_25e2h		;2662	cd e2 25	. . %
	defb 016h		;2665	16		.

; BLOCK 'Tok2666' (start 0x2666 end 0x266a)
Tok2666_start:
	defb 06eh		;2666	6e		n
	defb 026h		;2667	26		&
	defb 0c1h		;2668	c1		.
	defb 000h		;2669	00		.
Tok2666_end:
	call sub_25e2h		;266a	cd e2 25	. . %
	rla			;266d	17		.

; BLOCK 'Tok266E' (start 0x266e end 0x2674)
Tok266E_start:
	defb 078h		;266e	78		x
	defb 026h		;266f	26		&
	defb 0cdh		;2670	cd		.
	defb 045h		;2671	45		E
	defb 04dh		;2672	4d		M
	defb 000h		;2673	00		.
Tok266E_end:
	call sub_25c8h		;2674	cd c8 25	. . %
	nop			;2677	00		.

; BLOCK 'Tok2678' (start 0x2678 end 0x267f)
Tok2678_start:
	defb 09ah		;2678	9a		.
	defb 026h		;2679	26		&
	defb 0c3h		;267a	c3		.
	defb 041h		;267b	41		A
	defb 04ch		;267c	4c		L
	defb 04ch		;267d	4c		L
	defb 000h		;267e	00		.
Tok2678_end:
	push iy			;267f	fd e5		. .
	push ix			;2681	dd e5		. .
	call sub_268bh		;2683	cd 8b 26	. . &
	pop ix			;2686	dd e1		. .
	pop iy			;2688	fd e1		. .
	ret			;268a	c9		.
sub_268bh:
	call sub_237fh		;268b	cd 7f 23	. . #
	push bc			;268e	c5		.
	ld ix,0dd69h		;268f	dd 21 69 dd	. ! i .
	ld de,l234dh		;2693	11 4d 23	. M #
	ld hl,sub_237fh		;2696	21 7f 23	! . #
	ret			;2699	c9		.

; BLOCK 'Tok269A' (start 0x269a end 0x269e)
Tok269A_start:
	defb 0a7h		;269a	a7		.
	defb 026h		;269b	26		&
	defb 0a1h		;269c	a1		.
	defb 000h		;269d	00		.
Tok269A_end:
	call sub_2398h		;269e	cd 98 23	. . #
	ld a,c			;26a1	79		y
	rst 0			;26a2	c7		.
	inc hl			;26a3	23		#
	ld a,b			;26a4	78		x
l26a5h:
	rst 0			;26a5	c7		.
	ret			;26a6	c9		.

; BLOCK 'Tok26A7' (start 0x26a7 end 0x26ac)
Tok26A7_start:
	defb 0b2h		;26a7	b2		.
	defb 026h		;26a8	26		&
	defb 0c3h		;26a9	c3		.
	defb 021h		;26aa	21		!
	defb 000h		;26ab	00		.
Tok26A7_end:
	call sub_2398h		;26ac	cd 98 23	. . #
	ld a,c			;26af	79		y
	jr l26a5h		;26b0	18 f3		. .

; BLOCK 'Tok26B2' (start 0x26b2 end 0x26b7)
Tok26B2_start:
	defb 0c0h		;26b2	c0		.
	defb 026h		;26b3	26		&
	defb 0c9h		;26b4	c9		.
	defb 046h		;26b5	46		F
	defb 000h		;26b6	00		.
Tok26B2_end:
	call sub_237fh		;26b7	cd 7f 23	. . #
	ld a,b			;26ba	78		x
	or c			;26bb	b1		.
	ret nz			;26bc	c0		.
	jp l225dh		;26bd	c3 5d 22	. ] "

; BLOCK 'Tok26C0' (start 0x26c0 end 0x26c7)
Tok26C0_start:
	defb 0cfh		;26c0	cf		.
	defb 026h		;26c1	26		&
	defb 0cdh		;26c2	cd		.
	defb 045h		;26c3	45		E
	defb 04dh		;26c4	4d		M
	defb 021h		;26c5	21		!
	defb 000h		;26c6	00		.
Tok26C0_end:
	call sub_237fh		;26c7	cd 7f 23	. . #
	ld (0dd69h),bc		;26ca	ed 43 69 dd	. C i .
	ret			;26ce	c9		.

; BLOCK 'Tok26CF' (start 0x26cf end 0x26d8)
Tok26CF_start:
	defb 0eah		;26cf	ea		.
	defb 026h		;26d0	26		&
	defb 0bfh		;26d1	bf		.
	defb 050h		;26d2	50		P
	defb 041h		;26d3	41		A
	defb 055h		;26d4	55		U
	defb 053h		;26d5	53		S
	defb 045h		;26d6	45		E
	defb 000h		;26d7	00		.
Tok26CF_end:
	call sub_237fh		;26d8	cd 7f 23	. . #
	ld a,c			;26db	79		y
	or b			;26dc	b0		.
	ret z			;26dd	c8		.
	rst 30h			;26de	f7		.
	ld c,d			;26df	4a		J
	rlca			;26e0	07		.
	ld b,0feh		;26e1	06 fe		. .
	inc bc			;26e3	03		.
	ret nz			;26e4	c0		.
	ld a,09ah		;26e5	3e 9a		> .
	jp l2307h		;26e7	c3 07 23	. . #

; BLOCK 'Tok26EA' (start 0x26ea end 0x26f0)
Tok26EA_start:
	defb 0fah		;26ea	fa		.
	defb 026h		;26eb	26		&
	defb 0cbh		;26ec	cb		.
	defb 045h		;26ed	45		E
	defb 059h		;26ee	59		Y
	defb 000h		;26ef	00		.
Tok26EA_end:
	rst 30h			;26f0	f7		.
	ld c,d			;26f1	4a		J
	rlca			;26f2	07		.
	ld b,04fh		;26f3	06 4f		. O
	ld b,000h		;26f5	06 00		. .
l26f7h:
	jp l234dh		;26f7	c3 4d 23	. M #

; BLOCK 'Tok26FA' (start 0x26fa end 0x2701)
Tok26FA_start:
	defb 00ch		;26fa	0c		.
	defb 027h		;26fb	27		'
	defb 0bfh		;26fc	bf		.
	defb 04dh		;26fd	4d		M
	defb 045h		;26fe	45		E
	defb 04dh		;26ff	4d		M
	defb 000h		;2700	00		.
Tok26FA_end:
	call sub_2736h		;2701	cd 36 27	. 6 '
	and 0cfh		;2704	e6 cf		. .
l2706h:
	ld (0e01eh),a		;2706	32 1e e0	2 . .
	jp sub_2ca8h		;2709	c3 a8 2c	. . ,

; BLOCK 'Tok270C' (start 0x270c end 0x2713)
Tok270C_start:
	defb 01ah		;270c	1a		.
	defb 027h		;270d	27		'
	defb 0bfh		;270e	bf		.
	defb 052h		;270f	52		R
	defb 045h		;2710	45		E
	defb 047h		;2711	47		G
	defb 000h		;2712	00		.
Tok270C_end:
	call sub_2736h		;2713	cd 36 27	. 6 '
	and 0f5h		;2716	e6 f5		. .
	jr l2706h		;2718	18 ec		. .

; BLOCK 'Tok271A' (start 0x271a end 0x2721)
Tok271A_start:
	defb 028h		;271a	28		(
	defb 027h		;271b	27		'
	defb 0bfh		;271c	bf		.
	defb 044h		;271d	44		D
	defb 049h		;271e	49		I
	defb 053h		;271f	53		S
	defb 000h		;2720	00		.
Tok271A_end:
	call sub_2736h		;2721	cd 36 27	. 6 '
	and 0bfh		;2724	e6 bf		. .
	jr l2706h		;2726	18 de		. .

; BLOCK 'Tok2728' (start 0x2728 end 0x272f)
Tok2728_start:
	defb 042h		;2728	42		B
	defb 027h		;2729	27		'
	defb 0bfh		;272a	bf		.
	defb 053h		;272b	53		S
	defb 043h		;272c	43		C
	defb 052h		;272d	52		R
	defb 000h		;272e	00		.
Tok2728_end:
	call sub_2736h		;272f	cd 36 27	. 6 '
	and 080h		;2732	e6 80		. .
	jr l2706h		;2734	18 d0		. .
sub_2736h:
	call sub_237fh		;2736	cd 7f 23	. . #
	ld a,c			;2739	79		y
	or b			;273a	b0		.
	pop hl			;273b	e1		.
	ret z			;273c	c8		.
	push hl			;273d	e5		.
	ld a,(0e01eh)		;273e	3a 1e e0	: . .
	ret			;2741	c9		.

; BLOCK 'Tok2742' (start 0x2742 end 0x2749)
Tok2742_start:
	defb 050h		;2742	50		P
	defb 027h		;2743	27		'
	defb 0d3h		;2744	d3		.
	defb 059h		;2745	59		Y
	defb 053h		;2746	53		S
	defb 031h		;2747	31		1
	defb 000h		;2748	00		.
Tok2742_end:
	ld c,(iy-002h)		;2749	fd 4e fe	. N .
l274ch:
	ld b,000h		;274c	06 00		. .
	jr l26f7h		;274e	18 a7		. .

; BLOCK 'Tok2750' (start 0x2750 end 0x2757)
Tok2750_start:
	defb 05ch		;2750	5c		\
	defb 027h		;2751	27		'
	defb 0d3h		;2752	d3		.
	defb 059h		;2753	59		Y
	defb 053h		;2754	53		S
	defb 032h		;2755	32		2
	defb 000h		;2756	00		.
Tok2750_end:
	ld c,(iy-001h)		;2757	fd 4e ff	. N .
	jr l274ch		;275a	18 f0		. .

; BLOCK 'Tok275C' (start 0x275c end 0x2764)
Tok275C_start:
	defb 06bh		;275c	6b		k
	defb 027h		;275d	27		'
	defb 0a1h		;275e	a1		.
	defb 053h		;275f	53		S
	defb 059h		;2760	59		Y
	defb 053h		;2761	53		S
	defb 031h		;2762	31		1
	defb 000h		;2763	00		.
Tok275C_end:
	call sub_237fh		;2764	cd 7f 23	. . #
	ld (iy-002h),c		;2767	fd 71 fe	. q .
	ret			;276a	c9		.

; BLOCK 'Tok276B' (start 0x276b end 0x2773)
Tok276B_start:
	defb 07ah		;276b	7a		z
	defb 027h		;276c	27		'
	defb 0a1h		;276d	a1		.
	defb 053h		;276e	53		S
	defb 059h		;276f	59		Y
	defb 053h		;2770	53		S
	defb 032h		;2771	32		2
	defb 000h		;2772	00		.
Tok276B_end:
	call sub_237fh		;2773	cd 7f 23	. . #
	ld (iy-001h),c		;2776	fd 71 ff	. q .
	ret			;2779	c9		.

; BLOCK 'Tok277A' (start 0x277a end 0x2780)
Tok277A_start:
	defb 08ah		;277a	8a		.
	defb 027h		;277b	27		'
	defb 0c9h		;277c	c9		.
	defb 046h		;277d	46		F
	defb 046h		;277e	46		F
	defb 000h		;277f	00		.
Tok277A_end:
	ld a,(UserIff)		;2780	3a 83 dd	: . .
	and 004h		;2783	e6 04		. .
	rrca			;2785	0f		.
	rrca			;2786	0f		.
	jp 026f4h		;2787	c3 f4 26	. . &

; BLOCK 'Tok278A' (start 0x278a end 0x2790)
Tok278A_start:
	defb 096h		;278a	96		.
	defb 027h		;278b	27		'
	defb 0d2h		;278c	d2		.
	defb 041h		;278d	41		A
	defb 04dh		;278e	4d		M
	defb 000h		;278f	00		.
Tok278A_end:
	call ScreenBankCheck	;2790	cd f9 36	. . 6
	jp 026f4h		;2793	c3 f4 26	. . &

; BLOCK 'Tok2796' (start 0x2796 end 0x279d)
Tok2796_start:
	defb 0a4h		;2796	a4		.
	defb 027h		;2797	27		'
	defb 0a1h		;2798	a1		.
	defb 052h		;2799	52		R
	defb 041h		;279a	41		A
	defb 04dh		;279b	4d		M
	defb 000h		;279c	00		.
Tok2796_end:
	call sub_237fh		;279d	cd 7f 23	. . #
	ld l,c			;27a0	69		i
	jp l3732h		;27a1	c3 32 37	. 2 7

; BLOCK 'Tok27A4' (start 0x27a4 end 0x27a9)
Tok27A4_start:
	defb 0b0h		;27a4	b0		.
	defb 027h		;27a5	27		'
	defb 0c9h		;27a6	c9		.
	defb 04eh		;27a7	4e		N
	defb 000h		;27a8	00		.
Tok27A4_end:
	call sub_237fh		;27a9	cd 7f 23	. . #
	in c,(c)		;27ac	ed 48		. H
	jr l274ch		;27ae	18 9c		. .

; BLOCK 'Tok27B0' (start 0x27b0 end 0x27b6)
Tok27B0_start:
	defb 0bch		;27b0	bc		.
	defb 027h		;27b1	27		'
	defb 0cfh		;27b2	cf		.
	defb 055h		;27b3	55		U
	defb 054h		;27b4	54		T
	defb 000h		;27b5	00		.
Tok27B0_end:
	call sub_2398h		;27b6	cd 98 23	. . #
	out (c),l		;27b9	ed 69		. i
	ret			;27bb	c9		.

; BLOCK 'Tok27BC' (start 0x27bc end 0x27c2)
Tok27BC_start:
	defb 0cbh		;27bc	cb		.
	defb 027h		;27bd	27		'
	defb 0c4h		;27be	c4		.
	defb 049h		;27bf	49		I
	defb 04eh		;27c0	4e		N
	defb 000h		;27c1	00		.
Tok27BC_end:
	call sub_237fh		;27c2	cd 7f 23	. . #
	rst 18h			;27c5	df		.
	di			;27c6	f3		.
	ccf			;27c7	3f		?
	ld c,a			;27c8	4f		O
	jr l274ch		;27c9	18 81		. .

; BLOCK 'Tok27CB' (start 0x27cb end 0x27d2)
Tok27CB_start:
	defb 0dah		;27cb	da		.
	defb 027h		;27cc	27		'
	defb 0c4h		;27cd	c4		.
	defb 04fh		;27ce	4f		O
	defb 055h		;27cf	55		U
	defb 054h		;27d0	54		T
	defb 000h		;27d1	00		.
Tok27CB_end:
	call sub_2398h		;27d2	cd 98 23	. . #
	ld a,l			;27d5	7d		}
	rst 18h			;27d6	df		.
	ret p			;27d7	f0		.
	ccf			;27d8	3f		?
	ret			;27d9	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; regptrs: data tail of the tokenizer - 13 row/column bytes for
; the register display, then 11 window-routine pointers ($2BF8,
; $2C18, $2AAD, $2ACA, $2AED, $2BA8, $2B7D, $2A5E, $2B6E, $2BCA,
; $2A1B) behind the register-view dispatch; code resumes at $27FD
; (call $28B8).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'regptrs' (start 0x27da end 0x27fd)
regptrs_start:
	defb 000h		;27da	00		.
	defb 000h		;27db	00		.
	defb 001h		;27dc	01		.
	defb 002h		;27dd	02		.
	defb 004h		;27de	04		.
	defb 005h		;27df	05		.
	defb 006h		;27e0	06		.
	defb 007h		;27e1	07		.
	defb 00ah		;27e2	0a		.
	defb 00ch		;27e3	0c		.
	defb 00dh		;27e4	0d		.
	defb 01bh		;27e5	1b		.
l27e6h:
	defb 01eh		;27e6	1e		.
	defb 0f8h		;27e7	f8		.
	defb 02bh		;27e8	2b		+
	defb 018h		;27e9	18		.
	defb 02ch		;27ea	2c		,
	defb 0adh		;27eb	ad		.
	defb 02ah		;27ec	2a		*
	defb 0cah		;27ed	ca		.
	defb 02ah		;27ee	2a		*
	defb 0edh		;27ef	ed		.
	defb 02ah		;27f0	2a		*
	defb 0a8h		;27f1	a8		.
	defb 02bh		;27f2	2b		+
	defb 07dh		;27f3	7d		}
	defb 02bh		;27f4	2b		+
	defb 05eh		;27f5	5e		^
	defb 02ah		;27f6	2a		*
	defb 06eh		;27f7	6e		n
	defb 02bh		;27f8	2b		+
	defb 0cah		;27f9	ca		.
	defb 02bh		;27fa	2b		+
	defb 01bh		;27fb	1b		.
	defb 02ah		;27fc	2a		*
regptrs_end:
	call SaveCursorCell	;27fd	cd b8 28	. . (
sub_2800h:
	ld l,a			;2800	6f		o
	ld c,a			;2801	4f		O
	ld h,000h		;2802	26 00		& .
	add hl,hl		;2804	29		)
	add hl,hl		;2805	29		)
	add hl,hl		;2806	29		)
	ld de,(0e9e5h)		;2807	ed 5b e5 e9	. [ . .
	add hl,de		;280b	19		.
	ld a,(ix+00ah)		;280c	dd 7e 0a	. ~ .
	cp 008h			;280f	fe 08		. .
	jr nz,l2847h		;2811	20 34		  4
	push hl			;2813	e5		.
	call BitmapAddr		;2814	cd 7c 29	. | )
	pop de			;2817	d1		.
	ld b,008h		;2818	06 08		. .
l281ah:
	ld a,(de)		;281a	1a		.
	ld (hl),a		;281b	77		w
	inc de			;281c	13		.
	inc h			;281d	24		$
	djnz l281ah		;281e	10 fa		. .
l2820h:
	bit 1,(ix+007h)		;2820	dd cb 07 4e	. . . N
	jr z,l2834h		;2824	28 0e		( .
	dec h			;2826	25		%
l2827h:
	ld a,h			;2827	7c		|
	rrca			;2828	0f		.
	rrca			;2829	0f		.
	rrca			;282a	0f		.
	and 003h		;282b	e6 03		. .
	xor 058h		;282d	ee 58		. X
	ld h,a			;282f	67		g
	ld a,(ix+006h)		;2830	dd 7e 06	. ~ .
	ld (hl),a		;2833	77		w
l2834h:
	bit 1,(iy+009h)		;2834	fd cb 09 4e	. . . N
	ret nz			;2838	c0		.
	inc (ix+001h)		;2839	dd 34 01	. 4 .
	ld a,(ix+00bh)		;283c	dd 7e 0b	. ~ .
	cp (ix+001h)		;283f	dd be 01	. . .
	ret nc			;2842	d0		.
	dec (ix+001h)		;2843	dd 35 01	. 5 .
	ret			;2846	c9		.
l2847h:
	push hl			;2847	e5		.
	call WindowAddr		;2848	cd 2f 29	. / )
	pop de			;284b	d1		.
	ld a,b			;284c	78		x
	ld b,008h		;284d	06 08		. .
	or a			;284f	b7		.
	jr z,l28a8h		;2850	28 56		( V
	cp 004h			;2852	fe 04		. .
	jr c,l2896h		;2854	38 40		8 @
	jr z,l2876h		;2856	28 1e		( .
l2858h:
	ld a,(de)		;2858	1a		.
	rlca			;2859	07		.
	rlca			;285a	07		.
	push af			;285b	f5		.
	and 003h		;285c	e6 03		. .
	ld c,a			;285e	4f		O
	ld a,(hl)		;285f	7e		~
	and 0fch		;2860	e6 fc		. .
	or c			;2862	b1		.
	ld (hl),a		;2863	77		w
	inc hl			;2864	23		#
	pop af			;2865	f1		.
	and 0f0h		;2866	e6 f0		. .
	ld c,a			;2868	4f		O
	ld a,(hl)		;2869	7e		~
	and 00fh		;286a	e6 0f		. .
	or c			;286c	b1		.
	ld (hl),a		;286d	77		w
	dec hl			;286e	2b		+
	inc h			;286f	24		$
	inc de			;2870	13		.
	djnz l2858h		;2871	10 e5		. .
	jp l2820h		;2873	c3 20 28	.   (
l2876h:
	ld a,(de)		;2876	1a		.
	rlca			;2877	07		.
	rlca			;2878	07		.
	rlca			;2879	07		.
	rlca			;287a	07		.
	push af			;287b	f5		.
	and 00fh		;287c	e6 0f		. .
	ld c,a			;287e	4f		O
	ld a,(hl)		;287f	7e		~
	and 0f0h		;2880	e6 f0		. .
	or c			;2882	b1		.
	ld (hl),a		;2883	77		w
	inc hl			;2884	23		#
	pop af			;2885	f1		.
	and 0c0h		;2886	e6 c0		. .
	ld c,a			;2888	4f		O
	ld a,(hl)		;2889	7e		~
	and 03fh		;288a	e6 3f		. ?
	or c			;288c	b1		.
	ld (hl),a		;288d	77		w
	dec hl			;288e	2b		+
	inc h			;288f	24		$
	inc de			;2890	13		.
	djnz l2876h		;2891	10 e3		. .
	jp l2820h		;2893	c3 20 28	.   (
l2896h:
	ld a,(hl)		;2896	7e		~
	and 0c0h		;2897	e6 c0		. .
	ld c,a			;2899	4f		O
	ld a,(de)		;289a	1a		.
	rrca			;289b	0f		.
	rrca			;289c	0f		.
	and 03fh		;289d	e6 3f		. ?
	or c			;289f	b1		.
	ld (hl),a		;28a0	77		w
	inc h			;28a1	24		$
	inc de			;28a2	13		.
	djnz l2896h		;28a3	10 f1		. .
	jp l2820h		;28a5	c3 20 28	.   (
l28a8h:
	ld a,(hl)		;28a8	7e		~
	and 003h		;28a9	e6 03		. .
	ld c,a			;28ab	4f		O
	ld a,(de)		;28ac	1a		.
	and 0fch		;28ad	e6 fc		. .
	or c			;28af	b1		.
	ld (hl),a		;28b0	77		w
	inc h			;28b1	24		$
	inc de			;28b2	13		.
	djnz l28a8h		;28b3	10 f3		. .
	jp l2820h		;28b5	c3 20 28	.   (
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SaveCursorCell (+restore): the cursor spans 2 glyphs of a
; 6-pixel cell; the 16 covered bytes are buffered at
; CursorCellSave (E2C1) and restored when the cursor moves.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SaveCursorCell:
	set 2,(iy+009h)		;28b8	fd cb 09 d6	. . . .
	bit 0,(iy+009h)		;28bc	fd cb 09 46	. . . F
	ret z			;28c0	c8		.
	res 0,(iy+009h)		;28c1	fd cb 09 86	. . . .
l28c5h:
	push af			;28c5	f5		.
	call WindowAddr		;28c6	cd 2f 29	. / )
	ld de,CursorCellSave	;28c9	11 a4 e3	. . .
	ld b,008h		;28cc	06 08		. .
l28ceh:
	ld c,002h		;28ce	0e 02		. .
l28d0h:
	bit 0,(iy+009h)		;28d0	fd cb 09 46	. . . F
	jr z,l28dah		;28d4	28 04		( .
	ld a,(hl)		;28d6	7e		~
	ld (de),a		;28d7	12		.
	jr l28dch		;28d8	18 02		. .
l28dah:
	ld a,(de)		;28da	1a		.
	ld (hl),a		;28db	77		w
l28dch:
	inc l			;28dc	2c		,
	inc de			;28dd	13		.
	dec c			;28de	0d		.
	jr nz,l28d0h		;28df	20 ef		  .
	dec l			;28e1	2d		-
	dec l			;28e2	2d		-
	inc h			;28e3	24		$
	djnz l28ceh		;28e4	10 e8		. .
	pop af			;28e6	f1		.
	ret			;28e7	c9		.
RestartCursorBlink:
	ld a,001h		;28e8	3e 01		> .
	res 2,(iy+009h)		;28ea	fd cb 09 96	. . . .
	bit 0,(iy+009h)		;28ee	fd cb 09 46	. . . F
	ret nz			;28f2	c0		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BlinkCursor: toggle cursor visibility - v4.01 counts down 12/10
; inversions in the $E3B6 cell (state bit in $E01D); draw/erase
; goes through the cell buffer.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BlinkCursor:
	ld (0e3b6h),a		;28f3	32 b6 e3	2 . .
	ld ix,(WorkBufferPtr)	;28f6	dd 2a b7 e3	. * . .
	bit 0,(ix+007h)		;28fa	dd cb 07 46	. . . F
	ret nz			;28fe	c0		.
	ld hl,0e01dh		;28ff	21 1d e0	! . .
	bit 2,(hl)		;2902	cb 56		. V
	res 2,(hl)		;2904	cb 96		. .
	ld hl,0e3b6h		;2906	21 b6 e3	! . .
	jr nz,l292ch		;2909	20 21		  !
	dec (hl)		;290b	35		5
	ret nz			;290c	c0		.
	ld (hl),00ch		;290d	36 0c		6 .
	ld hl,0e01dh		;290f	21 1d e0	! . .
	bit 0,(hl)		;2912	cb 46		. F
	res 0,(hl)		;2914	cb 86		. .
	jr nz,l28c5h		;2916	20 ad		  .
	set 0,(hl)		;2918	cb c6		. .
	push hl			;291a	e5		.
	call l28c5h		;291b	cd c5 28	. . (
	pop hl			;291e	e1		.
	ld a,(ix+00ch)		;291f	dd 7e 0c	. ~ .
	set 1,(hl)		;2922	cb ce		. .
	push hl			;2924	e5		.
	call sub_2800h		;2925	cd 00 28	. . (
	pop hl			;2928	e1		.
	res 1,(hl)		;2929	cb 8e		. .
	ret			;292b	c9		.
l292ch:
	ld (hl),00ah		;292c	36 0a		6 .
	ret			;292e	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WindowAddr: window cursor (IX+0 row, IX+1 column) ->
; absolute screen address.  The 6-pixel font packs two glyphs
; per 8-pixel byte cell, so columns address 6-pixel units.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WindowAddr:
	ld a,(ix+002h)		;292f	dd 7e 02	. ~ .
	add a,(ix+000h)		;2932	dd 86 00	. . .
	ld h,a			;2935	67		g
	rrca			;2936	0f		.
	rrca			;2937	0f		.
	rrca			;2938	0f		.
	and 0e0h		;2939	e6 e0		. .
	ld l,a			;293b	6f		o
	ld c,(ix+001h)		;293c	dd 4e 01	. N .
	ld a,c			;293f	79		y
	add a,c			;2940	81		.
	add a,c			;2941	81		.
	add a,a			;2942	87		.
	ld c,a			;2943	4f		O
	and 007h		;2944	e6 07		. .
	ld b,a			;2946	47		G
	ld a,c			;2947	79		y
	rrca			;2948	0f		.
	rrca			;2949	0f		.
	rrca			;294a	0f		.
	add a,(ix+003h)		;294b	dd 86 03	. . .
	and 01fh		;294e	e6 1f		. .
	or l			;2950	b5		.
	ld l,a			;2951	6f		o
	ld a,h			;2952	7c		|
	and 018h		;2953	e6 18		. .
	or 040h			;2955	f6 40		. @
	ld h,a			;2957	67		g
	ret			;2958	c9		.
sub_2959h:
	ld l,(ix+000h)		;2959	dd 6e 00	. n .
	ld h,(ix+001h)		;295c	dd 66 01	. f .
	ld c,(ix+002h)		;295f	dd 4e 02	. N .
	ld b,(ix+003h)		;2962	dd 46 03	. F .
	add hl,bc		;2965	09		.
	ld c,l			;2966	4d		M
	ld b,h			;2967	44		D
	ret			;2968	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; AttrAddr: attribute address for the cursor position.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
AttrAddr:
	call sub_2959h		;2969	cd 59 29	. Y )
	ld a,c			;296c	79		y
	rrca			;296d	0f		.
	rrca			;296e	0f		.
	rrca			;296f	0f		.
	ld c,a			;2970	4f		O
	and 0e0h		;2971	e6 e0		. .
	xor b			;2973	a8		.
	ld l,a			;2974	6f		o
	ld a,c			;2975	79		y
	and 003h		;2976	e6 03		. .
	xor 058h		;2978	ee 58		. X
	ld h,a			;297a	67		g
	ret			;297b	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BitmapAddr: bitmap address for the cursor position.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BitmapAddr:
	call sub_2959h		;297c	cd 59 29	. Y )
	ld a,c			;297f	79		y
	rrca			;2980	0f		.
	rrca			;2981	0f		.
	rrca			;2982	0f		.
	and 0e0h		;2983	e6 e0		. .
	xor b			;2985	a8		.
	ld l,a			;2986	6f		o
	ld a,c			;2987	79		y
	and 018h		;2988	e6 18		. .
	xor 040h		;298a	ee 40		. @
	ld h,a			;298c	67		g
	ret			;298d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScrollUp: shift the window bitmap (and attributes, when the
; descriptor flag bit 1 is set) one row toward row 0, then blank
; the vacated bottom row via FillWholeRow.  Called by WindowNewline
; when the cursor sits on the last row - the classic text-scroll.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScrollUp:
	ld c,(ix+000h)		;298e	dd 4e 00	. N .
	ld b,(ix+001h)		;2991	dd 46 01	. F .
	push bc			;2994	c5		.
	ld (ix+000h),001h	;2995	dd 36 00 01	. 6 . .
	ld (ix+001h),000h	;2999	dd 36 01 00	. 6 . .
	ld b,(ix+004h)		;299d	dd 46 04	. F .
l29a0h:
	dec b			;29a0	05		.
	jr z,l29beh		;29a1	28 1b		( .
	push bc			;29a3	c5		.
	call BitmapAddr		;29a4	cd 7c 29	. | )
	or a			;29a7	b7		.
	call CopyCharRow	;29a8	cd d0 29	. . )
	bit 1,(ix+007h)		;29ab	dd cb 07 4e	. . . N
	jr z,l29b8h		;29af	28 07		( .
	call AttrAddr		;29b1	cd 69 29	. i )
	or a			;29b4	b7		.
	call CopyAttrRow	;29b5	cd fb 29	. . )
l29b8h:
	inc (ix+000h)		;29b8	dd 34 00	. 4 .
	pop bc			;29bb	c1		.
	jr l29a0h		;29bc	18 e2		. .
l29beh:
	ld a,(ix+004h)		;29be	dd 7e 04	. ~ .
	dec a			;29c1	3d		=
	ld (ix+000h),a		;29c2	dd 77 00	. w .
	call sub_2a81h		;29c5	cd 81 2a	. . *
	pop bc			;29c8	c1		.
	ld (ix+000h),c		;29c9	dd 71 00	. q .
	ld (ix+001h),b		;29cc	dd 70 01	. p .
	ret			;29cf	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CopyCharRow: copy one 8-line character row between adjacent
; window rows (LDIR per line, H/D advanced within the third);
; carry selects the source row: +1 (into ScrollDown) or -1.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CopyCharRow:
	push hl			;29d0	e5		.
	ld a,(ix+000h)		;29d1	dd 7e 00	. ~ .
	push af			;29d4	f5		.
	jr c,l29dah		;29d5	38 03		8 .
	dec a			;29d7	3d		=
	jr l29dbh		;29d8	18 01		. .
l29dah:
	inc a			;29da	3c		<
l29dbh:
	ld (ix+000h),a		;29db	dd 77 00	. w .
	call BitmapAddr		;29de	cd 7c 29	. | )
	pop af			;29e1	f1		.
	ld (ix+000h),a		;29e2	dd 77 00	. w .
	ld e,l			;29e5	5d		]
	ld d,h			;29e6	54		T
	pop hl			;29e7	e1		.
	ld a,008h		;29e8	3e 08		> .
l29eah:
	ld b,000h		;29ea	06 00		. .
	ld c,(ix+005h)		;29ec	dd 4e 05	. N .
	push hl			;29ef	e5		.
	push de			;29f0	d5		.
	ldir			;29f1	ed b0		. .
	pop de			;29f3	d1		.
	pop hl			;29f4	e1		.
	inc h			;29f5	24		$
	inc d			;29f6	14		.
	dec a			;29f7	3d		=
	jr nz,l29eah		;29f8	20 f0		  .
	ret			;29fa	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CopyAttrRow: the attribute-row twin of CopyCharRow - one LDIR
; of the window width, source row selected by carry.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CopyAttrRow:
	push hl			;29fb	e5		.
	ld a,(ix+000h)		;29fc	dd 7e 00	. ~ .
	push af			;29ff	f5		.
	jr c,l2a05h		;2a00	38 03		8 .
	dec a			;2a02	3d		=
	jr l2a06h		;2a03	18 01		. .
l2a05h:
	inc a			;2a05	3c		<
l2a06h:
	ld (ix+000h),a		;2a06	dd 77 00	. w .
	call AttrAddr		;2a09	cd 69 29	. i )
	pop af			;2a0c	f1		.
	ld (ix+000h),a		;2a0d	dd 77 00	. w .
	ld e,l			;2a10	5d		]
	ld d,h			;2a11	54		T
	pop hl			;2a12	e1		.
	ld b,000h		;2a13	06 00		. .
	ld c,(ix+005h)		;2a15	dd 4e 05	. N .
	ldir			;2a18	ed b0		. .
	ret			;2a1a	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScrollDown: reverse scroll - rows shift away from row 0 (the
; helpers are called with carry set), the vacated top row is
; blanked from column 0.  Used by the disassembly scroll-back
; (the caller sits just before ArmWatchpoint).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScrollDown:
	ld c,(ix+000h)		;2a1b	dd 4e 00	. N .
	ld b,(ix+001h)		;2a1e	dd 46 01	. F .
	push bc			;2a21	c5		.
	ld (ix+001h),000h	;2a22	dd 36 01 00	. 6 . .
	ld b,(ix+004h)		;2a26	dd 46 04	. F .
	dec b			;2a29	05		.
	jr z,l2a4ch		;2a2a	28 20		(  
	dec b			;2a2c	05		.
	ld (ix+000h),b		;2a2d	dd 70 00	. p .
	inc b			;2a30	04		.
l2a31h:
	push bc			;2a31	c5		.
	call BitmapAddr		;2a32	cd 7c 29	. | )
	scf			;2a35	37		7
	call CopyCharRow	;2a36	cd d0 29	. . )
	bit 1,(ix+007h)		;2a39	dd cb 07 4e	. . . N
	jr z,l2a46h		;2a3d	28 07		( .
	call AttrAddr		;2a3f	cd 69 29	. i )
	scf			;2a42	37		7
	call CopyAttrRow	;2a43	cd fb 29	. . )
l2a46h:
	dec (ix+000h)		;2a46	dd 35 00	. 5 .
	pop bc			;2a49	c1		.
	djnz l2a31h		;2a4a	10 e5		. .
l2a4ch:
	ld (ix+000h),000h	;2a4c	dd 36 00 00	. 6 . .
	call sub_2a81h		;2a50	cd 81 2a	. . *
	pop bc			;2a53	c1		.
	ld (ix+000h),c		;2a54	dd 71 00	. q .
	ld (ix+001h),b		;2a57	dd 70 01	. p .
	ret			;2a5a	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FillWindow: blank every row of the window (FillRowToEnd per
; row, attributes from the descriptor), home the cursor to row 0
; and restart the blink timer ($28E8).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FillWindow:
	call sub_300fh		;2a5b	cd 0f 30	. . 0
sub_2a5eh:
	xor a			;2a5e	af		.
	ld (ix+000h),a		;2a5f	dd 77 00	. w .
	ld (ix+001h),a		;2a62	dd 77 01	. w .
	ld b,(ix+004h)		;2a65	dd 46 04	. F .
l2a68h:
	push bc			;2a68	c5		.
	call FillRowToEnd	;2a69	cd 85 2a	. . *
	pop bc			;2a6c	c1		.
	inc (ix+000h)		;2a6d	dd 34 00	. 4 .
	djnz l2a68h		;2a70	10 f6		. .
	ld (ix+000h),000h	;2a72	dd 36 00 00	. 6 . .
	call RestartCursorBlink	;2a76	cd e8 28	. . (
	ret			;2a79	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FillWholeRow: save the cursor cell, park X at 0 and fall into
; FillRowToEnd - i.e. blank the entire current row.  The scroll
; routines enter at $2A81 (one instruction further) to keep the
; cursor flag untouched.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FillWholeRow:
	call SaveCursorCell	;2a7a	cd b8 28	. . (
	ld (ix+001h),000h	;2a7d	dd 36 01 00	. 6 . .
sub_2a81h:
	res 3,(iy+009h)		;2a81	fd cb 09 9e	. . . .
FillRowToEnd:
	call SaveCursorCell	;2a85	cd b8 28	. . (
	call BitmapAddr		;2a88	cd 7c 29	. | )
	ld a,(ix+005h)		;2a8b	dd 7e 05	. ~ .
	sub (ix+001h)		;2a8e	dd 96 01	. . .
	ld c,008h		;2a91	0e 08		. .
	ld d,a			;2a93	57		W
	ld e,l			;2a94	5d		]
l2a95h:
	ld b,d			;2a95	42		B
	ld l,e			;2a96	6b		k
l2a97h:
	ld (hl),000h		;2a97	36 00		6 .
	inc l			;2a99	2c		,
	djnz l2a97h		;2a9a	10 fb		. .
	inc h			;2a9c	24		$
	dec c			;2a9d	0d		.
	jr nz,l2a95h		;2a9e	20 f5		  .
	push de			;2aa0	d5		.
	call AttrAddr		;2aa1	cd 69 29	. i )
	pop bc			;2aa4	c1		.
	ld d,(ix+006h)		;2aa5	dd 56 06	. V .
l2aa8h:
	ld (hl),d		;2aa8	72		r
	inc hl			;2aa9	23		#
	djnz l2aa8h		;2aaa	10 fc		. .
	ret			;2aac	c9		.
	ld hl,0e01dh		;2aad	21 1d e0	! . .
	set 6,(hl)		;2ab0	cb f6		. .
	set 7,(hl)		;2ab2	cb fe		. .
	ld hl,0e1a6h		;2ab4	21 a6 e1	! . .
	ld (0e3bch),hl		;2ab7	22 bc e3	" . .
	ret			;2aba	c9		.
sub_2abbh:
	ld de,0e1a6h		;2abb	11 a6 e1	. . .
	ld hl,(0e3bch)		;2abe	2a bc e3	* . .
	or a			;2ac1	b7		.
	sbc hl,de		;2ac2	ed 52		. R
	ld b,l			;2ac4	45		E
	ld a,(ix+00bh)		;2ac5	dd 7e 0b	. ~ .
	sub l			;2ac8	95		.
	ret			;2ac9	c9		.
	call sub_2abbh		;2aca	cd bb 2a	. . *
	srl a			;2acd	cb 3f		. ?
l2acfh:
	ld (ix+001h),a		;2acf	dd 77 01	. w .
	push bc			;2ad2	c5		.
	call SaveCursorCell	;2ad3	cd b8 28	. . (
	pop bc			;2ad6	c1		.
	ld hl,0e1a6h		;2ad7	21 a6 e1	! . .
l2adah:
	ld a,(hl)		;2ada	7e		~
	push hl			;2adb	e5		.
	push bc			;2adc	c5		.
	call sub_2800h		;2add	cd 00 28	. . (
	pop bc			;2ae0	c1		.
	pop hl			;2ae1	e1		.
	inc hl			;2ae2	23		#
	djnz l2adah		;2ae3	10 f5		. .
	ld hl,0e01dh		;2ae5	21 1d e0	! . .
	res 6,(hl)		;2ae8	cb b6		. .
	res 7,(hl)		;2aea	cb be		. .
	ret			;2aec	c9		.
	call sub_2abbh		;2aed	cd bb 2a	. . *
	jr l2acfh		;2af0	18 dd		. .
l2af2h:
	bit 5,(hl)		;2af2	cb 6e		. n
	jr nz,l2b57h		;2af4	20 61		  a
	bit 7,(hl)		;2af6	cb 7e		. ~
	jr z,PrinterPutChar	;2af8	28 04		( .
	cp 020h			;2afa	fe 20		.  
	jr c,l2b42h		;2afc	38 44		8 D
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrinterPutChar: buffer a character for the printer/parallel
; port (buffer pointer at $E3BC).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrinterPutChar:
	ld hl,(0e3bch)		;2afe	2a bc e3	* . .
	ld (hl),a		;2b01	77		w
	inc hl			;2b02	23		#
	ld (0e3bch),hl		;2b03	22 bc e3	" . .
	ret			;2b06	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PopAllRet: pop every register and return - the standard exit
; of window handlers.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PopAllRet:
	pop bc			;2b07	c1		.
	pop de			;2b08	d1		.
	pop hl			;2b09	e1		.
	ret			;2b0a	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintCharBit7: print A with bit 7 stripped (terminator-safe).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintCharBit7:
	push af			;2b0b	f5		.
	and 07fh		;2b0c	e6 7f		. .
	jr l2b13h		;2b0e	18 03		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintSpace.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintSpace:
	ld a,020h		;2b10	3e 20		>  
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintChar - the RST 10h backend and the single character
; sink of the monitor; everything funnels into
; DispatchOutput next.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintChar:
	push af			;2b12	f5		.
l2b13h:
	call DispatchOutput	;2b13	cd 18 2b	. . +
	pop af			;2b16	f1		.
	ret			;2b17	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DispatchOutput: the output switchboard - $E01D bits 4/5/6
; route each character to tape ($0950), printer or the screen
; window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DispatchOutput:
	push hl			;2b18	e5		.
	push de			;2b19	d5		.
	push bc			;2b1a	c5		.
	ld hl,PopAllRet		;2b1b	21 07 2b	! . +
	push hl			;2b1e	e5		.
	ld hl,0e01dh		;2b1f	21 1d e0	! . .
	bit 6,(hl)		;2b22	cb 76		. v
	jr nz,l2af2h		;2b24	20 cc		  .
	bit 5,(ix+007h)		;2b26	dd cb 07 6e	. . . n
	ret nz			;2b2a	c0		.
	bit 4,(hl)		;2b2b	cb 66		. f
	jp nz,FeedTapeOutput	;2b2d	c2 50 09	. P .
	cp 020h			;2b30	fe 20		.  
	jr c,l2b42h		;2b32	38 0e		8 .
	call regptrs_end	;2b34	cd fd 27	. . '
	ret nz			;2b37	c0		.
	bit 4,(ix+007h)		;2b38	dd cb 07 66	. . . f
	ret z			;2b3c	c8		.
	call sub_2b75h		;2b3d	cd 75 2b	. u +
	jr l2b84h		;2b40	18 42		. B
l2b42h:
	ld hl,l27e6h		;2b42	21 e6 27	! . '
	ld bc,l000bh		;2b45	01 0b 00	. . .
	ld e,l			;2b48	5d		]
	ld d,h			;2b49	54		T
	cpdr			;2b4a	ed b9		. .
	ret nz			;2b4c	c0		.
	inc de			;2b4d	13		.
	ex de,hl		;2b4e	eb		.
	sla c			;2b4f	cb 21		. !
	add hl,bc		;2b51	09		.
	ld e,(hl)		;2b52	5e		^
	inc hl			;2b53	23		#
	ld d,(hl)		;2b54	56		V
	ex de,hl		;2b55	eb		.
	jp (hl)			;2b56	e9		.
l2b57h:
	bit 7,(hl)		;2b57	cb 7e		. ~
	jr nz,l2b5dh		;2b59	20 02		  .
	res 6,(hl)		;2b5b	cb b6		. .
l2b5dh:
	res 5,(hl)		;2b5d	cb ae		. .
	ld hl,(0e3b9h)		;2b5f	2a b9 e3	* . .
	jp (hl)			;2b62	e9		.
l2b63h:
	ld (0e3b9h),hl		;2b63	22 b9 e3	" . .
	ld hl,0e01dh		;2b66	21 1d e0	! . .
	set 5,(hl)		;2b69	cb ee		. .
	set 6,(hl)		;2b6b	cb f6		. .
	ret			;2b6d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WindowNewline: advance to the next row (scroll at the bottom
; edge).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WindowNewline:
	bit 2,(ix+007h)		;2b6e	dd cb 07 56	. . . V
	call nz,l2b84h		;2b72	c4 84 2b	. . +
sub_2b75h:
	call SaveCursorCell	;2b75	cd b8 28	. . (
	xor a			;2b78	af		.
	ld (ix+001h),a		;2b79	dd 77 01	. w .
	ret			;2b7c	c9		.
	bit 3,(ix+007h)		;2b7d	dd cb 07 5e	. . . ^
	call nz,sub_2b75h	;2b81	c4 75 2b	. u +
l2b84h:
	call sub_2b8eh		;2b84	cd 8e 2b	. . +
	ret nz			;2b87	c0		.
	call SaveCursorCell	;2b88	cd b8 28	. . (
	jp ScrollUp		;2b8b	c3 8e 29	. . )
sub_2b8eh:
	ld a,(ix+004h)		;2b8e	dd 7e 04	. ~ .
	dec a			;2b91	3d		=
	cp (ix+000h)		;2b92	dd be 00	. . .
	ret z			;2b95	c8		.
	call SaveCursorCell	;2b96	cd b8 28	. . (
	inc (ix+000h)		;2b99	dd 34 00	. 4 .
	ret			;2b9c	c9		.
sub_2b9dh:
	call SaveCursorCell	;2b9d	cd b8 28	. . (
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; WindowHome: cursor to row/column 0 of the current window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
WindowHome:
	xor a			;2ba0	af		.
	ld (ix+001h),a		;2ba1	dd 77 01	. w .
	ld (ix+000h),a		;2ba4	dd 77 00	. w .
	ret			;2ba7	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; KeyClick: brief speaker toggle (OUT $FE, bit 4 XOR) per
; keypress.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
KeyClick:
	push de			;2ba8	d5		.
	ld de,07060h		;2ba9	11 60 70	. ` p
	push bc			;2bac	c5		.
	push af			;2bad	f5		.
	ld a,(0e053h)		;2bae	3a 53 e0	: S .
	push af			;2bb1	f5		.
l2bb2h:
	out (0feh),a		;2bb2	d3 fe		. .
	xor 010h		;2bb4	ee 10		. .
	push af			;2bb6	f5		.
	ld a,r			;2bb7	ed 5f		. _
	and 001h		;2bb9	e6 01		. .
	add a,e			;2bbb	83		.
	ld b,a			;2bbc	47		G
l2bbdh:
	djnz l2bbdh		;2bbd	10 fe		. .
	pop af			;2bbf	f1		.
	dec d			;2bc0	15		.
	jr nz,l2bb2h		;2bc1	20 ef		  .
	pop af			;2bc3	f1		.
	out (0feh),a		;2bc4	d3 fe		. .
	pop af			;2bc6	f1		.
	pop bc			;2bc7	c1		.
	pop de			;2bc8	d1		.
	ret			;2bc9	c9		.
	ld hl,l2bd0h		;2bca	21 d0 2b	! . +
	jp l2b63h		;2bcd	c3 63 2b	. c +
l2bd0h:
	ld c,(ix+000h)		;2bd0	dd 4e 00	. N .
	cp (ix+004h)		;2bd3	dd be 04	. . .
	jr nc,l2bd9h		;2bd6	30 01		0 .
	ld c,a			;2bd8	4f		O
l2bd9h:
	ld a,c			;2bd9	79		y
	ld (0e3bbh),a		;2bda	32 bb e3	2 . .
	ld hl,l2be3h		;2bdd	21 e3 2b	! . +
	jp l2b63h		;2be0	c3 63 2b	. c +
l2be3h:
	call SaveCursorCell	;2be3	cd b8 28	. . (
	ld c,(ix+00bh)		;2be6	dd 4e 0b	. N .
	dec c			;2be9	0d		.
	cp c			;2bea	b9		.
	jr nc,l2beeh		;2beb	30 01		0 .
	ld c,a			;2bed	4f		O
l2beeh:
	ld (ix+001h),c		;2bee	dd 71 01	. q .
	ld a,(0e3bbh)		;2bf1	3a bb e3	: . .
	ld (ix+000h),a		;2bf4	dd 77 00	. w .
	ret			;2bf7	c9		.
	ld a,(ix+006h)		;2bf8	dd 7e 06	. ~ .
	ld (0e3b4h),a		;2bfb	32 b4 e3	2 . .
	ld hl,l2c04h		;2bfe	21 04 2c	! . ,
	jp l2b63h		;2c01	c3 63 2b	. c +
l2c04h:
	or (ix+006h)		;2c04	dd b6 06	. . .
	ld (ix+006h),a		;2c07	dd 77 06	. w .
	ld hl,l2c10h		;2c0a	21 10 2c	! . ,
	jp l2b63h		;2c0d	c3 63 2b	. c +
l2c10h:
	cpl			;2c10	2f		/
	and (ix+006h)		;2c11	dd a6 06	. . .
	ld (ix+006h),a		;2c14	dd 77 06	. w .
	ret			;2c17	c9		.
	ld a,(0e3b4h)		;2c18	3a b4 e3	: . .
	ld (ix+006h),a		;2c1b	dd 77 06	. w .
	ret			;2c1e	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ShortDelay: calibrated A/E delay loops (tape and AY timing).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ShortDelay:
	xor a			;2c1f	af		.
	ld h,a			;2c20	67		g
	ld e,00eh		;2c21	1e 0e		. .
	inc a			;2c23	3c		<
l2c24h:
	or a			;2c24	b7		.
	jr z,l2c2eh		;2c25	28 07		( .
	dec h			;2c27	25		%
	jr nz,l2c24h		;2c28	20 fa		  .
	dec e			;2c2a	1d		.
	jr nz,l2c24h		;2c2b	20 f7		  .
	inc e			;2c2d	1c		.
l2c2eh:
	di			;2c2e	f3		.
	ret			;2c2f	c9		.
sub_2c30h:
	bit 7,(iy+019h)		;2c30	fd cb 19 7e	. . . ~
	ret z			;2c34	c8		.
	call sub_3b75h		;2c35	cd 75 3b	. u ;
	push af			;2c38	f5		.
	im 1			;2c39	ed 56		. V
	ei			;2c3b	fb		.
	call ShortDelay		;2c3c	cd 1f 2c	. . ,
	ld hl,0e02dh		;2c3f	21 2d e0	! - .
	res 6,(hl)		;2c42	cb b6		. .
	jr z,l2c48h		;2c44	28 02		( .
	set 6,(hl)		;2c46	cb f6		. .
l2c48h:
	pop af			;2c48	f1		.
	ret nz			;2c49	c0		.
	im 2			;2c4a	ed 5e		. ^
	ret			;2c4c	c9		.
sub_2c4dh:
	and 003h		;2c4d	e6 03		. .
	ld (0ffcah),a		;2c4f	32 ca ff	2 . .
	push hl			;2c52	e5		.
	ld l,a			;2c53	6f		o
	rlca			;2c54	07		.
	rlca			;2c55	07		.
	ld h,a			;2c56	67		g
	rlca			;2c57	07		.
	rlca			;2c58	07		.
	add a,h			;2c59	84		.
	add a,l			;2c5a	85		.
	ld hl,(0e9f3h)		;2c5b	2a f3 e9	* . .
	add a,l			;2c5e	85		.
	ld l,a			;2c5f	6f		o
	jr nc,l2c63h		;2c60	30 01		0 .
	inc h			;2c62	24		$
l2c63h:
	ld a,(hl)		;2c63	7e		~
	or a			;2c64	b7		.
	jr z,l2c75h		;2c65	28 0e		( .
	push de			;2c67	d5		.
	ld de,0e590h		;2c68	11 90 e5	. . .
	push bc			;2c6b	c5		.
	ld bc,l0015h		;2c6c	01 15 00	. . .
	ldir			;2c6f	ed b0		. .
	pop bc			;2c71	c1		.
	pop de			;2c72	d1		.
	pop hl			;2c73	e1		.
	ret			;2c74	c9		.
l2c75h:
	ld (0e590h),a		;2c75	32 90 e5	2 . .
	pop hl			;2c78	e1		.
	ret			;2c79	c9		.
	ld a,(l00fbh)		;2c7a	3a fb 00	: . .
	ret			;2c7d	c9		.
sub_2c7eh:
	pop hl			;2c7e	e1		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScanTokens: walks the v4.01 tokenizer linked list at TokenTable
; ($23A3): each record matches one character of the input, the
; next-pointer is followed on a partial match, and the record
; handler runs (terminator action via jp (HL)) once the name is
; consumed.  The command tokenizer of the monitor.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScanTokens:
	jr l2c83h		;2c7f	18 02		. .
l2c81h:
	inc hl			;2c81	23		#
	inc hl			;2c82	23		#
l2c83h:
	ld e,(hl)		;2c83	5e		^
	inc e			;2c84	1c		.
	jr z,l2c90h		;2c85	28 09		( .
	cp (hl)			;2c87	be		.
	inc hl			;2c88	23		#
	jr nz,l2c81h		;2c89	20 f6		  .
	ld e,(hl)		;2c8b	5e		^
	inc hl			;2c8c	23		#
	ld d,(hl)		;2c8d	56		V
	dec de			;2c8e	1b		.
	ex de,hl		;2c8f	eb		.
l2c90h:
	inc hl			;2c90	23		#
	jp (hl)			;2c91	e9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CommandLoop - the heart of the monitor: print the prompt in
; the $E075 window, read a line through the editor, parse and
; execute it via the command table (RunCommand $385D), repeat.
; Errors funnel back here through ExitToError.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CommandLoop:
	call BuildStepStub	;2c92	cd f0 17	. . .
	call sub_328bh		;2c95	cd 8b 32	. . 2
	ld e,0ffh		;2c98	1e ff		. .
	jp nc,l019dh		;2c9a	d2 9d 01	. . .
l2c9dh:
	ld a,(0e02ah)		;2c9d	3a 2a e0	: * .
	and 07fh		;2ca0	e6 7f		. .
	bit 2,a			;2ca2	cb 57		. W
	ret nz			;2ca4	c0		.
	ld (0e01eh),a		;2ca5	32 1e e0	2 . .
sub_2ca8h:
	call sub_0550h		;2ca8	cd 50 05	. P .
	call sub_3000h		;2cab	cd 00 30	. . 0
	bit 0,(iy+00ah)		;2cae	fd cb 0a 46	. . . F
	jr nz,$+55		;2cb2	20 35		  5
	call sub_2a5eh		;2cb4	cd 5e 2a	. ^ *
	rst 20h			;2cb7	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RegHeaderLine: header row of the register window
; ('IR  SZ-H-PNC  INT RAM ROM SCR  ZX' with
; embedded control bytes for the column gaps);
; printed above the register dump.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'RegHeaderLine' (start 0x2cb8 end 0x2cdc)
RegHeaderLine_start:
	defb 049h		;2cb8	49		I
	defb 052h		;2cb9	52		R
	defb 01bh		;2cba	1b		.
	defb 008h		;2cbb	08		.
	defb 00ah		;2cbc	0a		.
	defb 053h		;2cbd	53		S
	defb 05ah		;2cbe	5a		Z
	defb 02dh		;2cbf	2d		-
	defb 048h		;2cc0	48		H
	defb 02dh		;2cc1	2d		-
	defb 050h		;2cc2	50		P
	defb 04eh		;2cc3	4e		N
	defb 043h		;2cc4	43		C
	defb 00dh		;2cc5	0d		.
	defb 00dh		;2cc6	0d		.
	defb 049h		;2cc7	49		I
	defb 04eh		;2cc8	4e		N
	defb 054h		;2cc9	54		T
	defb 020h		;2cca	20		 
	defb 052h		;2ccb	52		R
	defb 041h		;2ccc	41		A
	defb 04dh		;2ccd	4d		M
	defb 020h		;2cce	20		 
	defb 052h		;2ccf	52		R
	defb 04fh		;2cd0	4f		O
	defb 04dh		;2cd1	4d		M
	defb 020h		;2cd2	20		 
	defb 053h		;2cd3	53		S
	defb 043h		;2cd4	43		C
	defb 052h		;2cd5	52		R
	defb 01bh		;2cd6	1b		.
l2cd7h:
	defb 000h		;2cd7	00		.
	defb 00bh		;2cd8	0b		.
	defb 05ah		;2cd9	5a		Z
	defb 058h		;2cda	58		X
	defb 0a0h		;2cdb	a0		.
RegHeaderLine_end:
	bit 5,(iy-002h)		;2cdc	fd cb fe 6e	. . . n
	jr z,l2ce6h		;2ce0	28 04		( .
	rst 20h			;2ce2	e7		.
	or h			;2ce3	b4		.
	jr $+5			;2ce4	18 03		. .
l2ce6h:
	rst 20h			;2ce6	e7		.
	ld sp,0e7b2h		;2ce7	31 b2 e7	1 . .
	jr c,l2cd7h		;2cea	38 eb		8 .
	ld (ix+000h),000h	;2cec	dd 36 00 00	. 6 . .
	ld (ix+001h),004h	;2cf0	dd 36 01 04	. 6 . .
	ld hl,(0dd84h)		;2cf4	2a 84 dd	* . .
	call PrintHexWord	;2cf7	cd de 16	. . .
	inc (ix+000h)		;2cfa	dd 34 00	. 4 .
	ld (ix+001h),000h	;2cfd	dd 36 01 00	. 6 . .
	call ShowRegisters	;2d01	cd 70 2d	. p -
	ld (ix+000h),009h	;2d04	dd 36 00 09	. 6 . .
	call sub_2df6h		;2d08	cd f6 2d	. . -
	ld hl,0e01eh		;2d0b	21 1e e0	! . .
	set 1,(hl)		;2d0e	cb ce		. .
	set 2,(hl)		;2d10	cb d6		. .
	ld (ix+000h),00bh	;2d12	dd 36 00 0b	. 6 . .
	ld hl,l0fa0h		;2d16	21 a0 0f	! . .
	ld a,(UserIff)		;2d19	3a 83 dd	: . .
	and 004h		;2d1c	e6 04		. .
	jr nz,l2d23h		;2d1e	20 03		  .
	ld hl,l0fa2h		;2d20	21 a2 0f	! . .
l2d23h:
	call PrintBit7Chars	;2d23	cd c7 15	. . .
	call PrintSpace		;2d26	cd 10 2b	. . +
	ld (ix+001h),005h	;2d29	dd 36 01 05	. 6 . .
	ld a,(PagingState)	;2d2d	3a 12 e0	: . .
	and 007h		;2d30	e6 07		. .
	ld c,a			;2d32	4f		O
	ld a,(0e013h)		;2d33	3a 13 e0	: . .
	rrca			;2d36	0f		.
	and 008h		;2d37	e6 08		. .
	or c			;2d39	b1		.
	call sub_16ech		;2d3a	cd ec 16	. . .
	ld (ix+001h),009h	;2d3d	dd 36 01 09	. 6 . .
	ld a,(PagingState)	;2d41	3a 12 e0	: . .
	rrca			;2d44	0f		.
	rrca			;2d45	0f		.
	rrca			;2d46	0f		.
	rrca			;2d47	0f		.
	and 001h		;2d48	e6 01		. .
	call sub_16ech		;2d4a	cd ec 16	. . .
	ld (ix+001h),00dh	;2d4d	dd 36 01 0d	. 6 . .
	ld c,035h		;2d51	0e 35		. 5
	ld a,(PagingState)	;2d53	3a 12 e0	: . .
	and 008h		;2d56	e6 08		. .
	jr z,l2d5ch		;2d58	28 02		( .
	ld c,037h		;2d5a	0e 37		. 7
l2d5ch:
	ld a,c			;2d5c	79		y
	rst 10h			;2d5d	d7		.
	ld (iy+006h),004h	;2d5e	fd 36 06 04	. 6 . .
	call sub_2e38h		;2d62	cd 38 2e	. 8 .
	call sub_2f30h		;2d65	cd 30 2f	. 0 /
	ld hl,0e01eh		;2d68	21 1e e0	! . .
	ld a,(hl)		;2d6b	7e		~
	or 051h			;2d6c	f6 51		. Q
	ld (hl),a		;2d6e	77		w
	ret			;2d6f	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ShowRegisters: dump the saved context - register names are
; two bit-7-terminated lists at RegisterNameList ($0F91), values
; printed in the current number base; the flag byte comes out
; as eight binary digits.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ShowRegisters:
	bit 1,(iy+00ah)		;2d70	fd cb 0a 4e	. . . N
	ret nz			;2d74	c0		.
	ld hl,RegisterNameList	;2d75	21 91 0f	! . .
	ld c,(iy+00ch)		;2d78	fd 4e 0c	. N .
	ld b,007h		;2d7b	06 07		. .
	ld de,UserPc		;2d7d	11 6b dd	. k .
	jr l2d85h		;2d80	18 03		. .
l2d82h:
	inc (ix+000h)		;2d82	dd 34 00	. 4 .
l2d85h:
	ld a,003h		;2d85	3e 03		> .
	cp b			;2d87	b8		.
	jr nz,l2d93h		;2d88	20 09		  .
	bit 0,(iy+00bh)		;2d8a	fd cb 0b 46	. . . F
	jr z,l2d93h		;2d8e	28 03		( .
	ld de,0dd79h		;2d90	11 79 dd	. y .
l2d93h:
	sla c			;2d93	cb 21		. !
	jr nc,l2d9bh		;2d95	30 04		0 .
	inc hl			;2d97	23		#
	inc hl			;2d98	23		#
	jr l2ddfh		;2d99	18 44		. D
l2d9bh:
	bit 2,(iy+00ah)		;2d9b	fd cb 0a 56	. . . V
	jr nz,l2daeh		;2d9f	20 0d		  .
	call PrintBit7Chars	;2da1	cd c7 15	. . .
	ld a,003h		;2da4	3e 03		> .
	cp b			;2da6	b8		.
	jr c,l2db0h		;2da7	38 07		8 .
	call sub_2de8h		;2da9	cd e8 2d	. . -
	jr l2db0h		;2dac	18 02		. .
l2daeh:
	inc hl			;2dae	23		#
	inc hl			;2daf	23		#
l2db0h:
	ld (ix+001h),004h	;2db0	dd 36 01 04	. 6 . .
	push hl			;2db4	e5		.
	push de			;2db5	d5		.
	ex de,hl		;2db6	eb		.
	ld a,(hl)		;2db7	7e		~
	inc hl			;2db8	23		#
	ld h,(hl)		;2db9	66		f
	ld l,a			;2dba	6f		o
	call PrintHexWord	;2dbb	cd de 16	. . .
	bit 3,(iy+00ah)		;2dbe	fd cb 0a 5e	. . . ^
	jr nz,l2dddh		;2dc2	20 19		  .
	push bc			;2dc4	c5		.
	ld bc,l0003h+1		;2dc5	01 04 00	. . .
	ld de,0ddach		;2dc8	11 ac dd	. . .
	push de			;2dcb	d5		.
	call CopyAcrossBanks	;2dcc	cd 53 07	. S .
	pop hl			;2dcf	e1		.
	ld b,004h		;2dd0	06 04		. .
l2dd2h:
	inc (ix+001h)		;2dd2	dd 34 01	. 4 .
	ld a,(hl)		;2dd5	7e		~
	call PrintHexByte	;2dd6	cd e3 16	. . .
	inc hl			;2dd9	23		#
	djnz l2dd2h		;2dda	10 f6		. .
	pop bc			;2ddc	c1		.
l2dddh:
	pop de			;2ddd	d1		.
	pop hl			;2dde	e1		.
l2ddfh:
	inc de			;2ddf	13		.
	inc de			;2de0	13		.
	ld (ix+001h),000h	;2de1	dd 36 01 00	. 6 . .
	djnz l2d82h		;2de5	10 9b		. .
	ret			;2de7	c9		.
sub_2de8h:
	bit 0,(iy+00bh)		;2de8	fd cb 0b 46	. . . F
sub_2dech:
	ld a,020h		;2dec	3e 20		>  
	jp z,PrintChar		;2dee	ca 12 2b	. . +
	ld a,027h		;2df1	3e 27		> '
	jp PrintChar		;2df3	c3 12 2b	. . +
sub_2df6h:
	ld b,(iy+00ah)		;2df6	fd 46 0a	. F .
	bit 1,b			;2df9	cb 48		. H
	ret nz			;2dfb	c0		.
	bit 0,(iy+00ch)		;2dfc	fd cb 0c 46	. . . F
	ret nz			;2e00	c0		.
	bit 2,b			;2e01	cb 50		. P
	ld b,(iy+00bh)		;2e03	fd 46 0b	. F .
	jr nz,l2e10h		;2e06	20 08		  .
	rst 20h			;2e08	e7		.
	ld b,c			;2e09	41		A
	add a,0cbh		;2e0a	c6 cb		. .
	ld c,b			;2e0c	48		H
	call sub_2dech		;2e0d	cd ec 2d	. . -
l2e10h:
	ld (ix+001h),004h	;2e10	dd 36 01 04	. 6 . .
	bit 1,b			;2e14	cb 48		. H
	ld hl,(0dd7fh)		;2e16	2a 7f dd	* . .
	jr z,l2e1eh		;2e19	28 03		( .
	ld hl,(0dd81h)		;2e1b	2a 81 dd	* . .
l2e1eh:
	call PrintHexWord	;2e1e	cd de 16	. . .
	ld (ix+001h),00ah	;2e21	dd 36 01 0a	. 6 . .
sub_2e25h:
	ld b,008h		;2e25	06 08		. .
l2e27h:
	ld a,030h		;2e27	3e 30		> 0
	sla l			;2e29	cb 25		. %
	jr nc,l2e2eh		;2e2b	30 01		0 .
	inc a			;2e2d	3c		<
l2e2eh:
	call PrintChar		;2e2e	cd 12 2b	. . +
	djnz l2e27h		;2e31	10 f4		. .
	ld (ix+001h),000h	;2e33	dd 36 01 00	. 6 . .
	ret			;2e37	c9		.
sub_2e38h:
	bit 4,(iy+00ah)		;2e38	fd cb 0a 66	. . . f
	ret nz			;2e3c	c0		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MemoryDumpWindow - the D-command view: default work buffer,
; scroll check, prompt-window cursor placement, then eight rows
; of HexDumpRow; the ASCII half re-fetches every byte across
; banks.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
MemoryDumpWindow:
	call SetDefaultWorkspace	;2e3d	cd 19 30	. . 0
	call sub_2e9dh		;2e40	cd 9d 2e	. . .
	bit 5,(iy+00ah)		;2e43	fd cb 0a 6e	. . . n
	call z,sub_2f74h	;2e47	cc 74 2f	. t /
	call SetDefaultWorkspace	;2e4a	cd 19 30	. . 0
	call sub_2b9dh		;2e4d	cd 9d 2b	. . +
	ld hl,(0dd87h)		;2e50	2a 87 dd	* . .
	ld b,008h		;2e53	06 08		. .
l2e55h:
	push bc			;2e55	c5		.
	call HexDumpRow		;2e56	cd ba 2e	. . .
	inc (ix+000h)		;2e59	dd 34 00	. 4 .
	pop bc			;2e5c	c1		.
	djnz l2e55h		;2e5d	10 f6		. .
	ld bc,l3e3ch		;2e5f	01 3c 3e	. < >
	ld hl,(0dd69h)		;2e62	2a 69 dd	* i .
l2e65h:
	call PositionCursor	;2e65	cd 7e 2e	. ~ .
	ld a,b			;2e68	78		x
	call PrintChar		;2e69	cd 12 2b	. . +
	inc (ix+001h)		;2e6c	dd 34 01	. 4 .
	inc (ix+001h)		;2e6f	dd 34 01	. 4 .
	ld a,c			;2e72	79		y
	jp PrintChar		;2e73	c3 12 2b	. . +
sub_2e76h:
	ld bc,02020h		;2e76	01 20 20	.    
	ld hl,(0dd89h)		;2e79	2a 89 dd	* . .
	jr l2e65h		;2e7c	18 e7		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PositionCursor: translate the dump address (DD87/DD89) into
; window cursor coordinates (IX+0/IX+1).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PositionCursor:
	ld (0dd89h),hl		;2e7e	22 89 dd	" . .
	ld de,(0dd87h)		;2e81	ed 5b 87 dd	. [ . .
	or a			;2e85	b7		.
	sbc hl,de		;2e86	ed 52		. R
	ld a,l			;2e88	7d		}
	and 007h		;2e89	e6 07		. .
	ld h,a			;2e8b	67		g
	add a,a			;2e8c	87		.
	add a,h			;2e8d	84		.
	add a,007h		;2e8e	c6 07		. .
	ld (ix+001h),a		;2e90	dd 77 01	. w .
	ld a,l			;2e93	7d		}
	and 038h		;2e94	e6 38		. 8
	rra			;2e96	1f		.
	rra			;2e97	1f		.
	rra			;2e98	1f		.
	ld (ix+000h),a		;2e99	dd 77 00	. w .
	ret			;2e9c	c9		.
sub_2e9dh:
	call sub_2e76h		;2e9d	cd 76 2e	. v .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScrollWindow: scroll the current window when the cursor is
; about to leave the bottom edge.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScrollWindow:
	ld hl,(0dd87h)		;2ea0	2a 87 dd	* . .
	ld de,(0dd69h)		;2ea3	ed 5b 69 dd	. [ i .
	ld bc,0003fh		;2ea7	01 3f 00	. ? .
	or a			;2eaa	b7		.
	sbc hl,de		;2eab	ed 52		. R
	ret z			;2ead	c8		.
	jr nc,l2eb2h		;2eae	30 02		0 .
	add hl,bc		;2eb0	09		.
	ret c			;2eb1	d8		.
l2eb2h:
	ld hl,0ffe8h		;2eb2	21 e8 ff	! . .
	add hl,de		;2eb5	19		.
	ld (0dd87h),hl		;2eb6	22 87 dd	" . .
	ret			;2eb9	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; HexDumpRow: one dump row - address word, 8 bytes in hex
; (fetched through the bank window), then the same 8 as ASCII.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
HexDumpRow:
	ld (ix+001h),002h	;2eba	dd 36 01 02	. 6 . .
l2ebeh:
	call PrintHexWord	;2ebe	cd de 16	. . .
	call sub_2f22h		;2ec1	cd 22 2f	. " /
	call sub_2f22h		;2ec4	cd 22 2f	. " /
	ld de,0ddach		;2ec7	11 ac dd	. . .
	ld bc,Rst08Vector	;2eca	01 08 00	. . .
	push de			;2ecd	d5		.
	call CopyAcrossBanks	;2ece	cd 53 07	. S .
	pop de			;2ed1	d1		.
	push de			;2ed2	d5		.
	ld b,008h		;2ed3	06 08		. .
	call sub_2ee6h		;2ed5	cd e6 2e	. . .
	pop de			;2ed8	d1		.
	call sub_2f22h		;2ed9	cd 22 2f	. " /
	ld b,008h		;2edc	06 08		. .
	call sub_2ef1h		;2ede	cd f1 2e	. . .
	ld (ix+001h),000h	;2ee1	dd 36 01 00	. 6 . .
	ret			;2ee5	c9		.
sub_2ee6h:
	ld a,(de)		;2ee6	1a		.
	inc de			;2ee7	13		.
	call PrintHexByte	;2ee8	cd e3 16	. . .
	call sub_2f22h		;2eeb	cd 22 2f	. " /
	djnz sub_2ee6h		;2eee	10 f6		. .
	ret			;2ef0	c9		.
sub_2ef1h:
	ld c,(iy+005h)		;2ef1	fd 4e 05	. N .
l2ef4h:
	ld a,(de)		;2ef4	1a		.
	inc de			;2ef5	13		.
	bit 7,c			;2ef6	cb 79		. y
	jr nz,l2f02h		;2ef8	20 08		  .
	and 07fh		;2efa	e6 7f		. .
	cp 020h			;2efc	fe 20		.  
	jr nc,l2f02h		;2efe	30 02		0 .
	ld a,02eh		;2f00	3e 2e		> .
l2f02h:
	bit 4,(iy+009h)		;2f02	fd cb 09 66	. . . f
	jr nz,l2f14h		;2f06	20 0c		  .
	push bc			;2f08	c5		.
	push de			;2f09	d5		.
	push hl			;2f0a	e5		.
	call regptrs_end	;2f0b	cd fd 27	. . '
	pop hl			;2f0e	e1		.
	pop de			;2f0f	d1		.
	pop bc			;2f10	c1		.
	djnz l2ef4h		;2f11	10 e1		. .
	ret			;2f13	c9		.
l2f14h:
	and 07fh		;2f14	e6 7f		. .
	cp 020h			;2f16	fe 20		.  
	jr nc,l2f1ch		;2f18	30 02		0 .
	ld a,02eh		;2f1a	3e 2e		> .
l2f1ch:
	call PrintChar		;2f1c	cd 12 2b	. . +
	djnz l2ef4h		;2f1f	10 d3		. .
	ret			;2f21	c9		.
sub_2f22h:
	inc (ix+001h)		;2f22	dd 34 01	. 4 .
	bit 4,(iy+009h)		;2f25	fd cb 09 66	. . . f
	ret z			;2f29	c8		.
	dec (ix+001h)		;2f2a	dd 35 01	. 5 .
	jp PrintSpace		;2f2d	c3 10 2b	. . +
sub_2f30h:
	ld b,(iy+00ah)		;2f30	fd 46 0a	. F .
	bit 6,b			;2f33	cb 70		. p
	ret nz			;2f35	c0		.
	bit 0,b			;2f36	cb 40		. @
	jr nz,l2f47h		;2f38	20 0d		  .
	ld hl,05830h		;2f3a	21 30 58	! 0 X
	ld (hl),030h		;2f3d	36 30		6 0
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DisasmWindow: the disassembly view.  Attributes through the
; $5831 copy, workspace $E091; renders 5 lines (1 while
; stepping) from DD87/DD89, advancing by each decoded length.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DisasmWindow:
	ld bc,l000dh+2		;2f3f	01 0f 00	. . .
	ld de,05831h		;2f42	11 31 58	. 1 X
	ldir			;2f45	ed b0		. .
l2f47h:
	ld hl,DisasmWindowDef	;2f47	21 91 e0	! . .
	call SetWorkspace	;2f4a	cd 1c 30	. . 0
	bit 0,(iy+00ah)		;2f4d	fd cb 0a 46	. . . F
	call nz,ScrollUp	;2f51	c4 8e 29	. . )
	ld (ix+000h),001h	;2f54	dd 36 00 01	. 6 . .
	ld (ix+001h),000h	;2f58	dd 36 01 00	. 6 . .
	ld hl,(UserPc)		;2f5c	2a 6b dd	* k .
	ld b,005h		;2f5f	06 05		. .
	bit 2,(iy+005h)		;2f61	fd cb 05 56	. . . V
	jr z,l2f69h		;2f65	28 02		( .
	ld b,001h		;2f67	06 01		. .
l2f69h:
	push bc			;2f69	c5		.
	call DisasmLine		;2f6a	cd 80 2f	. . /
	inc (ix+000h)		;2f6d	dd 34 00	. 4 .
	pop bc			;2f70	c1		.
	djnz l2f69h		;2f71	10 f6		. .
	ret			;2f73	c9		.
sub_2f74h:
	call sub_3000h		;2f74	cd 00 30	. . 0
	ld (ix+000h),00bh	;2f77	dd 36 00 0b	. 6 . .
	ld (ix+001h),015h	;2f7b	dd 36 01 15	. 6 . .
	ex de,hl		;2f7f	eb		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DisasmLine: disassemble one instruction and print it
; (address, opcode bytes, mnemonic via Disassemble).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DisasmLine:
	call PrintHexWord	;2f80	cd de 16	. . .
	call FetchPrefixBytes	;2f83	cd 96 2f	. . /
	push hl			;2f86	e5		.
	ld hl,0ddb7h		;2f87	21 b7 dd	! . .
	call Disassemble	;2f8a	cd 12 15	. . .
	call PadColumns		;2f8d	cd ed 2f	. . /
	pop hl			;2f90	e1		.
	ld (ix+001h),000h	;2f91	dd 36 01 00	. 6 . .
	ret			;2f95	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FetchPrefixBytes: pull up to 4 opcode bytes for the
; disassembler across the bank window (DD/DDCB/FD/FDCB
; prefixes included).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FetchPrefixBytes:
	ld (0ddaah),hl		;2f96	22 aa dd	" . .
	push hl			;2f99	e5		.
	ld de,0ddach		;2f9a	11 ac dd	. . .
	ld bc,l0003h+1		;2f9d	01 04 00	. . .
	call CopyAcrossBanks	;2fa0	cd 53 07	. S .
	call sub_197fh		;2fa3	cd 7f 19	. . .
	pop hl			;2fa6	e1		.
	add hl,bc		;2fa7	09		.
	ld (0ddaah),hl		;2fa8	22 aa dd	" . .
	ret			;2fab	c9		.
sub_2fach:
	bit 0,(iy+005h)		;2fac	fd cb 05 46	. . . F
	push af			;2fb0	f5		.
	call z,PrintHexWord	;2fb1	cc de 16	. . .
	pop af			;2fb4	f1		.
	ld c,l			;2fb5	4d		M
	ld b,h			;2fb6	44		D
	ld (iy+008h),020h	;2fb7	fd 36 08 20	. 6 .  
	call nz,sub_1669h	;2fbb	c4 69 16	. i .
	call FetchPrefixBytes	;2fbe	cd 96 2f	. . /
	push hl			;2fc1	e5		.
	push bc			;2fc2	c5		.
	ld hl,0ddb7h		;2fc3	21 b7 dd	! . .
	call Disassemble	;2fc6	cd 12 15	. . .
	pop bc			;2fc9	c1		.
	bit 1,(iy+005h)		;2fca	fd cb 05 4e	. . . N
	jr z,l2febh		;2fce	28 1b		( .
	ld a,017h		;2fd0	3e 17		> .
	call sub_2ff1h		;2fd2	cd f1 2f	. . /
	ld a,03bh		;2fd5	3e 3b		> ;
	rst 10h			;2fd7	d7		.
	ld b,c			;2fd8	41		A
	push bc			;2fd9	c5		.
	ld de,0ddach		;2fda	11 ac dd	. . .
	push de			;2fdd	d5		.
	call sub_2ee6h		;2fde	cd e6 2e	. . .
	ld a,025h		;2fe1	3e 25		> %
	call sub_2ff1h		;2fe3	cd f1 2f	. . /
	pop de			;2fe6	d1		.
	pop bc			;2fe7	c1		.
	call sub_2ef1h		;2fe8	cd f1 2e	. . .
l2febh:
	pop hl			;2feb	e1		.
	ret			;2fec	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PadColumns: pad the listing to its fixed column layout.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PadColumns:
	ld a,(ix+00bh)		;2fed	dd 7e 0b	. ~ .
	dec a			;2ff0	3d		=
sub_2ff1h:
	ld b,(ix+001h)		;2ff1	dd 46 01	. F .
	sub b			;2ff4	90		.
	ret z			;2ff5	c8		.
	ret c			;2ff6	d8		.
	ld b,a			;2ff7	47		G
	jp l15f3h		;2ff8	c3 f3 15	. . .
	ld hl,0e0e5h		;2ffb	21 e5 e0	! . .
	jr SetWorkspace		;2ffe	18 1c		. .
sub_3000h:
	ld hl,PromptWindowDef	;3000	21 75 e0	! u .
	jr SetWorkspace		;3003	18 17		. .
	ld hl,RegWindowDef	;3005	21 c9 e0	! . .
	jr SetWorkspace		;3008	18 12		. .
sub_300ah:
	ld hl,0e067h		;300a	21 67 e0	! g .
	jr SetWorkspace		;300d	18 0d		. .
sub_300fh:
	ld hl,(0dd6fh)		;300f	2a 6f dd	* o .
	jr SetWorkspace		;3012	18 08		. .
sub_3014h:
	ld hl,0e083h		;3014	21 83 e0	! . .
	jr SetWorkspace		;3017	18 03		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SetDefaultWorkspace: HL <- $E09F (the v4.01 WorkBuffer), then
; falls into SetWorkspace below.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SetDefaultWorkspace:
	ld hl,WorkBuffer	;3019	21 9f e0	! . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SetWorkspace: (WorkBufferPtr, $E3B7) <- HL and IX <- HL.  Window
; routines address their descriptors IX-relative, so this is how
; the monitor switches windows.  In v4.01 this is a plain routine
; - RST 30h no longer enters here (see the RAM hook at $0030).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SetWorkspace:
	ld (WorkBufferPtr),hl	;301c	22 b7 e3	" . .
	push hl			;301f	e5		.
	pop ix			;3020	dd e1		. .
	ret			;3022	c9		.
sub_3023h:
	push hl			;3023	e5		.
	push de			;3024	d5		.
	push bc			;3025	c5		.
	rst 30h			;3026	f7		.
	ld c,d			;3027	4a		J
	rlca			;3028	07		.
	ld b,0c1h		;3029	06 c1		. .
	pop de			;302b	d1		.
	pop hl			;302c	e1		.
	ret			;302d	c9		.
sub_302eh:
	ld e,(ix+007h)		;302e	dd 5e 07	. ^ .
	set 0,(ix+007h)		;3031	dd cb 07 c6	. . . .
	call sub_3023h		;3035	cd 23 30	. # 0
	ld (ix+007h),e		;3038	dd 73 07	. s .
	ret			;303b	c9		.
	xor a			;303c	af		.
	jr l3041h		;303d	18 02		. .
	ld a,080h		;303f	3e 80		> .
l3041h:
	bit 1,(iy+012h)		;3041	fd cb 12 4e	. . . N
	jr z,l3049h		;3045	28 02		( .
	or 001h			;3047	f6 01		. .
l3049h:
	ld (0e02ah),a		;3049	32 2a e0	2 * .
	call CommandLoop	;304c	cd 92 2c	. . ,
	ld a,081h		;304f	3e 81		> .
	ret			;3051	c9		.
l3052h:
	di			;3052	f3		.
	call sub_305bh		;3053	cd 5b 30	. [ 0
	ld hl,l0040h		;3056	21 40 00	! @ .
	push hl			;3059	e5		.
	ret			;305a	c9		.
sub_305bh:
	ld hl,l3052h		;305b	21 52 30	! R 0
	ld (BootCounter),hl	;305e	22 64 c0	" d .
	pop de			;3061	d1		.
	ld sp,hl		;3062	f9		.
	ex de,hl		;3063	eb		.
	jp (hl)			;3064	e9		.
sub_3065h:
	call l0ee6h		;3065	cd e6 0e	. . .
	ld (iy+00ah),000h	;3068	fd 36 0a 00	. 6 . .
	ld de,0e8a9h		;306c	11 a9 e8	. . .
sub_306fh:
	ld ix,0e7a9h		;306f	dd 21 a9 e7	. ! . .
	ret			;3073	c9		.
	call sub_3065h		;3074	cd 65 30	. e 0
	rst 30h			;3077	f7		.
	ret nz			;3078	c0		.
	inc d			;3079	14		.
	rlca			;307a	07		.
l307bh:
	jp nc,l0afbh		;307b	d2 fb 0a	. . .
l307eh:
	rst 30h			;307e	f7		.
	ld l,h			;307f	6c		l
	ld b,006h		;3080	06 06		. .
l3082h:
	jp ExitToError		;3082	c3 06 0b	. . .
	ld b,002h		;3085	06 02		. .
	call sub_309fh		;3087	cd 9f 30	. . 0
	call sub_3107h		;308a	cd 07 31	. . 1
l308dh:
	jr c,l307eh		;308d	38 ef		8 .
	rst 30h			;308f	f7		.
	rlca			;3090	07		.
	inc de			;3091	13		.
	rlca			;3092	07		.
	jr l307bh		;3093	18 e6		. .
	ld b,001h		;3095	06 01		. .
	call sub_309fh		;3097	cd 9f 30	. . 0
	call sub_30d8h		;309a	cd d8 30	. . 0
	jr l308dh		;309d	18 ee		. .
sub_309fh:
	call sub_3065h		;309f	cd 65 30	. e 0
	rst 30h			;30a2	f7		.
	or b			;30a3	b0		.
	ld de,l3807h		;30a4	11 07 38	. . 8
	sub 0cdh		;30a7	d6 cd		. .
	cp d			;30a9	ba		.
	ld c,0feh		;30aa	0e fe		. .
	ld (bc),a		;30ac	02		.
	jr z,l30bbh		;30ad	28 0c		( .
	jr nc,l30d4h		;30af	30 23		0 #
	bit 0,(ix+017h)		;30b1	dd cb 17 46	. . . F
	jr nz,l30bbh		;30b5	20 04		  .
	ld a,00ah		;30b7	3e 0a		> .
	jr l3082h		;30b9	18 c7		. .
l30bbh:
	push hl			;30bb	e5		.
	push de			;30bc	d5		.
	push af			;30bd	f5		.
	rst 30h			;30be	f7		.
	rst 18h			;30bf	df		.
	ld de,l3807h		;30c0	11 07 38	. . 8
	cp d			;30c3	ba		.
	ld e,c			;30c4	59		Y
	ld d,b			;30c5	50		P
	pop af			;30c6	f1		.
	pop hl			;30c7	e1		.
	pop bc			;30c8	c1		.
	cp 001h			;30c9	fe 01		. .
	jr z,l30d1h		;30cb	28 04		( .
	ret nc			;30cd	d0		.
	ld hl,(0e7b2h)		;30ce	2a b2 e7	* . .
l30d1h:
	ld b,d			;30d1	42		B
	ld c,e			;30d2	4b		K
	ret			;30d3	c9		.
l30d4h:
	ld a,00bh		;30d4	3e 0b		> .
	jr l3082h		;30d6	18 aa		. .
sub_30d8h:
	ld a,b			;30d8	78		x
	or a			;30d9	b7		.
	jr z,l30f4h		;30da	28 18		( .
l30dch:
	push bc			;30dc	c5		.
	push hl			;30dd	e5		.
	rst 30h			;30de	f7		.
	ld h,l			;30df	65		e
	inc de			;30e0	13		.
	rlca			;30e1	07		.
	pop hl			;30e2	e1		.
	pop bc			;30e3	c1		.
	ret c			;30e4	d8		.
	push bc			;30e5	c5		.
	ld de,0e8a9h		;30e6	11 a9 e8	. . .
	ex de,hl		;30e9	eb		.
	ld bc,XorKeys_end	;30ea	01 00 01	. . .
	call sub_0749h		;30ed	cd 49 07	. I .
	ex de,hl		;30f0	eb		.
	pop bc			;30f1	c1		.
	djnz l30dch		;30f2	10 e8		. .
l30f4h:
	ld a,c			;30f4	79		y
	or a			;30f5	b7		.
	ret z			;30f6	c8		.
l30f7h:
	push hl			;30f7	e5		.
	push bc			;30f8	c5		.
	rst 30h			;30f9	f7		.
	ld b,b			;30fa	40		@
	ld (de),a		;30fb	12		.
	rlca			;30fc	07		.
	pop bc			;30fd	c1		.
	pop hl			;30fe	e1		.
	ret c			;30ff	d8		.
	rst 0			;3100	c7		.
	inc hl			;3101	23		#
	dec c			;3102	0d		.
	jr nz,l30f7h		;3103	20 f2		  .
	or a			;3105	b7		.
	ret			;3106	c9		.
sub_3107h:
	ld (0e7b2h),hl		;3107	22 b2 e7	" . .
	ld a,b			;310a	78		x
	or a			;310b	b7		.
	jr z,l3122h		;310c	28 14		( .
l310eh:
	push bc			;310e	c5		.
	ld de,0e8a9h		;310f	11 a9 e8	. . .
	ld bc,XorKeys_end	;3112	01 00 01	. . .
	call CopyAcrossBanks	;3115	cd 53 07	. S .
	push hl			;3118	e5		.
	rst 30h			;3119	f7		.
	sub l			;311a	95		.
	inc de			;311b	13		.
	rlca			;311c	07		.
	pop hl			;311d	e1		.
	pop bc			;311e	c1		.
	ret c			;311f	d8		.
	djnz l310eh		;3120	10 ec		. .
l3122h:
	ld a,c			;3122	79		y
	or a			;3123	b7		.
	ret z			;3124	c8		.
l3125h:
	push hl			;3125	e5		.
	push bc			;3126	c5		.
	rst 28h			;3127	ef		.
	rst 30h			;3128	f7		.
	adc a,h			;3129	8c		.
	ld (de),a		;312a	12		.
	rlca			;312b	07		.
	pop bc			;312c	c1		.
	pop hl			;312d	e1		.
	ret c			;312e	d8		.
	inc hl			;312f	23		#
	dec c			;3130	0d		.
	jr nz,l3125h		;3131	20 f2		  .
	or a			;3133	b7		.
	ret			;3134	c9		.
l3135h:
	xor a			;3135	af		.
	ld l,a			;3136	6f		o
	ld h,a			;3137	67		g
	ld bc,l00f6h		;3138	01 f6 00	. . .
	push bc			;313b	c5		.
	call sub_315ah		;313c	cd 5a 31	. Z 1
	inc hl			;313f	23		#
	ld bc,l000dh		;3140	01 0d 00	. . .
	call sub_315ah		;3143	cd 5a 31	. Z 1
	ld de,l000bh+1		;3146	11 0c 00	. . .
	add hl,de		;3149	19		.
	ld bc,l3ef0h		;314a	01 f0 3e	. . >
	call sub_315ah		;314d	cd 5a 31	. Z 1
	pop hl			;3150	e1		.
	cp (hl)			;3151	be		.
	jp z,ReadPort1FFD	;3152	ca d9 04	. . .
	call sub_305bh		;3155	cd 5b 30	. [ 0
	jr l3135h		;3158	18 db		. .
sub_315ah:
	add a,(hl)		;315a	86		.
	cpi			;315b	ed a1		. .
	jp pe,sub_315ah		;315d	ea 5a 31	. Z 1
	ret			;3160	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3161: command keyword Help of the 4.01
; monitor.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg3161' (start 0x3161 end 0x3165)
Msg3161_start:
	defb 048h		;3161	48		H
	defb 065h		;3162	65		e
	defb 06ch		;3163	6c		l
	defb 0f0h		;3164	f0		.
Msg3161_end:
	cp a			;3165	bf		.
	ld c,c			;3166	49		I
	ld c,c			;3167	49		I
	ret			;3168	c9		.
	ld c,a			;3169	4f		O
	ld c,a			;316a	4f		O
	rst 8			;316b	cf		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg316C: command keyword CMOS (NVRAM setup) of
; the 4.01 monitor.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg316C' (start 0x316c end 0x3170)
Msg316C_start:
	defb 043h		;316c	43		C
	defb 04dh		;316d	4d		M
	defb 04fh		;316e	4f		O
	defb 0d3h		;316f	d3		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3170: command keyword RESNVRAM (NVRAM reset)
; of the 4.01 monitor.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
Msg316C_end:

; BLOCK 'Msg3170' (start 0x3170 end 0x3178)
Msg3170_start:
	defb 052h		;3170	52		R
	defb 045h		;3171	45		E
	defb 053h		;3172	53		S
	defb 04eh		;3173	4e		N
	defb 056h		;3174	56		V
	defb 052h		;3175	52		R
	defb 041h		;3176	41		A
	defb 0cdh		;3177	cd		.
Msg3170_end:
	nop			;3178	00		.
	ret nc			;3179	d0		.
	ld a,(08900h)		;317a	3a 00 89	: . .
	inc sp			;317d	33		3
	ld bc,l318ch		;317e	01 8c 31	. . 1
	nop			;3181	00		.
	sub h			;3182	94		.
	ld sp,09c00h		;3183	31 00 9c	1 . .
	ld sp,0a400h		;3186	31 00 a4	1 . .
	ld sp,WriteAnyBankByte	;3189	31 00 00	1 . .
l318ch:
	rst 30h			;318c	f7		.
	cp c			;318d	b9		.
	ld d,007h		;318e	16 07		. .
	ret c			;3190	d8		.
	ld a,081h		;3191	3e 81		> .
	ret			;3193	c9		.
	rst 30h			;3194	f7		.
	rst 0			;3195	c7		.
	dec c			;3196	0d		.
	rlca			;3197	07		.
	ret c			;3198	d8		.
	ld a,081h		;3199	3e 81		> .
	ret			;319b	c9		.
	rst 30h			;319c	f7		.
	xor l			;319d	ad		.
	dec c			;319e	0d		.
	rlca			;319f	07		.
	ret c			;31a0	d8		.
	ld a,081h		;31a1	3e 81		> .
	ret			;31a3	c9		.
	rst 30h			;31a4	f7		.
	ld (hl),d		;31a5	72		r
	ld c,007h		;31a6	0e 07		. .
	ld a,081h		;31a8	3e 81		> .
	ret			;31aa	c9		.
	ld a,d			;31ab	7a		z
	or a			;31ac	b7		.
	ld a,02ch		;31ad	3e 2c		> ,
	ret nz			;31af	c0		.
	ld (iy+016h),e		;31b0	fd 73 16	. s .
	ld hl,(UserPc)		;31b3	2a 6b dd	* k .
	call l18c0h		;31b6	cd c0 18	. . .
l31b9h:
	call CommandLoop	;31b9	cd 92 2c	. . ,
	call sub_37b9h		;31bc	cd b9 37	. . 7
	jr l31b9h		;31bf	18 f8		. .
	ld hl,WatchTable	;31c1	21 2d e5	! - .
	ld de,l2827h		;31c4	11 27 28	. ' (
	ld bc,l31fch		;31c7	01 fc 31	. . 1
	ld a,009h		;31ca	3e 09		> .
	push bc			;31cc	c5		.
	push af			;31cd	f5		.
	push hl			;31ce	e5		.
	push de			;31cf	d5		.
	call InitErrorWindow	;31d0	cd 95 3a	. . :
	ld b,e			;31d3	43		C
	ld c,000h		;31d4	0e 00		. .
	rst 8			;31d6	cf		.
	rst 20h			;31d7	e7		.
	dec c			;31d8	0d		.
	adc a,l			;31d9	8d		.
	pop bc			;31da	c1		.
	ld c,000h		;31db	0e 00		. .
	rst 8			;31dd	cf		.
	rst 20h			;31de	e7		.
	dec c			;31df	0d		.
	adc a,l			;31e0	8d		.
	pop hl			;31e1	e1		.
	pop de			;31e2	d1		.
	ld e,001h		;31e3	1e 01		. .
	pop bc			;31e5	c1		.
l31e6h:
	push de			;31e6	d5		.
	push bc			;31e7	c5		.
	call sub_3a93h		;31e8	cd 93 3a	. . :
	jr c,l31f9h		;31eb	38 0c		8 .
	call sub_37b9h		;31ed	cd b9 37	. . 7
	rst 20h			;31f0	e7		.
	adc a,l			;31f1	8d		.
	pop bc			;31f2	c1		.
	pop de			;31f3	d1		.
	inc e			;31f4	1c		.
	ld a,d			;31f5	7a		z
	cp e			;31f6	bb		.
	jr nz,l31e6h		;31f7	20 ed		  .
l31f9h:
	ld a,081h		;31f9	3e 81		> .
	ret			;31fb	c9		.
l31fch:
	rst 30h			;31fc	f7		.
	ld d,(hl)		;31fd	56		V
	ld bc,0c906h		;31fe	01 06 c9	. . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PrintOkError: "Ok" or the pending error after a command.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PrintOkError:
	push hl			;3201	e5		.
	jr nz,l320ah		;3202	20 06		  .
	rst 20h			;3204	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgOff: inline RST 20h string OF+$C6 which prints
; OFF - the terminating byte is emitted with bit 7
; stripped; used by the watch/breakpoint listing
; routine above.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgOff' (start 0x3205 end 0x3208)
MsgOff_start:
	defb 04fh		;3205	4f		O
	defb 046h		;3206	46		F
	defb 0c6h		;3207	c6		.
MsgOff_end:
	pop hl			;3208	e1		.
	ret			;3209	c9		.
l320ah:
	rst 20h			;320a	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgOn: ON+$A0, printing ON - companion of MsgOff
; ($3205).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgOn' (start 0x320b end 0x320e)
MsgOn_start:
	defb 04fh		;320b	4f		O
	defb 04eh		;320c	4e		N
	defb 0a0h		;320d	a0		.
MsgOn_end:
	pop hl			;320e	e1		.
	ret			;320f	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; LookupCommand: walk CommandTable (E80B, 2-byte entries)
; matching the entered verb.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
LookupCommand:
	ld de,CommandTable	;3210	11 bd e9	. . .
	ld l,a			;3213	6f		o
	ld h,000h		;3214	26 00		& .
	add hl,hl		;3216	29		)
	add hl,de		;3217	19		.
	ld e,(hl)		;3218	5e		^
	push hl			;3219	e5		.
	inc hl			;321a	23		#
	ld d,(hl)		;321b	56		V
	ld l,a			;321c	6f		o
	ld a,d			;321d	7a		z
	or e			;321e	b3		.
	ld a,l			;321f	7d		}
	pop hl			;3220	e1		.
	ret			;3221	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckWatchpoints: after every step compare the 8 entries of
; WatchTable (E394, 11 bytes each: address, length, value...)
; against the visible banks; a hit re-enters the monitor.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckWatchpoints:
	res 7,(iy+00bh)		;3222	fd cb 0b be	. . . .
	jr FindWatchpoint	;3226	18 04		. .
sub_3228h:
	set 7,(iy+00bh)		;3228	fd cb 0b fe	. . . .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; FindWatchpoint: locate a free/owning slot in WatchTable.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
FindWatchpoint:
	call InitWatchTable	;322c	cd 81 32	. . 2
l322fh:
	call sub_326ch		;322f	cd 6c 32	. l 2
	jr nc,l3267h		;3232	30 33		0 3
	call SavePagingShadow	;3234	cd e0 35	. . 5
	exx			;3237	d9		.
	ld a,h			;3238	7c		|
	exx			;3239	d9		.
	and 0c0h		;323a	e6 c0		. .
	jr z,l3250h		;323c	28 12		( .
	bit 5,(iy-002h)		;323e	fd cb fe 6e	. . . n
	jr z,l324bh		;3242	28 07		( .
	call sub_3707h		;3244	cd 07 37	. . 7
	exx			;3247	d9		.
	jr nz,l3264h		;3248	20 1a		  .
	exx			;324a	d9		.
l324bh:
	push bc			;324b	c5		.
	call l3732h		;324c	cd 32 37	. 2 7
	pop bc			;324f	c1		.
l3250h:
	exx			;3250	d9		.
	bit 7,(iy+00bh)		;3251	fd cb 0b 7e	. . . ~
	jr z,l3260h		;3255	28 09		( .
	rst 28h			;3257	ef		.
	ld (ix+004h),a		;3258	dd 77 04	. w .
	ld a,0cfh		;325b	3e cf		> .
	rst 0			;325d	c7		.
	jr l3264h		;325e	18 04		. .
l3260h:
	ld a,(ix+004h)		;3260	dd 7e 04	. ~ .
	rst 0			;3263	c7		.
l3264h:
	call RestorePaging	;3264	cd 4e 3a	. N :
l3267h:
	add ix,de		;3267	dd 19		. .
	djnz l322fh		;3269	10 c4		. .
	ret			;326b	c9		.
sub_326ch:
	ld a,(ix+000h)		;326c	dd 7e 00	. ~ .
	rlca			;326f	07		.
	ret nc			;3270	d0		.
	ccf			;3271	3f		?
	bit 1,a			;3272	cb 4f		. O
	ret nz			;3274	c0		.
	ld l,(ix+002h)		;3275	dd 6e 02	. n .
	ld h,(ix+003h)		;3278	dd 66 03	. f .
	exx			;327b	d9		.
	ld l,(ix+001h)		;327c	dd 6e 01	. n .
	ccf			;327f	3f		?
	ret			;3280	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; InitWatchTable: clear the watchpoint table.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
InitWatchTable:
	ld ix,WatchTable	;3281	dd 21 2d e5	. ! - .
	ld b,008h		;3285	06 08		. .
	ld de,l000bh		;3287	11 0b 00	. . .
	ret			;328a	c9		.
sub_328bh:
	call InitWatchTable	;328b	cd 81 32	. . 2
l328eh:
	ld a,(ix+000h)		;328e	dd 7e 00	. ~ .
	rlca			;3291	07		.
	jr nc,l32a9h		;3292	30 15		0 .
	exx			;3294	d9		.
	ld l,(ix+001h)		;3295	dd 6e 01	. n .
	call sub_3707h		;3298	cd 07 37	. . 7
	exx			;329b	d9		.
	jr nz,l32a9h		;329c	20 0b		  .
	ld a,(ix+002h)		;329e	dd 7e 02	. ~ .
	cp l			;32a1	bd		.
	jr nz,l32a9h		;32a2	20 05		  .
	ld a,(ix+003h)		;32a4	dd 7e 03	. ~ .
	cp h			;32a7	bc		.
	ret z			;32a8	c8		.
l32a9h:
	add ix,de		;32a9	dd 19		. .
	djnz l328eh		;32ab	10 e1		. .
	scf			;32ad	37		7
	ret			;32ae	c9		.
	call sub_32c2h		;32af	cd c2 32	. . 2
	ld (ix+000h),000h	;32b2	dd 36 00 00	. 6 . .
	ld a,(RegisterBackup)	;32b6	3a 99 dd	: . .
	call LookupCommand	;32b9	cd 10 32	. . 2
	xor a			;32bc	af		.
	ld (hl),a		;32bd	77		w
	inc hl			;32be	23		#
	ld (hl),a		;32bf	77		w
	jr l32fbh		;32c0	18 39		. 9
sub_32c2h:
	push af			;32c2	f5		.
	dec e			;32c3	1d		.
	ld a,e			;32c4	7b		{
	and 0f8h		;32c5	e6 f8		. .
	or d			;32c7	b2		.
	ld a,022h		;32c8	3e 22		> "
	jr nz,l3300h		;32ca	20 34		  4
	push hl			;32cc	e5		.
	ld l,e			;32cd	6b		k
	ld h,d			;32ce	62		b
	add hl,hl		;32cf	29		)
	add hl,hl		;32d0	29		)
	add hl,de		;32d1	19		.
	add hl,hl		;32d2	29		)
	add hl,de		;32d3	19		.
	ld de,WatchTable	;32d4	11 2d e5	. - .
	add hl,de		;32d7	19		.
	push hl			;32d8	e5		.
	pop ix			;32d9	dd e1		. .
	pop hl			;32db	e1		.
	pop af			;32dc	f1		.
	bit 6,(ix+000h)		;32dd	dd cb 00 76	. . . v
	ret			;32e1	c9		.
	call sub_32c2h		;32e2	cd c2 32	. . 2
	ld a,026h		;32e5	3e 26		> &
	jr z,l3300h		;32e7	28 17		( .
	ld a,l			;32e9	7d		}
	and 0feh		;32ea	e6 fe		. .
	or h			;32ec	b4		.
	ld a,023h		;32ed	3e 23		> #
	jr nz,l3300h		;32ef	20 0f		  .
sub_32f1h:
	sla (ix+000h)		;32f1	dd cb 00 26	. . . &
	srl l			;32f5	cb 3d		. =
	rr (ix+000h)		;32f7	dd cb 00 1e	. . . .
l32fbh:
	ld a,081h		;32fb	3e 81		> .
	ret			;32fd	c9		.
	ld a,00ah		;32fe	3e 0a		> .
l3300h:
	jp ExitToError		;3300	c3 06 0b	. . .
sub_3303h:
	call sub_32c2h		;3303	cd c2 32	. . 2
	rst 30h			;3306	f7		.
	call po,l0601h		;3307	e4 01 06	. . .
	ret			;330a	c9		.
	ld de,Rst08Vector	;330b	11 08 00	. . .
	ld bc,(0dd8bh)		;330e	ed 4b 8b dd	. K . .
	ld a,b			;3312	78		x
	and 0c0h		;3313	e6 c0		. .
	ld hl,l0001h		;3315	21 01 00	! . .
	ld (0dd9fh),hl		;3318	22 9f dd	" . .
	jr z,l331eh		;331b	28 01		( .
	dec hl			;331d	2b		+
l331eh:
	ld a,004h		;331e	3e 04		> .
	call sub_3303h		;3320	cd 03 33	. . 3
l3323h:
	jp l3960h		;3323	c3 60 39	. ` 9
	ld de,Rst08Vector	;3326	11 08 00	. . .
	call sub_32c2h		;3329	cd c2 32	. . 2
	ld l,000h		;332c	2e 00		. .
	call sub_32f1h		;332e	cd f1 32	. . 2
	jr l3323h		;3331	18 f0		. .
	ld a,e			;3333	7b		{
	and 0f8h		;3334	e6 f8		. .
	or d			;3336	b2		.
	ld a,00dh		;3337	3e 0d		> .
	ret nz			;3339	c0		.
	ld a,l			;333a	7d		}
	and 0feh		;333b	e6 fe		. .
	or h			;333d	b4		.
	ld a,017h		;333e	3e 17		> .
	ret nz			;3340	c0		.
	inc h			;3341	24		$
	ld a,e			;3342	7b		{
l3343h:
	or a			;3343	b7		.
	jr z,l334bh		;3344	28 05		( .
	dec a			;3346	3d		=
	sla h			;3347	cb 24		. $
	jr l3343h		;3349	18 f8		. .
l334bh:
	ld a,h			;334b	7c		|
	bit 0,l			;334c	cb 45		. E
	jr nz,l335bh		;334e	20 0b		  .
	xor 0ffh		;3350	ee ff		. .
	and (iy+005h)		;3352	fd a6 05	. . .
l3355h:
	ld (iy+005h),a		;3355	fd 77 05	. w .
	ld a,081h		;3358	3e 81		> .
	ret			;335a	c9		.
l335bh:
	or (iy+005h)		;335b	fd b6 05	. . .
	jr l3355h		;335e	18 f5		. .
	ld a,h			;3360	7c		|
	or a			;3361	b7		.
	ld a,011h		;3362	3e 11		> .
	ret nz			;3364	c0		.
	ld c,e			;3365	4b		K
	ld b,d			;3366	42		B
	out (c),l		;3367	ed 69		. i
l3369h:
	ld a,081h		;3369	3e 81		> .
	ret			;336b	c9		.
	ld a,h			;336c	7c		|
	or a			;336d	b7		.
	ld a,011h		;336e	3e 11		> .
	ret nz			;3370	c0		.
	ld c,e			;3371	4b		K
	ld b,d			;3372	42		B
	ld a,l			;3373	7d		}
	rst 30h			;3374	f7		.
	ret p			;3375	f0		.
	ccf			;3376	3f		?
	inc bc			;3377	03		.
	jr l3369h		;3378	18 ef		. .
	ld c,e			;337a	4b		K
	ld b,d			;337b	42		B
	rst 30h			;337c	f7		.
	di			;337d	f3		.
	ccf			;337e	3f		?
	inc bc			;337f	03		.
	ld e,a			;3380	5f		_
	jr l3387h		;3381	18 04		. .
	ld c,e			;3383	4b		K
	ld b,d			;3384	42		B
	in e,(c)		;3385	ed 58		. X
l3387h:
	ld d,000h		;3387	16 00		. .
sub_3389h:
	call PopupTabStops_end	;3389	cd bb 33	. . 3
	ld c,e			;338c	4b		K
	ld b,d			;338d	42		B
	ld a,b			;338e	78		x
	or a			;338f	b7		.
	jr nz,l33a3h		;3390	20 11		  .
	ld a,c			;3392	79		y
	rlca			;3393	07		.
	jr c,l33a3h		;3394	38 0d		8 .
	rrca			;3396	0f		.
	cp 020h			;3397	fe 20		.  
	jr c,l33a3h		;3399	38 08		8 .
	push af			;339b	f5		.
	rst 20h			;339c	e7		.
	dec c			;339d	0d		.
	and d			;339e	a2		.
	pop af			;339f	f1		.
	rst 10h			;33a0	d7		.
	rst 20h			;33a1	e7		.
	and d			;33a2	a2		.
l33a3h:
	ld hl,PopupTabStops_start	;33a3	21 b6 33	! . 3
l33a6h:
	ld a,(hl)		;33a6	7e		~
	cp 081h			;33a7	fe 81		. .
	ret z			;33a9	c8		.
	push bc			;33aa	c5		.
	push af			;33ab	f5		.
	rst 20h			;33ac	e7		.
	adc a,l			;33ad	8d		.
	pop af			;33ae	f1		.
	call sub_1629h		;33af	cd 29 16	. ) .
	pop bc			;33b2	c1		.
	inc hl			;33b3	23		#
	jr l33a6h		;33b4	18 f0		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; PopupTabStops: four tab columns (0,2,4,6) closed
; by the $81 sentinel; the loop at $33A3 feeds each
; value to the print helper to lay out the popup
; menu line right before OpenPopup ($33BB).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'PopupTabStops' (start 0x33b6 end 0x33bb)
PopupTabStops_start:
	defb 000h		;33b6	00		.
	defb 002h		;33b7	02		.
	defb 004h		;33b8	04		.
	defb 006h		;33b9	06		.
	defb 081h		;33ba	81		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; OpenPopup: pop up a window from the descriptor at
; PopupWindowDef (E06D).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
PopupTabStops_end:
OpenPopup:
	push de			;33bb	d5		.
	push bc			;33bc	c5		.
	push hl			;33bd	e5		.
	ld hl,PopupWindowDef	;33be	21 ad e0	! . .
	call SetWorkspace	;33c1	cd 1c 30	. . 0
	call sub_2a5eh		;33c4	cd 5e 2a	. ^ *
	rst 20h			;33c7	e7		.
	adc a,l			;33c8	8d		.
	pop hl			;33c9	e1		.
	pop bc			;33ca	c1		.
	pop de			;33cb	d1		.
	ret			;33cc	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; TapeMenu: the tape command cluster - save/verify/leader state
; machine; control flow is dense here (cf. the feeder at
; FeedTapeOutput and the key waits in this region).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
TapeMenu:
	bit 5,(iy+005h)		;33cd	fd cb 05 6e	. . . n
	ret nz			;33d1	c0		.
	call PopupTabStops_end	;33d2	cd bb 33	. . 3
	rst 20h			;33d5	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgAnalyser: menu title Analyser  printed as an
; inline RST 20h string right after the popup at
; $33CD is opened.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgAnalyser' (start 0x33d6 end 0x33e0)
MsgAnalyser_start:
	defb 041h		;33d6	41		A
	defb 06eh		;33d7	6e		n
	defb 061h		;33d8	61		a
	defb 06ch		;33d9	6c		l
	defb 079h		;33da	79		y
	defb 073h		;33db	73		s
	defb 065h		;33dc	65		e
	defb 072h		;33dd	72		r
	defb 020h		;33de	20		 
	defb 0a0h		;33df	a0		.
MsgAnalyser_end:
	bit 4,(iy+014h)		;33e0	fd cb 14 66	. . . f
	call PrintOkError	;33e4	cd 01 32	. . 2
	rst 20h			;33e7	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgBase: popup-menu item base led by a carriage
; return byte; the $A0 terminator prints as a
; trailing space.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgBase' (start 0x33e8 end 0x33ee)
MsgBase_start:
	defb 00dh		;33e8	0d		.
	defb 062h		;33e9	62		b
	defb 061h		;33ea	61		a
	defb 073h		;33eb	73		s
	defb 065h		;33ec	65		e
	defb 0a0h		;33ed	a0		.
MsgBase_end:
	ld de,Rst08Vector+2	;33ee	11 0a 00	. . .
	ld c,(iy+017h)		;33f1	fd 4e 17	. N .
	ld b,d			;33f4	42		B
	call l1677h		;33f5	cd 77 16	. w .
	rst 20h			;33f8	e7		.
	adc a,l			;33f9	8d		.
	ld bc,l3c00h		;33fa	01 00 3c	. . <
	rst 8			;33fd	cf		.
	ld bc,(StepContext)	;33fe	ed 4b 1a e1	. K . .
	call sub_1661h		;3402	cd 61 16	. a .
	rst 20h			;3405	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgOption: popup-menu item option (CR-prefixed,
; $A0-terminated).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgOption' (start 0x3406 end 0x340e)
MsgOption_start:
	defb 00dh		;3406	0d		.
	defb 06fh		;3407	6f		o
	defb 070h		;3408	70		p
	defb 074h		;3409	74		t
	defb 069h		;340a	69		i
	defb 06fh		;340b	6f		o
	defb 06eh		;340c	6e		n
	defb 0a0h		;340d	a0		.
MsgOption_end:
	ld l,(iy+005h)		;340e	fd 6e 05	. n .
	call sub_2e25h		;3411	cd 25 2e	. % .
	rst 20h			;3414	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; MsgIntMode: popup-menu item Int mode (CR-prefixed,
; $A0-terminated).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'MsgIntMode' (start 0x3415 end 0x341f)
MsgIntMode_start:
	defb 00dh		;3415	0d		.
	defb 049h		;3416	49		I
	defb 06eh		;3417	6e		n
	defb 074h		;3418	74		t
	defb 020h		;3419	20		 
	defb 06dh		;341a	6d		m
	defb 06fh		;341b	6f		o
	defb 064h		;341c	64		d
	defb 065h		;341d	65		e
	defb 0a0h		;341e	a0		.
MsgIntMode_end:
	call sub_3b75h		;341f	cd 75 3b	. u ;
	ld a,032h		;3422	3e 32		> 2
	sub d			;3424	92		.
	rst 10h			;3425	d7		.
	bit 6,(iy+019h)		;3426	fd cb 19 76	. . . v
	jr z,l3447h		;342a	28 1b		( .
	rst 20h			;342c	e7		.
	jr z,$+118		;342d	28 74		( t
	xor c			;342f	a9		.
	jr l3447h		;3430	18 15		. .
	ld a,d			;3432	7a		z
	or a			;3433	b7		.
	jr nz,l3441h		;3434	20 0b		  .
	ld a,e			;3436	7b		{
	ld bc,l0003h+2		;3437	01 05 00	. . .
	ld hl,ScreenModeValues_start	;343a	21 4a 34	! J 4
	cpir			;343d	ed b1		. .
	jr z,l3444h		;343f	28 03		( .
l3441h:
	ld a,00dh		;3441	3e 0d		> .
	ret			;3443	c9		.
l3444h:
	ld (iy+017h),e		;3444	fd 73 17	. s .
l3447h:
	ld a,081h		;3447	3e 81		> .
	ret			;3449	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScreenModeValues: the five legal screen-
; configuration values for the paging port;
; validated with cpir at $343D (the index of the
; current value is kept in the workspace) - any
; other value returns a carriage return.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'ScreenModeValues' (start 0x344a end 0x344f)
ScreenModeValues_start:
	defb 000h		;344a	00		.
	defb 002h		;344b	02		.
	defb 008h		;344c	08		.
	defb 00ah		;344d	0a		.
	defb 010h		;344e	10		.
ScreenModeValues_end:
	ld a,d			;344f	7a		z
	cp 05bh			;3450	fe 5b		. [
	ld a,03bh		;3452	3e 3b		> ;
	ret c			;3454	d8		.
	ld a,d			;3455	7a		z
	cp 0c0h			;3456	fe c0		. .
	ld a,03bh		;3458	3e 3b		> ;
	ret nc			;345a	d0		.
	ld (StepContext),de	;345b	ed 53 1a e1	. S . .
	jr l3447h		;345f	18 e6		. .
	ld de,l0318h		;3461	11 18 03	. . .
	set 0,(iy+013h)		;3464	fd cb 13 c6	. . . .
	jr l3471h		;3468	18 07		. .
	ld de,l0216h+1		;346a	11 17 02	. . .
	res 0,(iy+013h)		;346d	fd cb 13 86	. . . .
l3471h:
	ld a,b			;3471	78		x
	cp d			;3472	ba		.
	ld a,00ah		;3473	3e 0a		> .
	ret c			;3475	d8		.
	ret z			;3476	c8		.
	ld a,b			;3477	78		x
	cp e			;3478	bb		.
	ld a,01fh		;3479	3e 1f		> .
	ret nc			;347b	d0		.
	ld a,b			;347c	78		x
	sub d			;347d	92		.
	push af			;347e	f5		.
	xor a			;347f	af		.
	ld (0dde0h),a		;3480	32 e0 dd	2 . .
	call sub_0c98h		;3483	cd 98 0c	. . .
	push de			;3486	d5		.
	call sub_0c98h		;3487	cd 98 0c	. . .
	ex (sp),hl		;348a	e3		.
	ex de,hl		;348b	eb		.
	call sub_375ch		;348c	cd 5c 37	. \ 7
	ld (ErrorTextTable),de	;348f	ed 53 dc dd	. S . .
	ld (ErrorTextTable2),hl	;3493	22 de dd	" . .
	ld hl,(PagingState)	;3496	2a 12 e0	* . .
	ld (0dde1h),hl		;3499	22 e1 dd	" . .
	ld (0e010h),hl		;349c	22 10 e0	" . .
	pop hl			;349f	e1		.
	bit 0,(iy+013h)		;34a0	fd cb 13 46	. . . F
	jr z,l34b7h		;34a4	28 11		( .
	call sub_0c98h		;34a6	cd 98 0c	. . .
	ex de,hl		;34a9	eb		.
	call sub_37ebh		;34aa	cd eb 37	. . 7
	call RestorePagingShadow	;34ad	cd ec 35	. . 5
	ld hl,(PagingState)	;34b0	2a 12 e0	* . .
	ld (0dde1h),hl		;34b3	22 e1 dd	" . .
	ex de,hl		;34b6	eb		.
l34b7h:
	ld bc,0dde3h		;34b7	01 e3 dd	. . .
	pop af			;34ba	f1		.
	ld (0dde0h),a		;34bb	32 e0 dd	2 . .
l34beh:
	push bc			;34be	c5		.
	call sub_0d55h		;34bf	cd 55 0d	. U .
	pop bc			;34c2	c1		.
	jr c,SearchPattern	;34c3	38 05		8 .
	ld a,e			;34c5	7b		{
	ld (bc),a		;34c6	02		.
	inc bc			;34c7	03		.
	jr l34beh		;34c8	18 f4		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SearchPattern: wildcard search across banks through the
; bank window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SearchPattern:
	ld hl,(ErrorTextTable)	;34ca	2a dc dd	* . .
l34cdh:
	ld bc,(0dddfh)		;34cd	ed 4b df dd	. K . .
	push hl			;34d1	e5		.
	ld de,0dde3h		;34d2	11 e3 dd	. . .
l34d5h:
	rst 28h			;34d5	ef		.
	ld c,a			;34d6	4f		O
	ld a,(de)		;34d7	1a		.
	cp c			;34d8	b9		.
	jr nz,l3502h		;34d9	20 27		  '
	inc hl			;34db	23		#
	inc de			;34dc	13		.
	djnz l34d5h		;34dd	10 f6		. .
	ld (ErrorTextTable),hl	;34df	22 dc dd	" . .
	pop hl			;34e2	e1		.
	bit 0,(iy+013h)		;34e3	fd cb 13 46	. . . F
	jr nz,l34ech		;34e7	20 03		  .
	ld (0dd69h),hl		;34e9	22 69 dd	" i .
l34ech:
	ld (0dda7h),hl		;34ec	22 a7 dd	" . .
	ld a,084h		;34ef	3e 84		> .
l34f1h:
	push af			;34f1	f5		.
	call RestorePaging	;34f2	cd 4e 3a	. N :
	ld a,(iy+00ah)		;34f5	fd 7e 0a	. ~ .
	and 0cfh		;34f8	e6 cf		. .
	ld (iy+00ah),a		;34fa	fd 77 0a	. w .
	call sub_2ca8h		;34fd	cd a8 2c	. . ,
l3500h:
	pop af			;3500	f1		.
	ret			;3501	c9		.
l3502h:
	pop hl			;3502	e1		.
	push hl			;3503	e5		.
	or a			;3504	b7		.
	ld de,(ErrorTextTable2)	;3505	ed 5b de dd	. [ . .
	sbc hl,de		;3509	ed 52		. R
	pop hl			;350b	e1		.
	inc hl			;350c	23		#
	jr c,l34cdh		;350d	38 be		8 .
	xor a			;350f	af		.
	ld (0dde0h),a		;3510	32 e0 dd	2 . .
	ld a,0a0h		;3513	3e a0		> .
	jr l34f1h		;3515	18 da		. .
	ld a,(0dde0h)		;3517	3a e0 dd	: . .
	or a			;351a	b7		.
	ld a,0a1h		;351b	3e a1		> .
	ret z			;351d	c8		.
	ld a,00ch		;351e	3e 0c		> .
	rst 30h			;3520	f7		.
	add a,h			;3521	84		.
	inc e			;3522	1c		.
	dec b			;3523	05		.
	call SavePagingShadow	;3524	cd e0 35	. . 5
	ld hl,(0dde1h)		;3527	2a e1 dd	* . .
	ld (PagingState),hl	;352a	22 12 e0	" . .
	jr SearchPattern	;352d	18 9b		. .
	call sub_3766h		;352f	cd 66 37	. f 7
	inc hl			;3532	23		#
	or a			;3533	b7		.
	sbc hl,de		;3534	ed 52		. R
	push bc			;3536	c5		.
	push hl			;3537	e5		.
	pop bc			;3538	c1		.
	pop hl			;3539	e1		.
	ex de,hl		;353a	eb		.
	ld a,c			;353b	79		y
	or b			;353c	b0		.
	ld a,042h		;353d	3e 42		> B
	ret z			;353f	c8		.
	push hl			;3540	e5		.
	sbc hl,de		;3541	ed 52		. R
	pop hl			;3543	e1		.
	jr c,l3551h		;3544	38 0b		8 .
l3546h:
	call sub_3563h		;3546	cd 63 35	. c 5
	inc hl			;3549	23		#
	inc de			;354a	13		.
	ld a,c			;354b	79		y
	or b			;354c	b0		.
	jr nz,l3546h		;354d	20 f7		  .
	jr l3560h		;354f	18 0f		. .
l3551h:
	dec bc			;3551	0b		.
	add hl,bc		;3552	09		.
	ex de,hl		;3553	eb		.
	add hl,bc		;3554	09		.
l3555h:
	ex de,hl		;3555	eb		.
	inc bc			;3556	03		.
l3557h:
	call sub_3563h		;3557	cd 63 35	. c 5
	dec hl			;355a	2b		+
	dec de			;355b	1b		.
	ld a,b			;355c	78		x
	or c			;355d	b1		.
	jr nz,l3557h		;355e	20 f7		  .
l3560h:
	jp l3679h		;3560	c3 79 36	. y 6
sub_3563h:
	call RestorePaging	;3563	cd 4e 3a	. N :
	rst 28h			;3566	ef		.
	call RestorePagingShadow	;3567	cd ec 35	. . 5
	ex de,hl		;356a	eb		.
	rst 0			;356b	c7		.
	ex de,hl		;356c	eb		.
	dec bc			;356d	0b		.
	ret			;356e	c9		.
	call sub_3766h		;356f	cd 66 37	. f 7
	push bc			;3572	c5		.
	push hl			;3573	e5		.
	pop bc			;3574	c1		.
	pop hl			;3575	e1		.
	ex de,hl		;3576	eb		.
l3577h:
	ex de,hl		;3577	eb		.
	call RestorePagingShadow	;3578	cd ec 35	. . 5
	rst 28h			;357b	ef		.
	ex de,hl		;357c	eb		.
	call RestorePaging	;357d	cd 4e 3a	. N :
	push bc			;3580	c5		.
	ld c,a			;3581	4f		O
	rst 28h			;3582	ef		.
	cp c			;3583	b9		.
	jr nz,l3598h		;3584	20 12		  .
l3586h:
	pop bc			;3586	c1		.
	push hl			;3587	e5		.
	or a			;3588	b7		.
	sbc hl,bc		;3589	ed 42		. B
	pop hl			;358b	e1		.
	inc de			;358c	13		.
	inc hl			;358d	23		#
	jr nz,l3577h		;358e	20 e7		  .
	call sub_35d3h		;3590	cd d3 35	. . 5
	ld a,082h		;3593	3e 82		> .
	ret nz			;3595	c0		.
	dec a			;3596	3d		=
	ret			;3597	c9		.
l3598h:
	ld b,a			;3598	47		G
	push hl			;3599	e5		.
	push de			;359a	d5		.
	push bc			;359b	c5		.
	call sub_35d3h		;359c	cd d3 35	. . 5
	call nz,InitErrorWindow	;359f	c4 95 3a	. . :
	ld bc,04100h		;35a2	01 00 41	. . A
	rst 8			;35a5	cf		.
	pop bc			;35a6	c1		.
	pop de			;35a7	d1		.
	pop hl			;35a8	e1		.
	push hl			;35a9	e5		.
	push de			;35aa	d5		.
	push bc			;35ab	c5		.
	call sub_1612h		;35ac	cd 12 16	. . .
	rst 20h			;35af	e7		.
	jr nz,l3555h		;35b0	20 a3		  .
	pop af			;35b2	f1		.
	push af			;35b3	f5		.
	call PrintHexByte	;35b4	cd e3 16	. . .
	rst 20h			;35b7	e7		.
	jr nz,l35f4h		;35b8	20 3a		  :
	and b			;35ba	a0		.
	pop bc			;35bb	c1		.
	pop hl			;35bc	e1		.
	push hl			;35bd	e5		.
	push bc			;35be	c5		.
	call sub_1612h		;35bf	cd 12 16	. . .
	rst 20h			;35c2	e7		.
	jr nz,$-91		;35c3	20 a3		  .
	pop hl			;35c5	e1		.
	ld a,l			;35c6	7d		}
	call PrintHexByte	;35c7	cd e3 16	. . .
	rst 20h			;35ca	e7		.
	adc a,l			;35cb	8d		.
	call sub_37b9h		;35cc	cd b9 37	. . 7
	pop de			;35cf	d1		.
	pop hl			;35d0	e1		.
	jr l3586h		;35d1	18 b3		. .
sub_35d3h:
	push de			;35d3	d5		.
	push ix			;35d4	dd e5		. .
	ex (sp),hl		;35d6	e3		.
	ld de,PromptWindowDef	;35d7	11 75 e0	. u .
	or a			;35da	b7		.
	sbc hl,de		;35db	ed 52		. R
	pop hl			;35dd	e1		.
	pop de			;35de	d1		.
	ret			;35df	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; SavePagingShadow / RestorePagingShadow ($35EC): stash the
; real paging ($E012) into the $E00E/$E010 shadows while the
; monitor reprograms banks.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
SavePagingShadow:
	push hl			;35e0	e5		.
	ld hl,(PagingState)	;35e1	2a 12 e0	* . .
	ld (0e010h),hl		;35e4	22 10 e0	" . .
	ld (PagingBackup),hl	;35e7	22 0e e0	" . .
	pop hl			;35ea	e1		.
	ret			;35eb	c9		.
RestorePagingShadow:
	push hl			;35ec	e5		.
	ld hl,(PagingBackup)	;35ed	2a 0e e0	* . .
	ld (PagingState),hl	;35f0	22 12 e0	" . .
	pop hl			;35f3	e1		.
l35f4h:
	ret			;35f4	c9		.
	call sub_3766h		;35f5	cd 66 37	. f 7
	ld a,b			;35f8	78		x
	or a			;35f9	b7		.
	ld a,00ch		;35fa	3e 0c		> .
	ret nz			;35fc	c0		.
	call RestorePagingShadow	;35fd	cd ec 35	. . 5
	ex de,hl		;3600	eb		.
l3601h:
	ld a,c			;3601	79		y
	rst 0			;3602	c7		.
	push hl			;3603	e5		.
	or a			;3604	b7		.
	sbc hl,de		;3605	ed 52		. R
	pop hl			;3607	e1		.
	inc hl			;3608	23		#
	jr nz,l3601h		;3609	20 f6		  .
	jr l3679h		;360b	18 6c		. l
	call sub_0c98h		;360d	cd 98 0c	. . .
	ld c,e			;3610	4b		K
	ld b,d			;3611	42		B
	jr l3618h		;3612	18 04		. .
	ld bc,(UserPc)		;3614	ed 4b 6b dd	. K k .
l3618h:
	set 1,(iy+013h)		;3618	fd cb 13 ce	. . . .
	jr l3626h		;361c	18 08		. .
	ld bc,(0dd69h)		;361e	ed 4b 69 dd	. K i .
	res 1,(iy+013h)		;3622	fd cb 13 8e	. . . .
l3626h:
	push bc			;3626	c5		.
	call sub_0d55h		;3627	cd 55 0d	. U .
	pop bc			;362a	c1		.
	jr c,l367ch		;362b	38 4f		8 O
	push hl			;362d	e5		.
	ld l,c			;362e	69		i
	ld h,b			;362f	60		`
	ld a,e			;3630	7b		{
	rst 0			;3631	c7		.
	inc bc			;3632	03		.
	bit 1,(iy+013h)		;3633	fd cb 13 4e	. . . N
	jr nz,l363dh		;3637	20 04		  .
	ld (0dd69h),bc		;3639	ed 43 69 dd	. C i .
l363dh:
	pop hl			;363d	e1		.
	jr l3626h		;363e	18 e6		. .
sub_3640h:
	ld hl,(UserSp)		;3640	2a 6d dd	* m .
	rst 28h			;3643	ef		.
	ld e,a			;3644	5f		_
	inc hl			;3645	23		#
	rst 28h			;3646	ef		.
	ld d,a			;3647	57		W
	inc hl			;3648	23		#
	ld (UserSp),hl		;3649	22 6d dd	" m .
	ret			;364c	c9		.
	call sub_3640h		;364d	cd 40 36	. @ 6
	call sub_3389h		;3650	cd 89 33	. . 3
	jr l366eh		;3653	18 19		. .
	ld hl,(0dd69h)		;3655	2a 69 dd	* i .
	call sub_3884h		;3658	cd 84 38	. . 8
	ex de,hl		;365b	eb		.
	ld (0dd69h),de		;365c	ed 53 69 dd	. S i .
	jr l367ch		;3660	18 1a		. .
	ld a,001h		;3662	3e 01		> .
	jr l3668h		;3664	18 02		. .
	ld a,002h		;3666	3e 02		> .
l3668h:
	xor (iy+00bh)		;3668	fd ae 0b	. . .
	ld (iy+00bh),a		;366b	fd 77 0b	. w .
l366eh:
	ld a,(iy+00ah)		;366e	fd 7e 0a	. ~ .
l3671h:
	and 0f1h		;3671	e6 f1		. .
	ld (iy+00ah),a		;3673	fd 77 0a	. w .
l3676h:
	jp l0afbh		;3676	c3 fb 0a	. . .
l3679h:
	call RestorePaging	;3679	cd 4e 3a	. N :
l367ch:
	ld a,(iy+00ah)		;367c	fd 7e 0a	. ~ .
	and 08fh		;367f	e6 8f		. .
	jr l3671h		;3681	18 ee		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CmdInterruptState: the EI/DI command - toggles the saved
; user IFF (DD83 bit 2) that the step trampoline applies as
; its DI/EI prefix.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CmdInterruptState:
	ld a,e			;3683	7b		{
	and 0feh		;3684	e6 fe		. .
	or d			;3686	b2		.
	ld a,017h		;3687	3e 17		> .
	ret nz			;3689	c0		.
	ld hl,UserIff		;368a	21 83 dd	! . .
	ld a,e			;368d	7b		{
	or e			;368e	b3		.
	jr z,l3695h		;368f	28 04		( .
	set 2,(hl)		;3691	cb d6		. .
	jr l3676h		;3693	18 e1		. .
l3695h:
	res 2,(hl)		;3695	cb 96		. .
	jr l3676h		;3697	18 dd		. .
	call sub_3713h		;3699	cd 13 37	. . 7
	set 4,(iy+012h)		;369c	fd cb 12 e6	. . . .
	push af			;36a0	f5		.
	push de			;36a1	d5		.
	ld de,(UserPc)		;36a2	ed 5b 6b dd	. [ k .
	inc de			;36a6	13		.
	call sub_3a14h		;36a7	cd 14 3a	. . :
	ld de,Rst08Vector	;36aa	11 08 00	. . .
	call sub_3a14h		;36ad	cd 14 3a	. . :
	pop de			;36b0	d1		.
	pop af			;36b1	f1		.
	jr l36b7h		;36b2	18 03		. .
	call sub_3713h		;36b4	cd 13 37	. . 7
l36b7h:
	jr c,l36bdh		;36b7	38 04		8 .
	ld (UserPc),de		;36b9	ed 53 6b dd	. S k .
l36bdh:
	call sub_382eh		;36bd	cd 2e 38	. . 8
	jp l013dh		;36c0	c3 3d 01	. = .
	ei			;36c3	fb		.
	halt			;36c4	76		v
	di			;36c5	f3		.
	ld de,(UserPc)		;36c6	ed 5b 6b dd	. [ k .
	call sub_3a14h		;36ca	cd 14 3a	. . :
	ld de,l0ff3h		;36cd	11 f3 0f	. . .
	call sub_3a14h		;36d0	cd 14 3a	. . :
	ld hl,l3d30h		;36d3	21 30 3d	! 0 =
	ld (UserPc),hl		;36d6	22 6b dd	" k .
	jr l36bdh		;36d9	18 e2		. .
	ld a,e			;36db	7b		{
	and 0feh		;36dc	e6 fe		. .
	or d			;36de	b2		.
	ld a,019h		;36df	3e 19		> .
	ret nz			;36e1	c0		.
	ld a,01bh		;36e2	3e 1b		> .
	bit 5,(iy-002h)		;36e4	fd cb fe 6e	. . . n
	ret nz			;36e8	c0		.
	ld hl,l367ch		;36e9	21 7c 36	! | 6
	ex (sp),hl		;36ec	e3		.
	res 4,(iy-002h)		;36ed	fd cb fe a6	. . . .
	bit 0,e			;36f1	cb 43		. C
	ret z			;36f3	c8		.
	set 4,(iy-002h)		;36f4	fd cb fe e6	. . . .
	ret			;36f8	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ScreenBankCheck: is the visible screen the bank being
; edited? (shadow-screen test).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ScreenBankCheck:
	push hl			;36f9	e5		.
	ld hl,(PagingState)	;36fa	2a 12 e0	* . .
	ld a,007h		;36fd	3e 07		> .
	and l			;36ff	a5		.
l3700h:
	bit 4,h			;3700	cb 64		. d
	pop hl			;3702	e1		.
	ret z			;3703	c8		.
	or 008h			;3704	f6 08		. .
	ret			;3706	c9		.
sub_3707h:
	call ScreenBankCheck	;3707	cd f9 36	. . 6
	cp l			;370a	bd		.
	ret			;370b	c9		.
	ld bc,l367ch		;370c	01 7c 36	. | 6
	push bc			;370f	c5		.
	ex de,hl		;3710	eb		.
	jr l371dh		;3711	18 0a		. .
sub_3713h:
	cp 001h			;3713	fe 01		. .
	ret c			;3715	d8		.
	ret z			;3716	c8		.
	cp 002h			;3717	fe 02		. .
l3719h:
	ld a,00bh		;3719	3e 0b		> .
	jr nz,l3763h		;371b	20 46		  F
l371dh:
	ld a,l			;371d	7d		}
	and 0f0h		;371e	e6 f0		. .
	or h			;3720	b4		.
	ld a,018h		;3721	3e 18		> .
	jr nz,l3763h		;3723	20 3e		  >
	ld a,l			;3725	7d		}
	and 007h		;3726	e6 07		. .
	jr z,l3732h		;3728	28 08		( .
	ld a,01ch		;372a	3e 1c		> .
	bit 5,(iy-002h)		;372c	fd cb fe 6e	. . . n
	jr nz,l3763h		;3730	20 31		  1
l3732h:
	ld bc,(PagingState)	;3732	ed 4b 12 e0	. K . .
	res 4,b			;3736	cb a0		. .
	bit 3,l			;3738	cb 5d		. ]
	jr z,l373eh		;373a	28 02		( .
	set 4,b			;373c	cb e0		. .
l373eh:
	res 3,l			;373e	cb 9d		. .
	ld a,c			;3740	79		y
	and 0f8h		;3741	e6 f8		. .
	or l			;3743	b5		.
	ld c,a			;3744	4f		O
	ld (PagingState),bc	;3745	ed 43 12 e0	. C . .
	xor a			;3749	af		.
	ret			;374a	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CheckAddressRange: validate a command address range (carry
; on error, message via ReportError).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CheckAddressRange:
	cp 002h			;374b	fe 02		. .
	jr z,sub_375ch		;374d	28 0d		( .
	jr nc,l3719h		;374f	30 c8		0 .
	ld hl,0ffffh		;3751	21 ff ff	! . .
	cp 001h			;3754	fe 01		. .
	jr z,sub_375ch		;3756	28 04		( .
	ld de,(0dd69h)		;3758	ed 5b 69 dd	. [ i .
sub_375ch:
	or a			;375c	b7		.
	sbc hl,de		;375d	ed 52		. R
	add hl,de		;375f	19		.
	ret nc			;3760	d0		.
	ld a,01dh		;3761	3e 1d		> .
l3763h:
	jp ExitToError		;3763	c3 06 0b	. . .
sub_3766h:
	call SavePagingShadow	;3766	cd e0 35	. . 5
	cp 004h			;3769	fe 04		. .
	call z,sub_37f0h	;376b	cc f0 37	. . 7
	jr z,sub_375ch		;376e	28 ec		( .
	cp 003h			;3770	fe 03		. .
	jr z,sub_375ch		;3772	28 e8		( .
	ld a,00ah		;3774	3e 0a		> .
l3776h:
	jr l3763h		;3776	18 eb		. .
	cp 001h			;3778	fe 01		. .
	jr z,l3787h		;377a	28 0b		( .
	jr nc,l3719h		;377c	30 9b		0 .
	call CallTrdos		;377e	cd 21 3a	. ! :
	call sub_302eh		;3781	cd 2e 30	. . 0
	jp l0af2h		;3784	c3 f2 0a	. . .
l3787h:
	ld a,e			;3787	7b		{
	and 0feh		;3788	e6 fe		. .
	or d			;378a	b2		.
	jr nz,l379ah		;378b	20 0d		  .
	ld a,081h		;378d	3e 81		> .
	ld hl,StepFlags		;378f	21 26 e0	! & .
	set 1,(hl)		;3792	cb ce		. .
	bit 0,e			;3794	cb 43		. C
	ret z			;3796	c8		.
	res 1,(hl)		;3797	cb 8e		. .
	ret			;3799	c9		.
l379ah:
	ld a,d			;379a	7a		z
	or a			;379b	b7		.
	ld a,017h		;379c	3e 17		> .
	ret nz			;379e	c0		.
	ld hl,PagingState	;379f	21 12 e0	! . .
	ld a,e			;37a2	7b		{
	cp 005h			;37a3	fe 05		. .
	jr z,l37aeh		;37a5	28 07		( .
	cp 007h			;37a7	fe 07		. .
	jr z,l37b5h		;37a9	28 0a		( .
	ld a,017h		;37ab	3e 17		> .
	ret			;37ad	c9		.
l37aeh:
	res 3,(hl)		;37ae	cb 9e		. .
l37b0h:
	ld a,081h		;37b0	3e 81		> .
	jp l0afbh		;37b2	c3 fb 0a	. . .
l37b5h:
	set 3,(hl)		;37b5	cb de		. .
	jr l37b0h		;37b7	18 f7		. .
sub_37b9h:
	push hl			;37b9	e5		.
	ld hl,0e02dh		;37ba	21 2d e0	! - .
	bit 3,(hl)		;37bd	cb 5e		. ^
	res 3,(hl)		;37bf	cb 9e		. .
	push af			;37c1	f5		.
	call nz,sub_381ch	;37c2	c4 1c 38	. . 8
	pop af			;37c5	f1		.
	call z,sub_3813h	;37c6	cc 13 38	. . 8
	pop hl			;37c9	e1		.
	ld a,0feh		;37ca	3e fe		> .
	in a,(0feh)		;37cc	db fe		. .
	rrca			;37ce	0f		.
	ret c			;37cf	d8		.
	ld a,0f7h		;37d0	3e f7		> .
	in a,(0feh)		;37d2	db fe		. .
	rrca			;37d4	0f		.
	ret c			;37d5	d8		.
	res 3,(iy+019h)		;37d6	fd cb 19 9e	. . . .
	call sub_382eh		;37da	cd 2e 38	. . 8
l37ddh:
	call sub_093ch		;37dd	cd 3c 09	. < .
	ld a,09ah		;37e0	3e 9a		> .
	bit 0,(iy+012h)		;37e2	fd cb 12 46	. . . F
	jr nz,l3776h		;37e6	20 8e		  .
	jp l0ad7h		;37e8	c3 d7 0a	. . .
sub_37ebh:
	push af			;37eb	f5		.
	push hl			;37ec	e5		.
	push bc			;37ed	c5		.
	jr l37f6h		;37ee	18 06		. .
sub_37f0h:
	push af			;37f0	f5		.
	push hl			;37f1	e5		.
	push bc			;37f2	c5		.
	ld hl,(0dd9fh)		;37f3	2a 9f dd	* . .
l37f6h:
	call l371dh		;37f6	cd 1d 37	. . 7
	ld hl,(PagingState)	;37f9	2a 12 e0	* . .
	ld (PagingBackup),hl	;37fc	22 0e e0	" . .
	call RestorePaging	;37ff	cd 4e 3a	. N :
	pop bc			;3802	c1		.
	pop hl			;3803	e1		.
	pop af			;3804	f1		.
	scf			;3805	37		7
	ret			;3806	c9		.
l3807h:
	ld a,0c0h		;3807	3e c0		> .
l3809h:
	push bc			;3809	c5		.
	ld b,000h		;380a	06 00		. .
l380ch:
	djnz l380ch		;380c	10 fe		. .
	pop bc			;380e	c1		.
	dec a			;380f	3d		=
	jr nz,l3809h		;3810	20 f7		  .
	ret			;3812	c9		.
sub_3813h:
	ld a,07fh		;3813	3e 7f		> .
	in a,(0feh)		;3815	db fe		. .
	rrca			;3817	0f		.
	ret c			;3818	d8		.
	call sub_382eh		;3819	cd 2e 38	. . 8
sub_381ch:
	call l3807h		;381c	cd 07 38	. . 8
	call sub_3023h		;381f	cd 23 30	. # 0
	cp 020h			;3822	fe 20		.  
	jr z,l382bh		;3824	28 05		( .
	and 05fh		;3826	e6 5f		. _
	cp 053h			;3828	fe 53		. S
	ret nz			;382a	c0		.
l382bh:
	set 3,(hl)		;382b	cb de		. .
	ret			;382d	c9		.
sub_382eh:
	xor a			;382e	af		.
	in a,(0feh)		;382f	db fe		. .
	cpl			;3831	2f		/
	and 01fh		;3832	e6 1f		. .
	ret z			;3834	c8		.
	jr sub_382eh		;3835	18 f7		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; DecodeXorTable: XOR-unmask a ROM table into RAM using the
; keys at XorDecodeKeys ($00FC).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
DecodeXorTable:
	ex (sp),hl		;3837	e3		.
	inc hl			;3838	23		#
	inc hl			;3839	23		#
	ld b,(hl)		;383a	46		F
	inc hl			;383b	23		#
	push de			;383c	d5		.
	ld e,(hl)		;383d	5e		^
	inc hl			;383e	23		#
	ld d,000h		;383f	16 00		. .
	ld a,(de)		;3841	1a		.
	pop de			;3842	d1		.
	ex (sp),hl		;3843	e3		.
	ld c,a			;3844	4f		O
	ld a,(l00feh)		;3845	3a fe 00	: . .
	xor c			;3848	a9		.
	ld c,a			;3849	4f		O
	ld a,(l00fdh)		;384a	3a fd 00	: . .
	xor c			;384d	a9		.
	ld c,a			;384e	4f		O
	ld a,(Filler00F1_end)	;384f	3a fc 00	: . .
	xor c			;3852	a9		.
	ld c,a			;3853	4f		O
l3854h:
	ld a,c			;3854	79		y
	xor b			;3855	a8		.
	xor (hl)		;3856	ae		.
	ld (de),a		;3857	12		.
	inc hl			;3858	23		#
	inc de			;3859	13		.
	djnz l3854h		;385a	10 f8		. .
	ret			;385c	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RunCommand: walk the command table, execute the matching
; entry (jp (HL) per entry), wait for a key, return to the
; main loop.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RunCommand:
	scf			;385d	37		7
	jr l3861h		;385e	18 01		. .
	or a			;3860	b7		.
l3861h:
	pop hl			;3861	e1		.
	ld e,(hl)		;3862	5e		^
	inc hl			;3863	23		#
	ld d,(hl)		;3864	56		V
	inc hl			;3865	23		#
	push hl			;3866	e5		.
	ex de,hl		;3867	eb		.
	jr c,l3880h		;3868	38 16		8 .
	ld a,(hl)		;386a	7e		~
	or a			;386b	b7		.
	jr nz,l3883h		;386c	20 15		  .
	ld a,03dh		;386e	3e 3d		> =
	sla a			;3870	cb 27		. '
	call sub_3014h		;3872	cd 14 30	. . 0
	scf			;3875	37		7
	rst 30h			;3876	f7		.
	add a,h			;3877	84		.
	inc e			;3878	1c		.
	dec b			;3879	05		.
	call sub_302eh		;387a	cd 2e 30	. . 0
	jp l0129h		;387d	c3 29 01	. ) .
l3880h:
	ld a,(hl)		;3880	7e		~
	or a			;3881	b7		.
	ret z			;3882	c8		.
l3883h:
	jp (hl)			;3883	e9		.
sub_3884h:
	push hl			;3884	e5		.
	call FetchPrefixBytes	;3885	cd 96 2f	. . /
	pop hl			;3888	e1		.
	bit 6,(iy+001h)		;3889	fd cb 01 76	. . . v
	ret z			;388d	c8		.
	ld hl,(StepPc)		;388e	2a d0 dd	* . .
	ret			;3891	c9		.
	xor a			;3892	af		.
	call sub_38c6h		;3893	cd c6 38	. . 8
	ex de,hl		;3896	eb		.
	call sub_3884h		;3897	cd 84 38	. . 8
	jr l38c3h		;389a	18 27		. '
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; BreakpointSlot: address of breakpoint slot A (DD8D-based
; table).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
BreakpointSlot:
	ld l,a			;389c	6f		o
	ld h,000h		;389d	26 00		& .
	ld de,0dd8dh		;389f	11 8d dd	. . .
	add hl,hl		;38a2	29		)
	add hl,de		;38a3	19		.
	ret			;38a4	c9		.
	ld a,0c3h		;38a5	3e c3		> .
l38a7h:
	rst 30h			;38a7	f7		.
	xor l			;38a8	ad		.
	ld b,006h		;38a9	06 06		. .
	jr c,l38d1h		;38ab	38 24		8 $
	bit 4,(iy+005h)		;38ad	fd cb 05 66	. . . f
	push af			;38b1	f5		.
	call z,sub_0d55h	;38b2	cc 55 0d	. U .
	pop af			;38b5	f1		.
	call nz,EditorSyntaxChars_end	;38b6	c4 f4 0d	. . .
	jr nz,l38a7h		;38b9	20 ec		  .
	push de			;38bb	d5		.
	ld a,00ch		;38bc	3e 0c		> .
	rst 30h			;38be	f7		.
	add a,h			;38bf	84		.
	inc e			;38c0	1c		.
l38c1h:
	dec b			;38c1	05		.
	pop hl			;38c2	e1		.
l38c3h:
	jp l394fh		;38c3	c3 4f 39	. O 9
sub_38c6h:
	call BreakpointSlot	;38c6	cd 9c 38	. . 8
	ld de,(0dd8bh)		;38c9	ed 5b 8b dd	. [ . .
	ld (hl),e		;38cd	73		s
	inc hl			;38ce	23		#
	ld (hl),d		;38cf	72		r
	ret			;38d0	c9		.
l38d1h:
	res 1,(ix+007h)		;38d1	dd cb 07 8e	. . . .
	jp l37ddh		;38d5	c3 dd 37	. . 7
	xor a			;38d8	af		.
	ld hl,(UserPc)		;38d9	2a 6b dd	* k .
	bit 6,(iy+005h)		;38dc	fd cb 05 76	. . . v
	jr nz,l38e5h		;38e0	20 03		  .
	ld hl,(0dd69h)		;38e2	2a 69 dd	* i .
l38e5h:
	or a			;38e5	b7		.
	jr z,l38eeh		;38e6	28 06		( .
	cp 002h			;38e8	fe 02		. .
	ld a,00bh		;38ea	3e 0b		> .
	ret nc			;38ec	d0		.
	ex de,hl		;38ed	eb		.
l38eeh:
	ld (0dd8bh),hl		;38ee	22 8b dd	" . .
	ld a,00ch		;38f1	3e 0c		> .
	rst 30h			;38f3	f7		.
	add a,h			;38f4	84		.
	inc e			;38f5	1c		.
	dec b			;38f6	05		.
l38f7h:
	call InitErrorWindow	;38f7	cd 95 3a	. . :
	ld (ix+000h),000h	;38fa	dd 36 00 00	. 6 . .
	set 1,(ix+007h)		;38fe	dd cb 07 ce	. . . .
	ld hl,(0dd8bh)		;3902	2a 8b dd	* . .
	ld b,016h		;3905	06 16		. .
	jr l390ch		;3907	18 03		. .
l3909h:
	inc (ix+000h)		;3909	dd 34 00	. 4 .
l390ch:
	push bc			;390c	c5		.
	call ArmWatchpoint	;390d	cd a8 39	. . 9
	pop bc			;3910	c1		.
	djnz l3909h		;3911	10 f6		. .
l3913h:
	ld hl,l3913h		;3913	21 13 39	! . 9
	push hl			;3916	e5		.
	rst 30h			;3917	f7		.
	ld c,d			;3918	4a		J
	rlca			;3919	07		.
	ld b,0cdh		;391a	06 cd		. .
	ld a,(hl)		;391c	7e		~
	inc l			;391d	2c		,
	adc a,(hl)		;391e	8e		.
	pop de			;391f	d1		.
	jr c,l3925h		;3920	38 03		8 .
	pop de			;3922	d1		.
	jr c,$-121		;3923	38 85		8 .
l3925h:
	ld d,c			;3925	51		Q
	add hl,sp		;3926	39		9
	add a,e			;3927	83		.
	ld d,(hl)		;3928	56		V
	add hl,sp		;3929	39		9
	add a,(hl)		;392a	86		.
	adc a,a			;392b	8f		.
	add hl,sp		;392c	39		9
	add a,h			;392d	84		.
	ld h,e			;392e	63		c
	add hl,sp		;392f	39		9
	dec c			;3930	0d		.
	and l			;3931	a5		.
	jr c,l38c1h		;3932	38 8d		8 .
	sub d			;3934	92		.
	jr c,l3999h		;3935	38 62		8 b
	dec bc			;3937	0b		.
	inc sp			;3938	33		3
	ld l,(hl)		;3939	6e		n
	ld h,033h		;393a	26 33		& 3
	rst 38h			;393c	ff		.
	sub 030h		;393d	d6 30		. 0
	ret c			;393f	d8		.
	ld c,005h		;3940	0e 05		. .
	cp c			;3942	b9		.
	jr c,sub_38c6h		;3943	38 81		8 .
	sub c			;3945	91		.
	cp c			;3946	b9		.
	ret nc			;3947	d0		.
	call BreakpointSlot	;3948	cd 9c 38	. . 8
	ld e,(hl)		;394b	5e		^
	inc hl			;394c	23		#
	ld d,(hl)		;394d	56		V
	ex de,hl		;394e	eb		.
l394fh:
	jr l395dh		;394f	18 0c		. .
	ld bc,0ffffh		;3951	01 ff ff	. . .
	jr l3959h		;3954	18 03		. .
	ld bc,l0001h		;3956	01 01 00	. . .
l3959h:
	ld hl,(0dd8bh)		;3959	2a 8b dd	* . .
	add hl,bc		;395c	09		.
l395dh:
	ld (0dd8bh),hl		;395d	22 8b dd	" . .
l3960h:
	pop hl			;3960	e1		.
	jr l38f7h		;3961	18 94		. .
	call ScrollDown		;3963	cd 1b 2a	. . *
	ld (ix+000h),000h	;3966	dd 36 00 00	. 6 . .
	ld bc,0fffbh		;396a	01 fb ff	. . .
l396dh:
	inc bc			;396d	03		.
	ld hl,(0dd8bh)		;396e	2a 8b dd	* . .
	add hl,bc		;3971	09		.
	push bc			;3972	c5		.
	call FetchPrefixBytes	;3973	cd 96 2f	. . /
	pop bc			;3976	c1		.
	ld de,(0dd8bh)		;3977	ed 5b 8b dd	. [ . .
	or a			;397b	b7		.
	sbc hl,de		;397c	ed 52		. R
	jr z,l3982h		;397e	28 02		( .
	jr nc,l398bh		;3980	30 09		0 .
l3982h:
	add hl,de		;3982	19		.
	jr nz,l396dh		;3983	20 e8		  .
	add hl,bc		;3985	09		.
	ld (0dd8bh),hl		;3986	22 8b dd	" . .
	jr ArmWatchpoint	;3989	18 1d		. .
l398bh:
	add hl,de		;398b	19		.
	add hl,bc		;398c	09		.
	jr l395dh		;398d	18 ce		. .
	call ScrollUp		;398f	cd 8e 29	. . )
	ld (ix+000h),015h	;3992	dd 36 00 15	. 6 . .
	ld hl,(0dd8bh)		;3996	2a 8b dd	* . .
l3999h:
	call FetchPrefixBytes	;3999	cd 96 2f	. . /
	ld (0dd8bh),hl		;399c	22 8b dd	" . .
	ld b,015h		;399f	06 15		. .
l39a1h:
	push bc			;39a1	c5		.
	call FetchPrefixBytes	;39a2	cd 96 2f	. . /
	pop bc			;39a5	c1		.
	djnz l39a1h		;39a6	10 f9		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ArmWatchpoint: activate a watch entry (address / length /
; value).
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ArmWatchpoint:
	ld a,(ix+006h)		;39a8	dd 7e 06	. ~ .
	push af			;39ab	f5		.
	ld (ix+001h),000h	;39ac	dd 36 01 00	. 6 . .
	push ix			;39b0	dd e5		. .
	call InitWatchTable	;39b2	cd 81 32	. . 2
l39b5h:
	bit 7,(ix+000h)		;39b5	dd cb 00 7e	. . . ~
	jr z,l39ddh		;39b9	28 22		( "
	push hl			;39bb	e5		.
	ld l,(ix+001h)		;39bc	dd 6e 01	. n .
	call sub_3707h		;39bf	cd 07 37	. . 7
	pop hl			;39c2	e1		.
	jr nz,l39ddh		;39c3	20 18		  .
	push de			;39c5	d5		.
	ld e,(ix+002h)		;39c6	dd 5e 02	. ^ .
	ld d,(ix+003h)		;39c9	dd 56 03	. V .
	or a			;39cc	b7		.
	sbc hl,de		;39cd	ed 52		. R
	add hl,de		;39cf	19		.
	pop de			;39d0	d1		.
	jr nz,l39ddh		;39d1	20 0a		  .
	ld a,(0e114h)		;39d3	3a 14 e1	: . .
	ex (sp),ix		;39d6	dd e3		. .
	ld (ix+006h),a		;39d8	dd 77 06	. w .
	ex (sp),ix		;39db	dd e3		. .
l39ddh:
	add ix,de		;39dd	dd 19		. .
	djnz l39b5h		;39df	10 d4		. .
	pop ix			;39e1	dd e1		. .
	call sub_2fach		;39e3	cd ac 2f	. . /
	pop af			;39e6	f1		.
	ld (ix+006h),a		;39e7	dd 77 06	. w .
	ret			;39ea	c9		.
	set 4,(iy+009h)		;39eb	fd cb 09 e6	. . . .
	or a			;39ef	b7		.
	jr z,l3a04h		;39f0	28 12		( .
	cp 004h			;39f2	fe 04		. .
	ld a,00bh		;39f4	3e 0b		> .
	jr nc,$+18		;39f6	30 10		0 .
	ld a,e			;39f8	7b		{
	and 0fch		;39f9	e6 fc		. .
	or d			;39fb	b2		.
	ld a,03eh		;39fc	3e 3e		> >
	jr nz,$+10		;39fe	20 08		  .
	ld a,e			;3a00	7b		{
	call sub_2c4dh		;3a01	cd 4d 2c	. M ,
l3a04h:
	rst 30h			;3a04	f7		.
	ld c,c			;3a05	49		I
	ld (bc),a		;3a06	02		.
	ld b,0cdh		;3a07	06 cd		. .
	inc a			;3a09	3c		<
	add hl,bc		;3a0a	09		.
	ld a,081h		;3a0b	3e 81		> .
	jp l0ae0h		;3a0d	c3 e0 0a	. . .
	ld hl,l367ch		;3a10	21 7c 36	! | 6
	ex (sp),hl		;3a13	e3		.
sub_3a14h:
	ld hl,(UserSp)		;3a14	2a 6d dd	* m .
	dec hl			;3a17	2b		+
	ld a,d			;3a18	7a		z
	rst 0			;3a19	c7		.
	dec hl			;3a1a	2b		+
	ld a,e			;3a1b	7b		{
	rst 0			;3a1c	c7		.
	ld (UserSp),hl		;3a1d	22 6d dd	" m .
	ret			;3a20	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; CallTrdos - enter a TR-DOS session: #1FFD=$02, #7FFD=$17,
; LDIR 6912 bytes $C000 -> $4000 (DOS screen into place),
; then restore #7FFD=$10 / #1FFD=$12 on return.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
CallTrdos:
	bit 3,(iy-002h)		;3a21	fd cb fe 5e	. . . ^
	jp z,sub_0581h		;3a25	ca 81 05	. . .
	ld bc,l1ffdh		;3a28	01 fd 1f	. . .
	ld a,002h		;3a2b	3e 02		> .
	out (c),a		;3a2d	ed 79		. y
	ld b,07fh		;3a2f	06 7f		. .
	ld a,017h		;3a31	3e 17		> .
	out (c),a		;3a33	ed 79		. y
	ld hl,0c000h		;3a35	21 00 c0	! . .
	ld de,EncodedTail_end	;3a38	11 00 40	. . @
	ld bc,01b00h		;3a3b	01 00 1b	. . .
	ldir			;3a3e	ed b0		. .
	ld bc,07ffdh		;3a40	01 fd 7f	. . .
	ld a,010h		;3a43	3e 10		> .
	out (c),a		;3a45	ed 79		. y
	ld b,01fh		;3a47	06 1f		. .
	ld a,012h		;3a49	3e 12		> .
	out (c),a		;3a4b	ed 79		. y
	ret			;3a4d	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; RestorePaging: $E00E shadow -> $E012 paging mirror, ports
; refreshed.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
RestorePaging:
	push hl			;3a4e	e5		.
	ld hl,(0e010h)		;3a4f	2a 10 e0	* . .
	ld (PagingState),hl	;3a52	22 12 e0	" . .
	pop hl			;3a55	e1		.
	ret			;3a56	c9		.
	call CheckAddressRange	;3a57	cd 4b 37	. K 7
	set 4,(iy+009h)		;3a5a	fd cb 09 e6	. . . .
	jr l3a63h		;3a5e	18 03		. .
	call CheckAddressRange	;3a60	cd 4b 37	. K 7
l3a63h:
	ld bc,sub_2fach		;3a63	01 ac 2f	. . /
	jr l3a77h		;3a66	18 0f		. .
	call CheckAddressRange	;3a68	cd 4b 37	. K 7
	set 4,(iy+009h)		;3a6b	fd cb 09 e6	. . . .
	jr l3a74h		;3a6f	18 03		. .
	call CheckAddressRange	;3a71	cd 4b 37	. K 7
l3a74h:
	ld bc,l2ebeh		;3a74	01 be 2e	. . .
l3a77h:
	ex de,hl		;3a77	eb		.
	call InitErrorWindow	;3a78	cd 95 3a	. . :
l3a7bh:
	call sub_37b9h		;3a7b	cd b9 37	. . 7
	rst 20h			;3a7e	e7		.
	adc a,l			;3a7f	8d		.
	push de			;3a80	d5		.
	push bc			;3a81	c5		.
	call sub_3a93h		;3a82	cd 93 3a	. . :
	pop bc			;3a85	c1		.
	pop de			;3a86	d1		.
	or a			;3a87	b7		.
	sbc hl,de		;3a88	ed 52		. R
	add hl,de		;3a8a	19		.
	jr c,l3a7bh		;3a8b	38 ee		8 .
	call sub_093ch		;3a8d	cd 3c 09	. < .
	ld a,081h		;3a90	3e 81		> .
	ret			;3a92	c9		.
sub_3a93h:
	push bc			;3a93	c5		.
	ret			;3a94	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; InitErrorWindow: open the service window stack and install
; the error hook below so RST 08 lands in a visible window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
InitErrorWindow:
	push hl			;3a95	e5		.
	push de			;3a96	d5		.
	push bc			;3a97	c5		.
	bit 4,(iy+009h)		;3a98	fd cb 09 66	. . . f
	ld hl,PromptWindowDef	;3a9c	21 75 e0	! u .
	jr z,l3aa4h		;3a9f	28 03		( .
	ld hl,0e067h		;3aa1	21 67 e0	! g .
l3aa4h:
	call SetWorkspace	;3aa4	cd 1c 30	. . 0
	push af			;3aa7	f5		.
	call z,sub_2a5eh	;3aa8	cc 5e 2a	. ^ *
	ld (iy+00ah),000h	;3aab	fd 36 0a 00	. 6 . .
	pop af			;3aaf	f1		.
	jr nz,l3ab6h		;3ab0	20 04		  .
	ld (ix+000h),015h	;3ab2	dd 36 00 15	. 6 . .
l3ab6h:
	ld hl,ErrorWindowHook	;3ab6	21 c0 3a	! . :
	ld (0de12h),hl		;3ab9	22 12 de	" . .
	pop bc			;3abc	c1		.
	pop de			;3abd	d1		.
	pop hl			;3abe	e1		.
	ret			;3abf	c9		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; ErrorWindowHook: the (DE12)-vectored handler printing
; errors into the popup window.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
ErrorWindowHook:
	call sub_093ch		;3ac0	cd 3c 09	. < .
	ld a,09eh		;3ac3	3e 9e		> .
	jp ExitToError		;3ac5	c3 06 0b	. . .
	ld hl,TokenTable	;3ac8	21 a3 23	! . #
	ld de,l2104h		;3acb	11 04 21	. . !
	jr l3ad8h		;3ace	18 08		. .
	ld hl,l3d01h		;3ad0	21 01 3d	! . =
	ld c,000h		;3ad3	0e 00		. .
	ld de,l3afbh		;3ad5	11 fb 3a	. . :
l3ad8h:
	call PopupTabStops_end	;3ad8	cd bb 33	. . 3
l3adbh:
	ld b,(ix+004h)		;3adb	dd 46 04	. F .
l3adeh:
	push de			;3ade	d5		.
	call sub_3af9h		;3adf	cd f9 3a	. . :
	pop de			;3ae2	d1		.
	ld a,081h		;3ae3	3e 81		> .
	ret nc			;3ae5	d0		.
	djnz l3adeh		;3ae6	10 f6		. .
	push hl			;3ae8	e5		.
	push bc			;3ae9	c5		.
	push de			;3aea	d5		.
	rst 30h			;3aeb	f7		.
	ld c,d			;3aec	4a		J
	rlca			;3aed	07		.
	ld b,0d1h		;3aee	06 d1		. .
	pop bc			;3af0	c1		.
	pop hl			;3af1	e1		.
	cp 003h			;3af2	fe 03		. .
	jr nz,l3adbh		;3af4	20 e5		  .
	ld a,09ah		;3af6	3e 9a		> .
	ret			;3af8	c9		.
sub_3af9h:
	push de			;3af9	d5		.
	ret			;3afa	c9		.
l3afbh:
	ld a,(hl)		;3afb	7e		~
	or a			;3afc	b7		.
	ret z			;3afd	c8		.
	cp 020h			;3afe	fe 20		.  
	jr nc,l3b12h		;3b00	30 10		0 .
	push bc			;3b02	c5		.
	ld c,a			;3b03	4f		O
	dec c			;3b04	0d		.
	rst 20h			;3b05	e7		.
	dec c			;3b06	0d		.
	dec l			;3b07	2d		-
	and b			;3b08	a0		.
	inc hl			;3b09	23		#
	ld b,(hl)		;3b0a	46		F
	inc hl			;3b0b	23		#
	push hl			;3b0c	e5		.
	rst 8			;3b0d	cf		.
	pop hl			;3b0e	e1		.
	pop bc			;3b0f	c1		.
	jr l3b53h		;3b10	18 41		. A
l3b12h:
	push bc			;3b12	c5		.
	rst 20h			;3b13	e7		.
	adc a,l			;3b14	8d		.
	ld b,00ah		;3b15	06 0a		. .
	call PrintJustifiedHeader	;3b17	cd dd 15	. . .
	pop bc			;3b1a	c1		.
	push bc			;3b1b	c5		.
	push hl			;3b1c	e5		.
	ld hl,l3e2ch		;3b1d	21 2c 3e	! , >
	ld a,c			;3b20	79		y
	add a,a			;3b21	87		.
	add a,c			;3b22	81		.
	ld c,a			;3b23	4f		O
	inc c			;3b24	0c		.
	inc c			;3b25	0c		.
	ld b,000h		;3b26	06 00		. .
	add hl,bc		;3b28	09		.
	ld a,(hl)		;3b29	7e		~
	bit 7,a			;3b2a	cb 7f		. .
	jr z,l3b30h		;3b2c	28 02		( .
	jr l3b34h		;3b2e	18 04		. .
l3b30h:
	bit 5,a			;3b30	cb 6f		. o
	jr z,l3b3ch		;3b32	28 08		( .
l3b34h:
	rst 20h			;3b34	e7		.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; Msg3B35: short string comp. - head fragment of
; the message pool in front of the encoded tail.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'Msg3B35' (start 0x3b35 end 0x3b3a)
Msg3B35_start:
	defb 063h		;3b35	63		c
	defb 06fh		;3b36	6f		o
	defb 06dh		;3b37	6d		m
	defb 070h		;3b38	70		p
	defb 0aeh		;3b39	ae		.
Msg3B35_end:
	jr $+22			;3b3a	18 14		. .
l3b3ch:
	bit 6,a			;3b3c	cb 77		. w
	push af			;3b3e	f5		.
	jr nz,l3b46h		;3b3f	20 05		  .
	rst 20h			;3b41	e7		.
	jr nz,$-94		;3b42	20 a0		  .
	jr l3b49h		;3b44	18 03		. .
l3b46h:
	rst 20h			;3b46	e7		.
	inc a			;3b47	3c		<
	cp l			;3b48	bd		.
l3b49h:
	pop af			;3b49	f1		.
	and 01fh		;3b4a	e6 1f		. .
	rst 30h			;3b4c	f7		.
	and c			;3b4d	a1		.
	inc bc			;3b4e	03		.
	ld b,0e1h		;3b4f	06 e1		. .
	pop bc			;3b51	c1		.
	inc c			;3b52	0c		.
l3b53h:
	scf			;3b53	37		7
	ret			;3b54	c9		.
	ld a,e			;3b55	7b		{
	and 0fch		;3b56	e6 fc		. .
	or d			;3b58	b2		.
	ld a,017h		;3b59	3e 17		> .
	ret nz			;3b5b	c0		.
	ld a,081h		;3b5c	3e 81		> .
	ld hl,WriteAnyBankByte	;3b5e	21 00 00	! . .
	add hl,de		;3b61	19		.
	add hl,de		;3b62	19		.
	add hl,de		;3b63	19		.
	ld de,l3b69h		;3b64	11 69 3b	. i ;
	add hl,de		;3b67	19		.
	jp (hl)			;3b68	e9		.
l3b69h:
	im 0			;3b69	ed 46		. F
	ret			;3b6b	c9		.
	im 1			;3b6c	ed 56		. V
	ret			;3b6e	c9		.
	im 2			;3b6f	ed 5e		. ^
	ret			;3b71	c9		.
	ld a,017h		;3b72	3e 17		> .
	ret			;3b74	c9		.
sub_3b75h:
	xor a			;3b75	af		.
	ld d,a			;3b76	57		W
	ld b,001h		;3b77	06 01		. .
	ei			;3b79	fb		.
	halt			;3b7a	76		v
	di			;3b7b	f3		.
	ld a,d			;3b7c	7a		z
	or a			;3b7d	b7		.
	ret			;3b7e	c9		.
sub_3b7fh:
	ld hl,l3edah		;3b7f	21 da 3e	! . >
	ld b,003h		;3b82	06 03		. .
l3b84h:
	ld e,(hl)		;3b84	5e		^
	inc hl			;3b85	23		#
	ld d,(hl)		;3b86	56		V
	inc hl			;3b87	23		#
	ld a,(hl)		;3b88	7e		~
	ld (de),a		;3b89	12		.
	inc hl			;3b8a	23		#
	inc de			;3b8b	13		.
	ld a,(hl)		;3b8c	7e		~
	ld (de),a		;3b8d	12		.
	inc hl			;3b8e	23		#
	djnz l3b84h		;3b8f	10 f3		. .
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-
; EncodedTail: XOR-encoded tail region. The boot
; decode (DecodeTables, $06B1) unmask $3B92 into
; DecodedTables ($E3BE) and $3ED2 into $EAED;
; the ROM copy itself stays masked, so these
; bytes never execute as code.
;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-;-

; BLOCK 'EncodedTail' (start 0x3b91 end 0x4000)
EncodedTail_start:
	defb 0c9h		;3b91	c9		.
l3b92h:
	defb 0ceh		;3b92	ce		.
	defb 038h		;3b93	38		8
	defb 03bh		;3b94	3b		;
	defb 03ah		;3b95	3a		:
	defb 0f4h		;3b96	f4		.
	defb 075h		;3b97	75		u
	defb 0dbh		;3b98	db		.
	defb 073h		;3b99	73		s
	defb 0d5h		;3b9a	d5		.
	defb 0f5h		;3b9b	f5		.
	defb 0d7h		;3b9c	d7		.
	defb 029h		;3b9d	29		)
	defb 0d1h		;3b9e	d1		.
	defb 0adh		;3b9f	ad		.
	defb 0d3h		;3ba0	d3		.
	defb 0bfh		;3ba1	bf		.
	defb 0edh		;3ba2	ed		.
	defb 08bh		;3ba3	8b		.
	defb 0efh		;3ba4	ef		.
	defb 08eh		;3ba5	8e		.
	defb 0e9h		;3ba6	e9		.
	defb 0e9h		;3ba7	e9		.
	defb 025h		;3ba8	25		%
	defb 00fh		;3ba9	0f		.
	defb 000h		;3baa	00		.
	defb 0e3h		;3bab	e3		.
	defb 0e6h		;3bac	e6		.
	defb 023h		;3bad	23		#
	defb 003h		;3bae	03		.
	defb 004h		;3baf	04		.
	defb 0f2h		;3bb0	f2		.
	defb 03fh		;3bb1	3f		?
	defb 0cch		;3bb2	cc		.
	defb 0ddh		;3bb3	dd		.
	defb 045h		;3bb4	45		E
	defb 039h		;3bb5	39		9
	defb 04bh		;3bb6	4b		K
	defb 0f7h		;3bb7	f7		.
	defb 051h		;3bb8	51		Q
	defb 03dh		;3bb9	3d		=
	defb 057h		;3bba	57		W
	defb 033h		;3bbb	33		3
	defb 06dh		;3bbc	6d		m
	defb 0dfh		;3bbd	df		.
	defb 06dh		;3bbe	6d		m
	defb 0f0h		;3bbf	f0		.
	defb 034h		;3bc0	34		4
	defb 0fdh		;3bc1	fd		.
	defb 01bh		;3bc2	1b		.
	defb 043h		;3bc3	43		C
	defb 018h		;3bc4	18		.
	defb 087h		;3bc5	87		.
	defb 02eh		;3bc6	2e		.
	defb 064h		;3bc7	64		d
	defb 08bh		;3bc8	8b		.
	defb 0a3h		;3bc9	a3		.
	defb 0cbh		;3bca	cb		.
	defb 084h		;3bcb	84		.
	defb 042h		;3bcc	42		B
	defb 0a1h		;3bcd	a1		.
	defb 086h		;3bce	86		.
	defb 0a9h		;3bcf	a9		.
	defb 0dfh		;3bd0	df		.
	defb 082h		;3bd1	82		.
	defb 05bh		;3bd2	5b		[
	defb 07eh		;3bd3	7e		~
	defb 09fh		;3bd4	9f		.
	defb 0bbh		;3bd5	bb		.
	defb 0ach		;3bd6	ac		.
	defb 08dh		;3bd7	8d		.
	defb 09eh		;3bd8	9e		.
	defb 0b3h		;3bd9	b3		.
	defb 071h		;3bda	71		q
	defb 070h		;3bdb	70		p
	defb 073h		;3bdc	73		s
	defb 072h		;3bdd	72		r
	defb 096h		;3bde	96		.
	defb 081h		;3bdf	81		.
	defb 0b2h		;3be0	b2		.
	defb 00bh		;3be1	0b		.
	defb 084h		;3be2	84		.
	defb 0e2h		;3be3	e2		.
	defb 0afh		;3be4	af		.
	defb 06bh		;3be5	6b		k
	defb 086h		;3be6	86		.
	defb 0afh		;3be7	af		.
	defb 082h		;3be8	82		.
	defb 0f6h		;3be9	f6		.
	defb 0a5h		;3bea	a5		.
	defb 081h		;3beb	81		.
	defb 0b2h		;3bec	b2		.
	defb 0a1h		;3bed	a1		.
	defb 08ch		;3bee	8c		.
	defb 0a7h		;3bef	a7		.
	defb 092h		;3bf0	92		.
	defb 018h		;3bf1	18		.
	defb 07ah		;3bf2	7a		z
	defb 00eh		;3bf3	0e		.
	defb 078h		;3bf4	78		x
	defb 0b7h		;3bf5	b7		.
	defb 00eh		;3bf6	0e		.
	defb 061h		;3bf7	61		a
	defb 0bbh		;3bf8	bb		.
	defb 08fh		;3bf9	8f		.
	defb 0b2h		;3bfa	b2		.
	defb 0b5h		;3bfb	b5		.
	defb 079h		;3bfc	79		y
	defb 06fh		;3bfd	6f		o
	defb 0b1h		;3bfe	b1		.
	defb 0b7h		;3bff	b7		.
l3c00h:
	defb 0a2h		;3c00	a2		.
	defb 03eh		;3c01	3e		>
	defb 010h		;3c02	10		.
	defb 032h		;3c03	32		2
	defb 069h		;3c04	69		i
	defb 0e4h		;3c05	e4		.
	defb 0f1h		;3c06	f1		.
	defb 0e3h		;3c07	e3		.
	defb 022h		;3c08	22		"
	defb 03dh		;3c09	3d		=
	defb 0e4h		;3c0a	e4		.
	defb 021h		;3c0b	21		!
	defb 078h		;3c0c	78		x
	defb 0e4h		;3c0d	e4		.
	defb 0e3h		;3c0e	e3		.
	defb 0e5h		;3c0f	e5		.
	defb 021h		;3c10	21		!
	defb 001h		;3c11	01		.
	defb 032h		;3c12	32		2
	defb 0e3h		;3c13	e3		.
	defb 0edh		;3c14	ed		.
	defb 043h		;3c15	43		C
	defb 03dh		;3c16	3d		=
	defb 0e4h		;3c17	e4		.
	defb 001h		;3c18	01		.
	defb 030h		;3c19	30		0
	defb 03dh		;3c1a	3d		=
	defb 018h		;3c1b	18		.
	defb 01ah		;3c1c	1a		.
	defb 0f5h		;3c1d	f5		.
	defb 0afh		;3c1e	af		.
	defb 018h		;3c1f	18		.
	defb 003h		;3c20	03		.
	defb 0f5h		;3c21	f5		.
	defb 03eh		;3c22	3e		>
	defb 010h		;3c23	10		.
	defb 032h		;3c24	32		2
	defb 069h		;3c25	69		i
	defb 0e4h		;3c26	e4		.
	defb 0f1h		;3c27	f1		.
	defb 0edh		;3c28	ed		.
	defb 043h		;3c29	43		C
	defb 03dh		;3c2a	3d		=
	defb 0e4h		;3c2b	e4		.
	defb 0e3h		;3c2c	e3		.
	defb 04eh		;3c2d	4e		N
	defb 023h		;3c2e	23		#
	defb 046h		;3c2f	46		F
	defb 023h		;3c30	23		#
	defb 0e3h		;3c31	e3		.
	defb 0e5h		;3c32	e5		.
	defb 021h		;3c33	21		!
	defb 078h		;3c34	78		x
	defb 0e4h		;3c35	e4		.
	defb 0e3h		;3c36	e3		.
	defb 0c5h		;3c37	c5		.
	defb 0f5h		;3c38	f5		.
	defb 001h		;3c39	01		.
	defb 0fdh		;3c3a	fd		.
	defb 07fh		;3c3b	7f		.
	defb 03eh		;3c3c	3e		>
	defb 000h		;3c3d	00		.
	defb 0edh		;3c3e	ed		.
	defb 079h		;3c3f	79		y
	defb 03eh		;3c40	3e		>
	defb 010h		;3c41	10		.
	defb 006h		;3c42	06		.
	defb 01fh		;3c43	1f		.
	defb 0edh		;3c44	ed		.
	defb 079h		;3c45	79		y
	defb 0f1h		;3c46	f1		.
	defb 0edh		;3c47	ed		.
	defb 04bh		;3c48	4b		K
	defb 03dh		;3c49	3d		=
	defb 0e4h		;3c4a	e4		.
	defb 0c9h		;3c4b	c9		.
	defb 0c5h		;3c4c	c5		.
	defb 0f5h		;3c4d	f5		.
	defb 001h		;3c4e	01		.
	defb 0fdh		;3c4f	fd		.
	defb 01fh		;3c50	1f		.
	defb 03eh		;3c51	3e		>
	defb 012h		;3c52	12		.
	defb 0edh		;3c53	ed		.
	defb 079h		;3c54	79		y
	defb 0f1h		;3c55	f1		.
	defb 0c1h		;3c56	c1		.
	defb 0c9h		;3c57	c9		.
	defb 0e5h		;3c58	e5		.
	defb 021h		;3c59	21		!
	defb 030h		;3c5a	30		0
	defb 03dh		;3c5b	3d		=
	defb 0e3h		;3c5c	e3		.
	defb 0c5h		;3c5d	c5		.
	defb 0f5h		;3c5e	f5		.
	defb 001h		;3c5f	01		.
	defb 0fdh		;3c60	fd		.
	defb 01fh		;3c61	1f		.
	defb 03eh		;3c62	3e		>
	defb 010h		;3c63	10		.
	defb 0edh		;3c64	ed		.
	defb 079h		;3c65	79		y
	defb 006h		;3c66	06		.
	defb 07fh		;3c67	7f		.
	defb 0edh		;3c68	ed		.
	defb 079h		;3c69	79		y
	defb 0f1h		;3c6a	f1		.
	defb 0c1h		;3c6b	c1		.
	defb 0c9h		;3c6c	c9		.
	defb 0c5h		;3c6d	c5		.
	defb 0f5h		;3c6e	f5		.
	defb 001h		;3c6f	01		.
	defb 0fdh		;3c70	fd		.
	defb 01fh		;3c71	1f		.
	defb 03eh		;3c72	3e		>
	defb 010h		;3c73	10		.
	defb 0edh		;3c74	ed		.
	defb 079h		;3c75	79		y
	defb 0afh		;3c76	af		.
	defb 006h		;3c77	06		.
	defb 07fh		;3c78	7f		.
	defb 0edh		;3c79	ed		.
	defb 079h		;3c7a	79		y
	defb 0f1h		;3c7b	f1		.
	defb 0c1h		;3c7c	c1		.
	defb 0c9h		;3c7d	c9		.
	defb 04fh		;3c7e	4f		O
	defb 021h		;3c7f	21		!
	defb 010h		;3c80	10		.
	defb 001h		;3c81	01		.
	defb 00fh		;3c82	0f		.
	defb 00fh		;3c83	0f		.
	defb 0e6h		;3c84	e6		.
	defb 003h		;3c85	03		.
	defb 085h		;3c86	85		.
	defb 06fh		;3c87	6f		o
	defb 06eh		;3c88	6e		n
	defb 06eh		;3c89	6e		n
	defb 079h		;3c8a	79		y
	defb 0c9h		;3c8b	c9		.
	defb 0e6h		;3c8c	e6		.
	defb 003h		;3c8d	03		.
	defb 017h		;3c8e	17		.
	defb 085h		;3c8f	85		.
	defb 06fh		;3c90	6f		o
	defb 030h		;3c91	30		0
	defb 001h		;3c92	01		.
	defb 024h		;3c93	24		$
	defb 07eh		;3c94	7e		~
	defb 023h		;3c95	23		#
	defb 066h		;3c96	66		f
	defb 06fh		;3c97	6f		o
	defb 0c9h		;3c98	c9		.
	defb 0e3h		;3c99	e3		.
	defb 023h		;3c9a	23		#
	defb 023h		;3c9b	23		#
	defb 0e3h		;3c9c	e3		.
	defb 0e5h		;3c9d	e5		.
	defb 02ah		;3c9e	2a		*
	defb 008h		;3c9f	08		.
	defb 0e4h		;3ca0	e4		.
	defb 0e3h		;3ca1	e3		.
	defb 0c9h		;3ca2	c9		.
	defb 07bh		;3ca3	7b		{
	defb 0e6h		;3ca4	e6		.
	defb 010h		;3ca5	10		.
	defb 0edh		;3ca6	ed		.
	defb 079h		;3ca7	79		y
	defb 006h		;3ca8	06		.
	defb 01fh		;3ca9	1f		.
	defb 07ah		;3caa	7a		z
	defb 0e6h		;3cab	e6		.
	defb 003h		;3cac	03		.
	defb 0f6h		;3cad	f6		.
	defb 010h		;3cae	10		.
	defb 0edh		;3caf	ed		.
	defb 079h		;3cb0	79		y
	defb 008h		;3cb1	08		.
	defb 038h		;3cb2	38		8
	defb 001h		;3cb3	01		.
	defb 07eh		;3cb4	7e		~
	defb 077h		;3cb5	77		w
	defb 008h		;3cb6	08		.
	defb 03eh		;3cb7	3e		>
	defb 012h		;3cb8	12		.
	defb 0edh		;3cb9	ed		.
	defb 079h		;3cba	79		y
	defb 006h		;3cbb	06		.
	defb 07fh		;3cbc	7f		.
	defb 03eh		;3cbd	3e		>
	defb 010h		;3cbe	10		.
	defb 0edh		;3cbf	ed		.
	defb 079h		;3cc0	79		y
	defb 0c3h		;3cc1	c3		.
	defb 0a1h		;3cc2	a1		.
	defb 005h		;3cc3	05		.
	defb 07bh		;3cc4	7b		{
	defb 0e6h		;3cc5	e6		.
	defb 010h		;3cc6	10		.
	defb 0edh		;3cc7	ed		.
	defb 079h		;3cc8	79		y
	defb 006h		;3cc9	06		.
	defb 01fh		;3cca	1f		.
	defb 07ah		;3ccb	7a		z
	defb 0e6h		;3ccc	e6		.
	defb 003h		;3ccd	03		.
	defb 0f6h		;3cce	f6		.
	defb 010h		;3ccf	10		.
	defb 0edh		;3cd0	ed		.
	defb 079h		;3cd1	79		y
	defb 0d9h		;3cd2	d9		.
	defb 008h		;3cd3	08		.
	defb 030h		;3cd4	30		0
	defb 001h		;3cd5	01		.
	defb 0ebh		;3cd6	eb		.
	defb 0edh		;3cd7	ed		.
	defb 0b0h		;3cd8	b0		.
	defb 030h		;3cd9	30		0
	defb 001h		;3cda	01		.
	defb 0ebh		;3cdb	eb		.
	defb 008h		;3cdc	08		.
	defb 0d9h		;3cdd	d9		.
	defb 03eh		;3cde	3e		>
	defb 012h		;3cdf	12		.
	defb 0edh		;3ce0	ed		.
	defb 079h		;3ce1	79		y
	defb 006h		;3ce2	06		.
	defb 07fh		;3ce3	7f		.
	defb 03eh		;3ce4	3e		>
	defb 010h		;3ce5	10		.
	defb 0edh		;3ce6	ed		.
	defb 079h		;3ce7	79		y
	defb 0d9h		;3ce8	d9		.
	defb 0c9h		;3ce9	c9		.
	defb 000h		;3cea	00		.
	defb 033h		;3ceb	33		3
	defb 033h		;3cec	33		3
	defb 0fbh		;3ced	fb		.
	defb 03ch		;3cee	3c		<
	defb 032h		;3cef	32		2
	defb 00dh		;3cf0	0d		.
	defb 0e0h		;3cf1	e0		.
	defb 03eh		;3cf2	3e		>
	defb 0c9h		;3cf3	c9		.
	defb 032h		;3cf4	32		2
	defb 018h		;3cf5	18		.
	defb 0e5h		;3cf6	e5		.
	defb 03eh		;3cf7	3e		>
	defb 0fbh		;3cf8	fb		.
	defb 032h		;3cf9	32		2
	defb 017h		;3cfa	17		.
	defb 0e5h		;3cfb	e5		.
	defb 0edh		;3cfc	ed		.
	defb 056h		;3cfd	56		V
	defb 0c3h		;3cfe	c3		.
	defb 05eh		;3cff	5e		^
l3d00h:
	defb 002h		;3d00	02		.
l3d01h:
	defb 001h		;3d01	01		.
	defb 04ch		;3d02	4c		L
	defb 041h		;3d03	41		A
	defb 04eh		;3d04	4e		N
	defb 061h		;3d05	61		a
	defb 06ch		;3d06	6c		l
	defb 079h		;3d07	79		y
	defb 073h		;3d08	73		s
	defb 065h		;3d09	65		e
	defb 0f2h		;3d0a	f2		.
	defb 043h		;3d0b	43		C
	defb 04ch		;3d0c	4c		L
	defb 065h		;3d0d	65		e
	defb 061h		;3d0e	61		a
	defb 0f2h		;3d0f	f2		.
	defb 044h		;3d10	44		D
	defb 045h		;3d11	45		E
	defb 046h		;3d12	46		F
	defb 04ch		;3d13	4c		L
	defb 06fh		;3d14	6f		o
	defb 061h		;3d15	61		a
	defb 0e4h		;3d16	e4		.
	defb 044h		;3d17	44		D
	defb 045h		;3d18	45		E
	defb 046h		;3d19	46		F
	defb 053h		;3d1a	53		S
	defb 061h		;3d1b	61		a
	defb 076h		;3d1c	76		v
	defb 0e5h		;3d1d	e5		.
	defb 045h		;3d1e	45		E
	defb 044h		;3d1f	44		D
	defb 069h		;3d20	69		i
	defb 0f4h		;3d21	f4		.
	defb 045h		;3d22	45		E
	defb 056h		;3d23	56		V
	defb 061h		;3d24	61		a
	defb 0ech		;3d25	ec		.
	defb 04ch		;3d26	4c		L
	defb 044h		;3d27	44		D
	defb 045h		;3d28	45		E
	defb 046h		;3d29	46		F
	defb 069h		;3d2a	69		i
	defb 06eh		;3d2b	6e		n
	defb 069h		;3d2c	69		i
	defb 074h		;3d2d	74		t
	defb 069h		;3d2e	69		i
	defb 06fh		;3d2f	6f		o
l3d30h:
	defb 0eeh		;3d30	ee		.
	defb 050h		;3d31	50		P
	defb 044h		;3d32	44		D
	defb 045h		;3d33	45		E
	defb 046h		;3d34	46		F
	defb 069h		;3d35	69		i
	defb 06eh		;3d36	6e		n
	defb 069h		;3d37	69		i
	defb 074h		;3d38	74		t
	defb 069h		;3d39	69		i
	defb 06fh		;3d3a	6f		o
	defb 0eeh		;3d3b	ee		.
	defb 056h		;3d3c	56		V
	defb 04ch		;3d3d	4c		L
	defb 069h		;3d3e	69		i
	defb 073h		;3d3f	73		s
	defb 0f4h		;3d40	f4		.
	defb 057h		;3d41	57		W
	defb 04fh		;3d42	4f		O
	defb 052h		;3d43	52		R
	defb 0c4h		;3d44	c4		.
	defb 001h		;3d45	01		.
	defb 04dh		;3d46	4d		M
	defb 042h		;3d47	42		B
	defb 052h		;3d48	52		R
	defb 045h		;3d49	45		E
	defb 061h		;3d4a	61		a
	defb 0ebh		;3d4b	eb		.
	defb 042h		;3d4c	42		B
	defb 052h		;3d4d	52		R
	defb 0cbh		;3d4e	cb		.
	defb 044h		;3d4f	44		D
	defb 045h		;3d50	45		E
	defb 04ch		;3d51	4c		L
	defb 042h		;3d52	42		B
	defb 072h		;3d53	72		r
	defb 065h		;3d54	65		e
	defb 061h		;3d55	61		a
	defb 0ebh		;3d56	eb		.
	defb 04ch		;3d57	4c		L
	defb 042h		;3d58	42		B
	defb 072h		;3d59	72		r
	defb 065h		;3d5a	65		e
	defb 061h		;3d5b	61		a
	defb 0ebh		;3d5c	eb		.
	defb 04ch		;3d5d	4c		L
	defb 049h		;3d5e	49		I
	defb 053h		;3d5f	53		S
	defb 054h		;3d60	54		T
	defb 042h		;3d61	42		B
	defb 072h		;3d62	72		r
	defb 065h		;3d63	65		e
	defb 061h		;3d64	61		a
	defb 0ebh		;3d65	eb		.
	defb 001h		;3d66	01		.
	defb 04eh		;3d67	4e		N
	defb 04ah		;3d68	4a		J
	defb 075h		;3d69	75		u
	defb 06dh		;3d6a	6d		m
	defb 0f0h		;3d6b	f0		.
	defb 043h		;3d6c	43		C
	defb 041h		;3d6d	41		A
	defb 04ch		;3d6e	4c		L
	defb 0ech		;3d6f	ec		.
	defb 053h		;3d70	53		S
	defb 04ch		;3d71	4c		L
	defb 04fh		;3d72	4f		O
	defb 0d7h		;3d73	d7		.
	defb 057h		;3d74	57		W
	defb 04fh		;3d75	4f		O
	defb 052h		;3d76	52		R
	defb 04bh		;3d77	4b		K
	defb 073h		;3d78	73		s
	defb 070h		;3d79	70		p
	defb 061h		;3d7a	61		a
	defb 063h		;3d7b	63		c
	defb 0e5h		;3d7c	e5		.
	defb 001h		;3d7d	01		.
	defb 04fh		;3d7e	4f		O
	defb 042h		;3d7f	42		B
	defb 041h		;3d80	41		A
	defb 053h		;3d81	53		S
	defb 0c5h		;3d82	c5		.
	defb 045h		;3d83	45		E
	defb 058h		;3d84	58		X
	defb 0d8h		;3d85	d8		.
	defb 045h		;3d86	45		E
	defb 058h		;3d87	58		X
	defb 020h		;3d88	20		 
	defb 041h		;3d89	41		A
	defb 0c6h		;3d8a	c6		.
	defb 045h		;3d8b	45		E
	defb 058h		;3d8c	58		X
	defb 069h		;3d8d	69		i
	defb 0f4h		;3d8e	f4		.
	defb 04fh		;3d8f	4f		O
	defb 050h		;3d90	50		P
	defb 054h		;3d91	54		T
	defb 069h		;3d92	69		i
	defb 06fh		;3d93	6f		o
	defb 0eeh		;3d94	ee		.
	defb 053h		;3d95	53		S
	defb 048h		;3d96	48		H
	defb 06fh		;3d97	6f		o
	defb 0f7h		;3d98	f7		.
	defb 001h		;3d99	01		.
	defb 051h		;3d9a	51		Q
	defb 02eh		;3d9b	2e		.
	defb 050h		;3d9c	50		P
	defb 0c3h		;3d9d	c3		.
	defb 0aeh		;3d9e	ae		.
	defb 043h		;3d9f	43		C
	defb 048h		;3da0	48		H
	defb 065h		;3da1	65		e
	defb 063h		;3da2	63		c
	defb 0ebh		;3da3	eb		.
	defb 044h		;3da4	44		D
	defb 041h		;3da5	41		A
	defb 053h		;3da6	53		S
	defb 0edh		;3da7	ed		.
	defb 044h		;3da8	44		D
	defb 041h		;3da9	41		A
	defb 054h		;3daa	54		T
	defb 0c1h		;3dab	c1		.
	defb 044h		;3dac	44		D
	defb 049h		;3dad	49		I
	defb 053h		;3dae	53		S
	defb 0f3h		;3daf	f3		.
	defb 044h		;3db0	44		D
	defb 055h		;3db1	55		U
	defb 06dh		;3db2	6d		m
	defb 0f0h		;3db3	f0		.
	defb 046h		;3db4	46		F
	defb 049h		;3db5	49		I
	defb 04ch		;3db6	4c		L
	defb 0ech		;3db7	ec		.
	defb 046h		;3db8	46		F
	defb 049h		;3db9	49		I
	defb 04eh		;3dba	4e		N
	defb 0e4h		;3dbb	e4		.
	defb 04ch		;3dbc	4c		L
	defb 044h		;3dbd	44		D
	defb 049h		;3dbe	49		I
	defb 053h		;3dbf	53		S
	defb 0f3h		;3dc0	f3		.
	defb 04ch		;3dc1	4c		L
	defb 044h		;3dc2	44		D
	defb 055h		;3dc3	55		U
	defb 06dh		;3dc4	6d		m
	defb 0f0h		;3dc5	f0		.
	defb 04dh		;3dc6	4d		M
	defb 045h		;3dc7	45		E
	defb 04dh		;3dc8	4d		M
	defb 06fh		;3dc9	6f		o
	defb 072h		;3dca	72		r
	defb 0f9h		;3dcb	f9		.
	defb 04dh		;3dcc	4d		M
	defb 04fh		;3dcd	4f		O
	defb 056h		;3dce	56		V
	defb 0e5h		;3dcf	e5		.
	defb 050h		;3dd0	50		P
	defb 055h		;3dd1	55		U
	defb 053h		;3dd2	53		S
	defb 0c8h		;3dd3	c8		.
	defb 050h		;3dd4	50		P
	defb 04fh		;3dd5	4f		O
	defb 0d0h		;3dd6	d0		.
	defb 052h		;3dd7	52		R
	defb 041h		;3dd8	41		A
	defb 0cdh		;3dd9	cd		.
	defb 052h		;3dda	52		R
	defb 04fh		;3ddb	4f		O
	defb 0cdh		;3ddc	cd		.
	defb 053h		;3ddd	53		S
	defb 045h		;3dde	45		E
	defb 061h		;3ddf	61		a
	defb 072h		;3de0	72		r
	defb 063h		;3de1	63		c
	defb 0e8h		;3de2	e8		.
	defb 001h		;3de3	01		.
	defb 052h		;3de4	52		R
	defb 043h		;3de5	43		C
	defb 041h		;3de6	41		A
	defb 054h		;3de7	54		T
	defb 061h		;3de8	61		a
	defb 06ch		;3de9	6c		l
	defb 06fh		;3dea	6f		o
	defb 067h		;3deb	67		g
	defb 075h		;3dec	75		u
	defb 0e5h		;3ded	e5		.
	defb 045h		;3dee	45		E
	defb 052h		;3def	52		R
	defb 041h		;3df0	41		A
	defb 073h		;3df1	73		s
	defb 0e5h		;3df2	e5		.
	defb 04ch		;3df3	4c		L
	defb 043h		;3df4	43		C
	defb 041h		;3df5	41		A
	defb 054h		;3df6	54		T
	defb 061h		;3df7	61		a
	defb 06ch		;3df8	6c		l
	defb 06fh		;3df9	6f		o
	defb 067h		;3dfa	67		g
	defb 075h		;3dfb	75		u
	defb 0e5h		;3dfc	e5		.
	defb 04ch		;3dfd	4c		L
	defb 04fh		;3dfe	4f		O
	defb 041h		;3dff	41		A
l3e00h:
	defb 0c4h		;3e00	c4		.
	defb 053h		;3e01	53		S
	defb 041h		;3e02	41		A
	defb 056h		;3e03	56		V
	defb 0c5h		;3e04	c5		.
	defb 001h		;3e05	01		.
	defb 053h		;3e06	53		S
	defb 049h		;3e07	49		I
	defb 0cdh		;3e08	cd		.
	defb 049h		;3e09	49		I
	defb 04eh		;3e0a	4e		N
	defb 054h		;3e0b	54		T
	defb 065h		;3e0c	65		e
	defb 072h		;3e0d	72		r
	defb 072h		;3e0e	72		r
	defb 075h		;3e0f	75		u
	defb 070h		;3e10	70		p
	defb 0f4h		;3e11	f4		.
	defb 049h		;3e12	49		I
	defb 04eh		;3e13	4e		N
	defb 0c4h		;3e14	c4		.
	defb 04fh		;3e15	4f		O
	defb 055h		;3e16	55		U
	defb 054h		;3e17	54		T
	defb 0c4h		;3e18	c4		.
	defb 049h		;3e19	49		I
	defb 0ceh		;3e1a	ce		.
	defb 04fh		;3e1b	4f		O
	defb 055h		;3e1c	55		U
	defb 0d4h		;3e1d	d4		.
	defb 053h		;3e1e	53		S
	defb 043h		;3e1f	43		C
	defb 052h		;3e20	52		R
	defb 065h		;3e21	65		e
	defb 065h		;3e22	65		e
	defb 0eeh		;3e23	ee		.
	defb 000h		;3e24	00		.
	defb 006h		;3e25	06		.
	defb 00eh		;3e26	0e		.
	defb 0cdh		;3e27	cd		.
	defb 0b6h		;3e28	b6		.
	defb 003h		;3e29	03		.
	defb 007h		;3e2a	07		.
	defb 0c9h		;3e2b	c9		.
l3e2ch:
	defb 042h		;3e2c	42		B
	defb 01fh		;3e2d	1f		.
	defb 001h		;3e2e	01		.
	defb 025h		;3e2f	25		%
	defb 01fh		;3e30	1f		.
	defb 000h		;3e31	00		.
	defb 0a5h		;3e32	a5		.
	defb 01eh		;3e33	1e		.
	defb 020h		;3e34	20		 
	defb 076h		;3e35	76		v
	defb 01eh		;3e36	1e		.
	defb 020h		;3e37	20		 
	defb 093h		;3e38	93		.
	defb 020h		;3e39	20		 
	defb 020h		;3e3a	20		 
	defb 097h		;3e3b	97		.
l3e3ch:
	defb 022h		;3e3c	22		"
	defb 020h		;3e3d	20		 
	defb 018h		;3e3e	18		.
	defb 022h		;3e3f	22		"
	defb 000h		;3e40	00		.
	defb 014h		;3e41	14		.
	defb 022h		;3e42	22		"
	defb 000h		;3e43	00		.
	defb 0c8h		;3e44	c8		.
	defb 03ah		;3e45	3a		:
	defb 000h		;3e46	00		.
	defb 0b0h		;3e47	b0		.
	defb 01fh		;3e48	1f		.
	defb 020h		;3e49	20		 
	defb 003h		;3e4a	03		.
	defb 033h		;3e4b	33		3
	defb 045h		;3e4c	45		E
	defb 0e2h		;3e4d	e2		.
	defb 032h		;3e4e	32		2
	defb 002h		;3e4f	02		.
	defb 0afh		;3e50	af		.
	defb 032h		;3e51	32		2
	defb 001h		;3e52	01		.
	defb 0c1h		;3e53	c1		.
	defb 031h		;3e54	31		1
	defb 000h		;3e55	00		.
	defb 0c1h		;3e56	c1		.
	defb 031h		;3e57	31		1
	defb 000h		;3e58	00		.
	defb 0b4h		;3e59	b4		.
	defb 036h		;3e5a	36		6
	defb 042h		;3e5b	42		B
	defb 099h		;3e5c	99		.
	defb 036h		;3e5d	36		6
	defb 042h		;3e5e	42		B
	defb 0abh		;3e5f	ab		.
	defb 031h		;3e60	31		1
	defb 001h		;3e61	01		.
	defb 04fh		;3e62	4f		O
	defb 034h		;3e63	34		4
	defb 001h		;3e64	01		.
	defb 032h		;3e65	32		2
	defb 034h		;3e66	34		4
	defb 001h		;3e67	01		.
	defb 062h		;3e68	62		b
	defb 036h		;3e69	36		6
	defb 000h		;3e6a	00		.
	defb 066h		;3e6b	66		f
	defb 036h		;3e6c	36		6
	defb 000h		;3e6d	00		.
	defb 0d7h		;3e6e	d7		.
	defb 00ah		;3e6f	0a		.
	defb 000h		;3e70	00		.
	defb 033h		;3e71	33		3
	defb 033h		;3e72	33		3
	defb 002h		;3e73	02		.
	defb 0d2h		;3e74	d2		.
	defb 033h		;3e75	33		3
	defb 000h		;3e76	00		.
	defb 014h		;3e77	14		.
	defb 036h		;3e78	36		6
	defb 080h		;3e79	80		.
	defb 01eh		;3e7a	1e		.
	defb 036h		;3e7b	36		6
	defb 080h		;3e7c	80		.
	defb 06fh		;3e7d	6f		o
	defb 035h		;3e7e	35		5
	defb 044h		;3e7f	44		D
	defb 0e2h		;3e80	e2		.
	defb 038h		;3e81	38		8
	defb 041h		;3e82	41		A
	defb 00dh		;3e83	0d		.
	defb 036h		;3e84	36		6
	defb 081h		;3e85	81		.
	defb 060h		;3e86	60		`
	defb 03ah		;3e87	3a		:
	defb 042h		;3e88	42		B
	defb 071h		;3e89	71		q
	defb 03ah		;3e8a	3a		:
	defb 042h		;3e8b	42		B
	defb 0f5h		;3e8c	f5		.
	defb 035h		;3e8d	35		5
	defb 044h		;3e8e	44		D
	defb 06ah		;3e8f	6a		j
	defb 034h		;3e90	34		4
	defb 082h		;3e91	82		.
	defb 057h		;3e92	57		W
	defb 03ah		;3e93	3a		:
	defb 042h		;3e94	42		B
	defb 068h		;3e95	68		h
	defb 03ah		;3e96	3a		:
	defb 042h		;3e97	42		B
	defb 05ch		;3e98	5c		\
	defb 036h		;3e99	36		6
	defb 001h		;3e9a	01		.
	defb 02fh		;3e9b	2f		/
	defb 035h		;3e9c	35		5
	defb 044h		;3e9d	44		D
	defb 010h		;3e9e	10		.
	defb 03ah		;3e9f	3a		:
	defb 001h		;3ea0	01		.
	defb 04dh		;3ea1	4d		M
	defb 036h		;3ea2	36		6
	defb 000h		;3ea3	00		.
	defb 00ch		;3ea4	0c		.
	defb 037h		;3ea5	37		7
	defb 001h		;3ea6	01		.
	defb 0dbh		;3ea7	db		.
	defb 036h		;3ea8	36		6
	defb 001h		;3ea9	01		.
	defb 061h		;3eaa	61		a
	defb 034h		;3eab	34		4
	defb 083h		;3eac	83		.
	defb 0efh		;3ead	ef		.
	defb 039h		;3eae	39		9
	defb 041h		;3eaf	41		A
	defb 074h		;3eb0	74		t
	defb 030h		;3eb1	30		0
	defb 020h		;3eb2	20		 
	defb 0ebh		;3eb3	eb		.
	defb 039h		;3eb4	39		9
	defb 041h		;3eb5	41		A
	defb 095h		;3eb6	95		.
	defb 030h		;3eb7	30		0
	defb 020h		;3eb8	20		 
	defb 085h		;3eb9	85		.
	defb 030h		;3eba	30		0
	defb 020h		;3ebb	20		 
	defb 055h		;3ebc	55		U
	defb 03bh		;3ebd	3b		;
	defb 001h		;3ebe	01		.
	defb 083h		;3ebf	83		.
	defb 036h		;3ec0	36		6
	defb 001h		;3ec1	01		.
	defb 07ah		;3ec2	7a		z
	defb 033h		;3ec3	33		3
	defb 001h		;3ec4	01		.
	defb 06ch		;3ec5	6c		l
	defb 033h		;3ec6	33		3
	defb 002h		;3ec7	02		.
	defb 083h		;3ec8	83		.
	defb 033h		;3ec9	33		3
	defb 001h		;3eca	01		.
	defb 060h		;3ecb	60		`
	defb 033h		;3ecc	33		3
	defb 002h		;3ecd	02		.
	defb 078h		;3ece	78		x
	defb 037h		;3ecf	37		7
	defb 041h		;3ed0	41		A
	defb 000h		;3ed1	00		.
l3ed2h:
	defb 0a1h		;3ed2	a1		.
	defb 0afh		;3ed3	af		.
	defb 0adh		;3ed4	ad		.
	defb 0afh		;3ed5	af		.
	defb 0a9h		;3ed6	a9		.
	defb 0afh		;3ed7	af		.
	defb 0adh		;3ed8	ad		.
	defb 0afh		;3ed9	af		.
l3edah:
	defb 063h		;3eda	63		c
	defb 0c0h		;3edb	c0		.
	defb 000h		;3edc	00		.
	defb 000h		;3edd	00		.
	defb 064h		;3ede	64		d
	defb 0c0h		;3edf	c0		.
	defb 000h		;3ee0	00		.
	defb 000h		;3ee1	00		.
	defb 0e5h		;3ee2	e5		.
	defb 0e9h		;3ee3	e9		.
	defb 0c0h		;3ee4	c0		.
	defb 0f7h		;3ee5	f7		.
	defb 04ah		;3ee6	4a		J
	defb 061h		;3ee7	61		a
	defb 0eeh		;3ee8	ee		.
	defb 046h		;3ee9	46		F
	defb 065h		;3eea	65		e
	defb 0e2h		;3eeb	e2		.
	defb 04dh		;3eec	4d		M
	defb 061h		;3eed	61		a
	defb 0f2h		;3eee	f2		.
	defb 041h		;3eef	41		A
l3ef0h:
	defb 070h		;3ef0	70		p
	defb 0f2h		;3ef1	f2		.
	defb 04dh		;3ef2	4d		M
	defb 061h		;3ef3	61		a
	defb 0f9h		;3ef4	f9		.
	defb 04ah		;3ef5	4a		J
	defb 075h		;3ef6	75		u
	defb 0eeh		;3ef7	ee		.
	defb 04ah		;3ef8	4a		J
	defb 075h		;3ef9	75		u
	defb 0ech		;3efa	ec		.
	defb 041h		;3efb	41		A
	defb 075h		;3efc	75		u
	defb 0e7h		;3efd	e7		.
	defb 053h		;3efe	53		S
	defb 065h		;3eff	65		e
	defb 0f0h		;3f00	f0		.
	defb 04fh		;3f01	4f		O
	defb 063h		;3f02	63		c
	defb 0f4h		;3f03	f4		.
	defb 04eh		;3f04	4e		N
	defb 06fh		;3f05	6f		o
	defb 0f6h		;3f06	f6		.
	defb 044h		;3f07	44		D
	defb 065h		;3f08	65		e
	defb 0e3h		;3f09	e3		.
	defb 053h		;3f0a	53		S
	defb 075h		;3f0b	75		u
	defb 0eeh		;3f0c	ee		.
	defb 04dh		;3f0d	4d		M
	defb 06fh		;3f0e	6f		o
	defb 0eeh		;3f0f	ee		.
	defb 054h		;3f10	54		T
	defb 075h		;3f11	75		u
	defb 0e5h		;3f12	e5		.
	defb 057h		;3f13	57		W
	defb 065h		;3f14	65		e
	defb 0e4h		;3f15	e4		.
	defb 054h		;3f16	54		T
	defb 068h		;3f17	68		h
	defb 0f5h		;3f18	f5		.
	defb 046h		;3f19	46		F
	defb 072h		;3f1a	72		r
	defb 0e9h		;3f1b	e9		.
	defb 053h		;3f1c	53		S
	defb 061h		;3f1d	61		a
	defb 0f4h		;3f1e	f4		.
	defb 000h		;3f1f	00		.
	defb 000h		;3f20	00		.
	defb 000h		;3f21	00		.
	defb 000h		;3f22	00		.
	defb 000h		;3f23	00		.
	defb 000h		;3f24	00		.
	defb 000h		;3f25	00		.
	defb 000h		;3f26	00		.
	defb 000h		;3f27	00		.
	defb 000h		;3f28	00		.
	defb 000h		;3f29	00		.
	defb 000h		;3f2a	00		.
	defb 000h		;3f2b	00		.
	defb 000h		;3f2c	00		.
	defb 000h		;3f2d	00		.
	defb 000h		;3f2e	00		.
	defb 000h		;3f2f	00		.
	defb 000h		;3f30	00		.
	defb 000h		;3f31	00		.
	defb 000h		;3f32	00		.
	defb 000h		;3f33	00		.
	defb 000h		;3f34	00		.
	defb 000h		;3f35	00		.
	defb 000h		;3f36	00		.
	defb 000h		;3f37	00		.
	defb 000h		;3f38	00		.
	defb 000h		;3f39	00		.
	defb 000h		;3f3a	00		.
	defb 000h		;3f3b	00		.
	defb 000h		;3f3c	00		.
	defb 000h		;3f3d	00		.
	defb 000h		;3f3e	00		.
	defb 000h		;3f3f	00		.
	defb 000h		;3f40	00		.
	defb 000h		;3f41	00		.
	defb 000h		;3f42	00		.
	defb 000h		;3f43	00		.
	defb 000h		;3f44	00		.
	defb 000h		;3f45	00		.
	defb 000h		;3f46	00		.
	defb 000h		;3f47	00		.
	defb 000h		;3f48	00		.
	defb 000h		;3f49	00		.
	defb 000h		;3f4a	00		.
	defb 000h		;3f4b	00		.
	defb 000h		;3f4c	00		.
	defb 000h		;3f4d	00		.
	defb 000h		;3f4e	00		.
	defb 000h		;3f4f	00		.
	defb 000h		;3f50	00		.
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
	defb 000h		;3f66	00		.
	defb 000h		;3f67	00		.
	defb 000h		;3f68	00		.
	defb 000h		;3f69	00		.
	defb 000h		;3f6a	00		.
	defb 000h		;3f6b	00		.
	defb 000h		;3f6c	00		.
	defb 000h		;3f6d	00		.
	defb 000h		;3f6e	00		.
	defb 000h		;3f6f	00		.
	defb 000h		;3f70	00		.
	defb 000h		;3f71	00		.
	defb 000h		;3f72	00		.
	defb 000h		;3f73	00		.
	defb 000h		;3f74	00		.
	defb 000h		;3f75	00		.
	defb 000h		;3f76	00		.
	defb 000h		;3f77	00		.
	defb 000h		;3f78	00		.
	defb 000h		;3f79	00		.
	defb 000h		;3f7a	00		.
	defb 000h		;3f7b	00		.
	defb 000h		;3f7c	00		.
	defb 000h		;3f7d	00		.
	defb 000h		;3f7e	00		.
	defb 000h		;3f7f	00		.
	defb 000h		;3f80	00		.
	defb 000h		;3f81	00		.
	defb 000h		;3f82	00		.
	defb 000h		;3f83	00		.
	defb 000h		;3f84	00		.
	defb 000h		;3f85	00		.
	defb 000h		;3f86	00		.
	defb 000h		;3f87	00		.
	defb 000h		;3f88	00		.
	defb 000h		;3f89	00		.
	defb 000h		;3f8a	00		.
	defb 000h		;3f8b	00		.
	defb 000h		;3f8c	00		.
	defb 000h		;3f8d	00		.
	defb 000h		;3f8e	00		.
	defb 000h		;3f8f	00		.
	defb 000h		;3f90	00		.
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
EncodedTail_end:
