REM ==============================================================================
REM "Scroller by Demarche" (1996) — TR-DOS BASIC Loader (SCROLLER.B)
REM Complete annotated source code and token-level breakdown
REM ==============================================================================

REM Original BASIC Listing (333 bytes, fails on 128K Sinclair menu):
10 INK NOT PI: PAPER NOT PI: BORDER NOT PI: CLEAR VAL "25088"
20 RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL00" CODE
30 RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL15" CODE
40 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL10" CODE
50 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL11" CODE
60 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL13" CODE
70 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL17" CODE
80 RANDOMIZE USR VAL "25094": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE
90 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "25088"

REM Patched Dual-Mode Listing (scroller_fixed.trd, 349 bytes, runs universally):
REM 80 RANDOMIZE USR VAL "25094": POKE VAL "23388",VAL "20": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE

REM ==============================================================================
REM DETAILED LINE-BY-LINE ANALYSIS
REM ==============================================================================

REM ------------------------------------------------------------------------------
REM Line 10: System Initialization & Memory Clamping
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
REM Lines 20–30: Loading the Core Dispatcher and First Payload
REM ------------------------------------------------------------------------------
REM - USR VAL "15619" ($3D03):
REM     Standard TR-DOS entry point for inline BASIC file operations. TR-DOS parses
REM     the characters immediately following "REM : " as a disk command.
REM - Line 20: LOAD "SCROLL00" CODE
REM     Loads SCROLL00.C (178 bytes) to address $6200. This installs the depack
REM     dispatcher at $6206 and the MegaLZ decompressor at $6244.
REM - Line 30: LOAD "SCROLL15" CODE
REM     Loads SCROLL15.C (1,708 bytes) to address $8000. This is the first packed
REM     chunk containing runtime engine extensions.

REM ------------------------------------------------------------------------------
REM Lines 40–70: The Streaming Decrunch Pipeline
REM ------------------------------------------------------------------------------
REM Each cycle follows a strict pattern:
REM   1. USR 25094 ($6206) calls the depack dispatcher.
REM   2. The dispatcher reads the next entry from the table at $6226, switches port
REM      #7FFD to the destination bank, and decompresses the previous file from
REM      RAM using MegaLZ.
REM   3. USR 15619 calls TR-DOS to load the next packed file into $8000.
REM
REM - Line 40:
REM     * USR 25094: Depack Call #1 reads SCROLL15 from $8000 and decrunches it
REM       directly into Page 5 at $62B2 (right after SCROLL00 in RAM).
REM     * LOAD "SCROLL10" CODE: 18 sectors (4,601 bytes) loaded to $8000.
REM - Line 50:
REM     * USR 25094: Depack Call #2 reads SCROLL10 from $8000 and decrunches it
REM       into RAM Page 0 at $C000 (Sample Block 1).
REM     * LOAD "SCROLL11" CODE: 21 sectors (5,342 bytes) loaded to $8000.
REM - Line 60:
REM     * USR 25094: Depack Call #3 reads SCROLL11 from $8000 and decrunches it
REM       into RAM Page 1 at $C000 (Sample Block 2).
REM     * LOAD "SCROLL13" CODE: 12 sectors (2,962 bytes) loaded to $8000.
REM - Line 70:
REM     * USR 25094: Depack Call #4 reads SCROLL13 from $8000 and decrunches it
REM       into RAM Page 3 at $C000 (Sample Block 3).
REM     * LOAD "SCROLL17" CODE: 18 sectors (4,371 bytes) loaded to $8000.

REM ------------------------------------------------------------------------------
REM Line 80: The Critical Transition and Port Desynchronization Trap
REM ------------------------------------------------------------------------------
REM - RANDOMIZE USR VAL "25094":
REM     Depack Call #5 reads SCROLL17 from $8000 and decrunches it into
REM     RAM Page 7 at $DB00.
REM
REM - OUT VAL "32765", VAL "20":
REM     Direct hardware I/O write to port #7FFD ($7FFD = 32765):
REM       Value 20 ($14 = %00010100):
REM         Bits 0-2 = 4  -> Maps RAM Page 4 to Bank 3 ($C000-$FFFF)
REM         Bit 3    = 0  -> Screen 0 (Page 5)
REM         Bit 4    = 1  -> 48K ROM (ROM 1)
REM         Bit 5    = 0  -> Paging enabled
REM
REM     FATAL FLAW UNDER SINCLAIR 128K EDITOR:
REM     Because OUT directly hits the hardware bus without updating system variable
REM     BANK_M ($5B5C / 23388), BANK_M remains $00.
REM     When the Sinclair 128K editor's statement-end hook executes at the colon (:),
REM     the inter-ROM trampoline at $5B14 -> $5B00 reads BANK_M ($00), toggles bit 4,
REM     and writes $10 to port #7FFD, switching Bank 3 from Page 4 back to Page 0!
REM
REM - RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE:
REM     TR-DOS loads the 48 sectors (12,155 bytes) of SCROLL12 into $C000.
REM     On Pentagon / 48K mode: Data lands in Page 4 (correct).
REM     Under 128K Sinclair menu: Data lands in Page 0 (corrupting Page 0, leaving Page 4 empty).
REM
REM - THE UNIVERSAL DUAL-MODE FIX (Applied in scroller_fixed.trd):
REM     Replace Line 80 with:
REM       80 RANDOMIZE USR VAL "25094": POKE VAL "23388",VAL "20": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE
REM
REM     * Under 128K Sinclair editor:
REM         POKE VAL "23388", VAL "20" updates BANK_M ($5B5C). When the statement ends
REM         at the colon (:), the $5B00 SWAP routine toggles bit 4 across ROM flips but
REM         preserves bits 0-2 (Page 4), reasserting Page 4 before TR-DOS loads SCROLL12!
REM     * Under authentic 48K BASIC / TR-DOS:
REM         OUT VAL "32765", VAL "20" directly latches #7FFD to Page 4 on the hardware bus.
REM         (Because 48K BASIC lacks the $5B00 hook, POKE alone would not update #7FFD).
REM     * Result: 100% stable execution across all models, menus, and configurations.

REM ------------------------------------------------------------------------------
REM Line 90: Final Decrunch & Demo Execution
REM ------------------------------------------------------------------------------
REM - RANDOMIZE USR VAL "25094":
REM     Depack Call #6:
REM       Table entry: Page 4, Dest = $8000, Src = $C000.
REM       MegaLZ decrunches SCROLL12 from Page 4 into Bank 2 ($8000-$BFFF),
REM       which contains the main demo engine, graphics scroller, Covox menu,
REM       and IM2 audio interrupt driver.
REM       (If Page 4 is empty, it writes all $00 to $8000-$BFFF).
REM
REM - RANDOMIZE USR VAL "25088":
REM     Jumps to $6200:
REM       $6200: LD SP, $6200
REM       $6203: JP $9B6B  -> Transfers control to the Covox menu!
