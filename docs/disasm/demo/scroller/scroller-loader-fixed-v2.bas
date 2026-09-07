REM ==============================================================================
REM "Scroller by Demarche" (1996) — Patched TR-DOS BASIC Loader v2 (SCROLLER.B)
REM With paging lock detection for 128K→48K mode
REM ==============================================================================

10 INK NOT PI: PAPER NOT PI: BORDER NOT PI: CLEAR VAL "25088"
11 FOR i=0 TO 59: READ d: POKE 23760+i,d: NEXT i
12 DATA 33,0,192,54,222,35,54,173,35,54,190,35,54,239,1,253,127,62,1,237,121,58,0,192,254,222,32,27,58,1,192,254,173,32,20,58,2,192,254,190,32,13,58,3,192,254,239,32,6,175,237,121,62,1,201,175,237,121,175,201
13 IF USR 23760 THEN PRINT "PAGING LOCKED!": PRINT "Use RESET=BASIC": STOP
20 RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL00" CODE
30 RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL15" CODE
40 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL10" CODE
50 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL11" CODE
60 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL13" CODE
70 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL17" CODE
80 RANDOMIZE USR VAL "25094": POKE VAL "23388",VAL "20": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE
90 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "25088"

REM ==============================================================================
REM LINE-BY-LINE ANALYSIS
REM ==============================================================================

REM ------------------------------------------------------------------------------
REM Line 10: System Initialization & Memory Protection
REM ------------------------------------------------------------------------------
REM - INK/PAPER/BORDER NOT PI: Sets all to 0 (black) using NOT PI trick
REM - CLEAR VAL "25088": Protects memory from $6200 upwards

REM ------------------------------------------------------------------------------
REM Lines 11-13: Paging Lock Detection (NEW IN V2)
REM ------------------------------------------------------------------------------
REM
REM Purpose: Detect if the user booted via 128K menu → 48K BASIC, which locks
REM port #7FFD bit 5 permanently until reset, making page switching impossible.
REM
REM Line 11: FOR i=0 TO 59: READ d: POKE 23760+i,d: NEXT i
REM   - Loads 60-byte machine code routine into printer buffer at $5CD0 (23760)
REM
REM Line 12: DATA 33,0,192,...
REM   - The assembled machine code for paging lock detection:
REM
REM   $5CD0: LD HL, $C000        ; Point to bank 3 area
REM   $5CD3: LD (HL), $DE        ; Write signature byte 1
REM   $5CD5: INC HL: LD (HL),$AD ; Signature byte 2
REM   $5CD8: INC HL: LD (HL),$BE ; Signature byte 3
REM   $5CDB: INC HL: LD (HL),$EF ; Signature byte 4 (total: $DEADBEEF)
REM   $5CDE: LD BC, $7FFD        ; Paging port
REM   $5CE1: LD A, 1             ; Try page 1
REM   $5CE3: OUT (C), A          ; Switch (ignored if locked)
REM   $5CE5: LD A, ($C000)       ; Read back
REM   $5CE8: CP $DE              ; Check signature byte 1
REM   $5CEA: JR NZ, ok           ; If different, page switched = OK
REM   $5CEC: LD A, ($C001)       ; Check byte 2
REM   $5CEF: CP $AD
REM   $5CF1: JR NZ, ok
REM   $5CF3: LD A, ($C002)       ; Check byte 3
REM   $5CF6: CP $BE
REM   $5CF8: JR NZ, ok
REM   $5CFA: LD A, ($C003)       ; Check byte 4
REM   $5CFD: CP $EF
REM   $5CFF: JR NZ, ok
REM   ; All 4 bytes still match = PAGING LOCKED
REM   $5D01: XOR A: OUT (C), A   ; Restore page 0
REM   $5D04: LD A, 1             ; Return 1 = locked
REM   $5D06: RET
REM   ; ok:
REM   $5D07: XOR A: OUT (C), A   ; Restore page 0
REM   $5D0A: XOR A               ; Return 0 = OK
REM   $5D0B: RET
REM
REM Line 13: IF USR 23760 THEN PRINT "PAGING LOCKED!": PRINT "Use RESET=BASIC": STOP
REM   - Calls the detection routine
REM   - If returns non-zero (locked), displays error and stops
REM   - User must reboot with RESET=BASIC or RESET=DOS configuration

REM ------------------------------------------------------------------------------
REM Lines 20–70: Streaming Decrunch Pipeline (unchanged from v1)
REM ------------------------------------------------------------------------------
REM See scroller-loader-fixed.bas for detailed analysis of these lines.

REM ------------------------------------------------------------------------------
REM Line 80: The Patched Dual-Mode Transition (from v1)
REM ------------------------------------------------------------------------------
REM - POKE VAL "23388", VAL "20": Updates BANK_M for 128K editor compatibility
REM - OUT VAL "32765", VAL "20": Direct hardware write for 48K/TR-DOS mode
REM - Together they ensure Page 4 is properly mapped in all boot scenarios

REM ------------------------------------------------------------------------------
REM Line 90: Final Decrunch & Demo Execution (unchanged)
REM ------------------------------------------------------------------------------
REM - Decrunches SCROLL12 from Page 4 into Page 2
REM - Jumps to $9B6B (Covox Menu)
