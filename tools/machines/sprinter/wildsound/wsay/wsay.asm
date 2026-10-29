; wsay - a PT3 (Pro Tracker 3 / Vortex Tracker II) player for the Peters Plus Sprinter that plays through the
; AY chips of the Wild Sound ISA card. One module: chip 0. Two modules (TurboSound): chip 0 and chip 1.
;
; Assembler: sjasmplus (1.20+). Build with wsaybuild.py, which writes wsay_mods.inc (the INCBINs of the
; module(s)), defines MOD2 for TurboSound (-DMOD2) and adds the 22-byte DSS EXE header in front of the raw code:
;     sjasmplus --nologo --raw=wsay.bin -I<dir of wsay_mods.inc> wsay.asm
;
; The player is S.V. Bulba's PT3 player (pt3prom.asm, freeware, (c)2004,2007 S.V.Bulba), included once per chip.
;
; Command line: <TRACK>.EXE [2]   - "2" = the card is in ISA slot 2 (page #D6); default slot 1 (page #D4).
; Any key stops the music; the card is silenced and DSS gets control back (DSS Exit).
;
; How a write reaches the card (docs/disasm/software/sprinter/prosiak-wildsound/player.asm, SetIsaLatch,
; SelectIsaIo, IsaOut, BusIdle): #1FFD <- #11 (pages #D0-#DF mean ISA), window 3 (port #E2) <- #D4 / #D6
; (ISA I/O space of the slot), #9FBD <- 0 (A19-A14 = 0, AEN = 0, RESET = 0). Then a write to #C000 is an ISA
; I/O write of port 0 (AY register select), #C001 port 1 (AY data); a read of #C001 reads the selected
; register. Register select #FF picks chip 0, #FE chip 1 (the card's TurboSound-style select). Afterwards
; #9FBD <- #40 (AEN on, bus idle), window 3 back, #1FFD <- #01.

	DEVICE NONE

DSS_EXIT	EQU #41
DSS_SCANKEY	EQU #31
DSS_PCHARS	EQU #5C

PORT_1FFD	EQU #1FFD
PORT_ISA	EQU #9FBD	; ISA latch: bits 5-0 = A19-A14, bit 6 = AEN, bit 7 = RESET
PORT_PAGE3	EQU #E2		; window 3 (#C000-#FFFF) page
ISA_PAGE_1	EQU #D4		; ISA I/O space of slot 1
ISA_PAGE_2	EQU #D6		; ISA I/O space of slot 2
CARD_ADDR	EQU #C000	; ISA I/O port 0: register select
CARD_DATA	EQU #C001	; ISA I/O port 1: register data

	ORG #8100

Start:
	DI
	LD SP,StackTop		; the header's SP too; below window 3, which is the card during CardOut
	LD A,(IX+0)		; PSP: command-line length, parameters from IX+1
	OR A
	JR Z,.slotDone
	LD B,A
	PUSH IX
	POP HL
.parse:
	INC HL
	LD A,(HL)
	CP '2'
	JR NZ,.next
	LD A,ISA_PAGE_2
	LD (IsaPage),A
	LD A,'2'
	LD (MsgSlotNum),A
.next:
	DJNZ .parse
.slotDone:
	LD HL,MsgBanner
	CALL Print

	CALL Probe
	JR Z,.found
	LD HL,MsgNoCard
	CALL Print
	LD HL,MsgSlotNum
	CALL Print
	CALL BusIdle
	LD BC,#0100+DSS_EXIT	; exit code 1
	RST #10
.found:
	LD HL,MsgFound
	CALL Print
	LD HL,MsgSlotNum
	CALL Print

	LD HL,MsgChip0
	CALL Print
	LD HL,Mod1
	CALL PrintTitle
	IFDEF MOD2
	LD HL,MsgChip1
	CALL Print
	LD HL,Mod2
	CALL PrintTitle
	ENDIF
	LD HL,MsgKey
	CALL Print

	CALL Silence
	LD HL,Mod1
	CALL pa.INIT
	IFDEF MOD2
	LD HL,Mod2
	CALL pb.INIT
	ENDIF

	EI
PlayLoop:
	HALT			; DSS frame interrupt: 50 Hz
	CALL pa.PLAY
	IFDEF MOD2
	CALL pb.PLAY
	ENDIF
	LD C,DSS_SCANKEY	; ZF = 1: no key
	RST #10
	JR Z,PlayLoop

	CALL Silence
	CALL BusIdle
	LD HL,MsgBye
	CALL Print
	LD BC,#0000+DSS_EXIT	; exit code 0
	RST #10

; Print the zero-terminated string at HL
Print:
	LD C,DSS_PCHARS
	RST #10
	RET

; Print "<title> by <author>" of the PT3 module at HL (header offsets 30..97), control characters as spaces
PrintTitle:
	LD DE,30
	ADD HL,DE
	LD DE,TitleBuf
	LD B,68
.copy:
	LD A,(HL)
	CP 32
	JR NC,.keep
	LD A,' '
.keep:
	LD (DE),A
	INC HL
	INC DE
	DJNZ .copy
	EX DE,HL
	LD (HL),13
	INC HL
	LD (HL),10
	INC HL
	LD (HL),0
	LD HL,TitleBuf
	JR Print

; Card access. Open: interrupts off, window 3 = the slot's ISA I/O space, ISA cycles on.
IsaOpen:
	DI
	IN A,(PORT_PAGE3)
	LD (SavedPage3),A
	LD BC,PORT_1FFD
	LD A,#11
	OUT (C),A
	LD A,(IsaPage)
	OUT (PORT_PAGE3),A
	LD BC,PORT_ISA
	XOR A
	OUT (C),A
	RET

; Close: AEN on (no cycles reach the cards), window 3 back, #1FFD <- #01, interrupts on
IsaClose:
	LD BC,PORT_ISA
	LD A,#40
	OUT (C),A
	LD A,(SavedPage3)
	OUT (PORT_PAGE3),A
	LD BC,PORT_1FFD
	LD A,#01
	OUT (C),A
	EI
	RET

; #9FBD <- #40 on its own (left that way on exit)
BusIdle:
	DI
	LD BC,PORT_1FFD
	LD A,#11
	OUT (C),A
	LD BC,PORT_ISA
	LD A,#40
	OUT (C),A
	LD BC,PORT_1FFD
	LD A,#01
	OUT (C),A
	EI
	RET

; Is a Wild Sound card there? Register #E0 (DEV_SIG) reads #55, #AA. ZF = 1: found.
Probe:
	CALL IsaOpen
	LD A,#FF		; chip 0
	LD (CARD_ADDR),A
	LD A,#E0
	LD (CARD_ADDR),A
	LD A,(CARD_DATA)
	LD E,A
	LD A,(CARD_DATA)
	LD D,A
	LD A,#FF		; chip 0 again, its register latch back to a normal register
	LD (CARD_ADDR),A
	XOR A
	LD (CARD_ADDR),A
	CALL IsaClose
	LD HL,#AA55
	OR A
	SBC HL,DE
	RET

; Write one chip's AY registers. HL = the player's AYREGS (R0..R13), E = #FF (chip 0) or #FE (chip 1).
; R0..R12 always, R13 (envelope shape) only when it is not negative (the player's "no new envelope" mark),
; exactly as the original ROUT. Called by the player (pt3prom.asm ROUT_A0) at the end of PLAY, MUTE, INIT.
CardOut:
	PUSH HL
	CALL IsaOpen
	POP HL
	LD A,E
	LD (CARD_ADDR),A	; select the chip
	XOR A
.reg:
	LD (CARD_ADDR),A
	LD C,A
	LD A,(HL)
	LD (CARD_DATA),A
	INC HL
	LD A,C
	INC A
	CP 13
	JR NZ,.reg
	LD C,(HL)
	BIT 7,C
	JR NZ,.noEnv
	LD (CARD_ADDR),A	; A = 13
	LD A,C
	LD (CARD_DATA),A
.noEnv:
	JR IsaClose

; Both chips: mixer all off, R8-R10 = 0
Silence:
	LD E,#FF
	CALL .chip
	LD E,#FE
.chip:
	CALL IsaOpen
	LD A,E
	LD (CARD_ADDR),A
	LD A,7
	LD (CARD_ADDR),A
	LD A,#3F
	LD (CARD_DATA),A
	LD A,8
.vol:
	LD (CARD_ADDR),A
	LD C,A
	XOR A
	LD (CARD_DATA),A
	LD A,C
	INC A
	CP 11
	JR NZ,.vol
	JP IsaClose

MsgBanner:
	DB "wsay - PT3 player for the Wild Sound ISA card (S.V.Bulba's PT3 player)",13,10,0
MsgFound:
	DB "Wild Sound found in ISA slot ",0
MsgNoCard:
	DB "Wild Sound (register #E0 = #55, #AA) not found in ISA slot ",0
MsgSlotNum:
	DB "1",13,10,0
MsgChip0:
	DB "AY 0: ",0
MsgChip1:
	DB "AY 1: ",0
MsgKey:
	DB "Press any key to stop.",13,10,0
MsgBye:
	DB "Stopped.",13,10,0

IsaPage:
	DB ISA_PAGE_1
SavedPage3:
	DB 0
TitleBuf:
	DS 68+3

; The player, once per chip. Its labels become pa.* / pb.*; CHIPSEL is its chip select for CardOut.
	MODULE pa
CHIPSEL	EQU #FF
	INCLUDE "pt3prom.asm"
	ENDMODULE

	IFDEF MOD2
	MODULE pb
CHIPSEL	EQU #FE
	INCLUDE "pt3prom.asm"
	ENDMODULE
	ENDIF

	DS 256
StackTop:
CodeEnd:
	INCLUDE "wsay_mods.inc"	; Mod1 (and Mod2): the INCBINs written by wsaybuild.py; may run into window 3
ModsEnd:
	ASSERT ModsEnd <= #10000	; DSS loads at most #7F00 bytes from #8100
	ASSERT pa.VARSEND <= #C000	; the player's variables must stay out of window 3 (it is the card then)
	IFDEF MOD2
	ASSERT pb.VARSEND <= #C000
	ENDIF
