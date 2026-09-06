REM ==============================================================================
REM "Scroller by Demarche" (1996) — Patched TR-DOS BASIC Loader (SCROLLER.B)
REM Complete annotated source code and dual-mode token-level breakdown
REM ==============================================================================

10 INK NOT PI: PAPER NOT PI: BORDER NOT PI: CLEAR VAL "25088"
20 RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL00" CODE
30 RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL15" CODE
40 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL10" CODE
50 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL11" CODE
60 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL13" CODE
70 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL17" CODE
80 RANDOMIZE USR VAL "25094": POKE VAL "23388",VAL "20": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE
90 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "25088"

REM ==============================================================================
REM LINE-BY-LINE ANALYSIS & THE DUAL-MODE FIX
REM ==============================================================================

REM ------------------------------------------------------------------------------
REM Line 10: System Initialization & Memory Protection
REM ------------------------------------------------------------------------------
REM - INK NOT PI:
REM     Sets INK color to 0 (black). In Sinclair BASIC, NOT PI evaluates to 0
REM     without the 6-byte inline numeric float encoding, saving memory.
REM - PAPER NOT PI:
REM     Sets PAPER color to 0 (black).
REM - BORDER NOT PI:
REM     Sets border color to 0 (black).
REM - CLEAR VAL "25088" ($61FF):
REM     Sets RAMTOP to $61FF (25,088 - 1). This clears all variables and protects
REM     all memory from $6200 upwards from BASIC variables and stack manipulation.
REM     The depack dispatcher SCROLL00 is loaded at $6200 in Line 20.

REM ------------------------------------------------------------------------------
REM Lines 20–70: Streaming Decrunch Pipeline
REM ------------------------------------------------------------------------------
REM - Line 20: Loads SCROLL00.C (178 bytes) to $6200 (Dispatcher & MegaLZ decompressor).
REM - Line 30: Loads SCROLL15.C (1,708 bytes) to $8000 (Packed audio engine).
REM - Line 40: USR 25094 decrunches SCROLL15 into Page 5 at $62B2.
REM            Loads SCROLL10.C (4,601 bytes) to $8000.
REM - Line 50: USR 25094 decrunches SCROLL10 into Page 0 at $C000 (Audio Sample 1).
REM            Loads SCROLL11.C (5,342 bytes) to $8000.
REM - Line 60: USR 25094 decrunches SCROLL11 into Page 1 at $C000 (Audio Sample 2).
REM            Loads SCROLL13.C (2,962 bytes) to $8000.
REM - Line 70: USR 25094 decrunches SCROLL13 into Page 3 at $C000 (Audio Sample 3).
REM            Loads SCROLL17.C (4,371 bytes) to $8000.

REM ------------------------------------------------------------------------------
REM Line 80: The Patched Dual-Mode Transition
REM ------------------------------------------------------------------------------
REM Original Line 80:
REM   80 RANDOMIZE USR VAL "25094": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE
REM
REM Patched Line 80:
REM   80 RANDOMIZE USR VAL "25094": POKE VAL "23388",VAL "20": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE
REM
REM 1. USR VAL "25094":
REM    Depack Call #5 reads SCROLL17 from $8000 and decrunches it into Page 7 at $DB00.
REM
REM 2. POKE VAL "23388", VAL "20":
REM    Writes 0x14 (%00010100) to system variable BANK_M ($5B5C, address 23388):
REM      - Bits 0-2 = 4 (RAM Page 4)
REM      - Bit 3    = 0 (Screen 0)
REM      - Bit 4    = 1 (ROM 1, 48K BASIC)
REM      - Bit 5    = 0 (Paging enabled)
REM    * Why this is essential under the Sinclair 128K Editor:
REM      At the statement separator (:), the editor's inter-ROM trampoline at $5B14
REM      calls $5B00 (SWAP). $5B00 reads BANK_M, toggles bit 4 across ROM switches,
REM      and writes the result to port #7FFD. Because BANK_M has bits 0-2 = 4,
REM      $5B00 maintains RAM Page 4 across the ROM switch, ensuring TR-DOS receives
REM      Page 4 at $C000!
REM
REM 3. OUT VAL "32765", VAL "20":
REM    Direct hardware I/O write to port #7FFD ($7FFD = 32765) with value 20 (0x14).
REM    * Why this is essential under 48K BASIC / Native TR-DOS boot:
REM      In authentic 48K / Pentagon mode, the Sinclair 128K editor is never initialized,
REM      and the $5B00 SWAP routine is not installed in RAM. 48K BASIC ignores BANK_M,
REM      so this OUT directly switches the hardware latch to Page 4.
REM
REM 4. RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE:
REM    TR-DOS loads the 48 sectors (12,155 bytes) of SCROLL12 directly into Page 4
REM    at $C000. In both environments, Page 4 is properly mapped!

REM ------------------------------------------------------------------------------
REM Line 90: Final Decrunch & Demo Execution
REM ------------------------------------------------------------------------------
REM - RANDOMIZE USR VAL "25094":
REM     Depack Call #6 reads SCROLL12 from Page 4 at $C000 and decrunches it into
REM     Page 2 at $8000-$BFFF (Engine, Scroller, Covox Menu, and IM2 driver).
REM - RANDOMIZE USR VAL "25088":
REM     Jumps to $6200:
REM       LD SP, $6200
REM       JP $9B6B  -> Starts the Covox Menu in Page 2!
