# UI Mockups

## Enhanced Breakpoint Editor

Current breakpoint editor needs expansion for conditions, ranges, and actions.

```
+------------------------------------------------------------+
|                    Edit Breakpoint                         |
+------------------------------------------------------------+
| Type:    [Memory        v]                                 |
|                                                            |
| Address: [____0x8000____] - [____0x8FFF____]  (Range)      |
|                                                            |
| Access:  [x] Execute    [ ] Read    [ ] Write              |
|                                                            |
| +-- Condition ------------------------------------------+  |
| | Expression: [A == 5 && M(HL) > 0x20              ]    |  |
| |                                                       |  |
| | [ ] Fire on change only (edge trigger)                |  |
| | Skip first [____10____] hits                          |  |
| |                                                       |  |
| | [?] Expression Help                                   |  |
| +-------------------------------------------------------+  |
|                                                            |
| Action:  ( ) Break into debugger                           |
|          ( ) Log and continue                              |
|          ( ) Screenshot and continue                       |
|          ( ) Count only                                    |
|                                                            |
| Group:   [default        v]   [New Group...]               |
| Note:    [Break in main loop when A=5              ]       |
|                                                            |
| [x] Active                                                 |
|                                                            |
| Statistics: Hits: 1234   Fires: 45   [Reset]               |
|                                                            |
|                              [  Cancel  ]   [    OK    ]   |
+------------------------------------------------------------+
```

## Expression Help Dialog

```
+----------------------------------------------------------------------+
|                    Expression Help                                    |
+----------------------------------------------------------------------+
| OPERATORS                                                            |
| -----------------------------------------------------------------    |
| Arithmetic:  + - * / %                                               |
| Bitwise:     & | ^ ~ << >>                                           |
| Comparison:  < > <= >= == !=                                         |
| Logical:     && || !                                                 |
|                                                                      |
| MEMORY ACCESS                                                        |
| -----------------------------------------------------------------    |
| M(addr)     Byte at address                                          |
| [addr]      Word at address (little-endian)                          |
| addr->off   Same as M(addr + off)                                    |
|                                                                      |
| CPU REGISTERS                                                        |
| -----------------------------------------------------------------    |
| A B C D E H L       8-bit registers                                  |
| AF BC DE HL         16-bit pairs                                     |
| AF' BC' DE' HL'     Shadow registers                                 |
| IX IY SP PC         Index and stack registers                        |
| I R IXH IXL IYH IYL Other registers                                  |
|                                                                      |
| PSEUDO-VARIABLES                                                     |
| -----------------------------------------------------------------    |
| RD       Last memory read address                                    |
| WR       Last memory write address                                   |
| MDT      Last memory data byte                                       |
| IN       Last IN port                                                |
| OUT      Last OUT port                                               |
| VAL      Last IN/OUT data                                            |
| DOS      1 if TR-DOS ROM active, 0 otherwise                         |
| SLOT0-3  Page number in memory slot 0-3                              |
| FRAME    Frame counter since reset                                   |
| RAYX     Current beam X position                                     |
| RAYY     Current beam Y position                                     |
| HITS     This breakpoint's hit count                                 |
|                                                                      |
| SPECIAL FUNCTIONS                                                    |
| -----------------------------------------------------------------    |
| RAY(x,y)  True if beam crossed pixel (x,y) this instruction          |
|                                                                      |
| NUMBER FORMATS                                                       |
| -----------------------------------------------------------------    |
| 1234      Decimal                                                    |
| 0x4AF3    Hex (0x prefix)                                            |
| #4AF3     Hex (# prefix)                                             |
| $4AF3     Hex ($ prefix)                                             |
| 0177      Octal (leading 0)                                          |
| 'A'       Character code                                             |
|                                                                      |
| EXAMPLES                                                             |
| -----------------------------------------------------------------    |
| A == 5                      Break when A equals 5                    |
| SP < #8000                  Stack overflow detection                 |
| M(HL) == 0x00               Byte at HL is zero                       |
| IN == #FE && VAL & 1        Keyboard port, key pressed               |
| SLOT1 == 5 && PC >= #C000   Page 5 at C000, executing                |
| RAY(128, 96)                Beam at center of screen                 |
| HITS >= 100                 After 100th hit                          |
|                                                                      |
|                                                    [     Close     ] |
+----------------------------------------------------------------------+
```

## Breakpoint List with New Columns

```
+--------------------------------------------------------------------------------+
| Breakpoints                                                    [+] [-] [Clear] |
+--------------------------------------------------------------------------------+
| [x] | Type | Address       | Access | Condition        | Hits  | Action | Note |
+-----+------+---------------+--------+------------------+-------+--------+------+
| [x] | CPU  | 8000-8FFF     | X      | A == 5           | 1234  | Break  | Main |
| [x] | CPU  | 9000          | XRW    |                  | 567   | Break  |      |
| [ ] | IO   | FE (mask FF)  | I      | VAL & 0x1F != 0  | 89    | Log    | Keys |
| [x] | IRQ  | INT           | -      |                  | 12    | Break  |      |
| [x] | COND | -             | -      | SP < #8000       | 0     | Break  | Stk  |
+-----+------+---------------+--------+------------------+-------+--------+------+
| [Edit]  [Add...]  [Remove]  [Enable All]  [Disable All]  [Load...]  [Save...] |
+--------------------------------------------------------------------------------+
```

## Port Watch Widget

```
+------------------------------------------+
| Port Watch                     [+] [Cfg] |
+------------------------------------------+
| Port  | Last IN | Last OUT | R | W       |
+-------+---------+----------+---+---------+
| 00FE  |   BF    |    --    | * |         |  Keyboard
| 7FFD  |   --    |    10    |   | *       |  Memory page
| FFFD  |   --    |    0E    |   | *       |  AY register
| BFFD  |   --    |    00    |   | *       |  AY data
| 1FFD  |   --    |    00    |   | *       |  +3 paging
+-------+---------+----------+---+---------+
| R/W indicators flash on access           |
+------------------------------------------+
```

## Memory Watcher Widget

```
+------------------------------------------------------------------------+
| Memory Watch                                              [+] [-] [x]  |
+------------------------------------------------------------------------+
| Expression         | Type | Value  | +0 +1 +2 +3 +4 +5 +6 +7           |
+--------------------+------+--------+----------------------------------+
| HL                 | CPU  | 5C00   | 00 3C 40 00 FF 02 00 00          |
| DE + 10            | CPU  | 5C10   | 41 42 43 44 45 46 47 48          |
| sprite_ptr         | CPU  | 8000   | C3 00 80 00 00 00 00 00          |
| RAM 5:3F00         | RAM  | 053F00 | ED B0 C9 00 00 00 00 00          |
+--------------------+------+--------+----------------------------------+
| Add: [________________________] Type: [CPU v]  [Add Watch]             |
+------------------------------------------------------------------------+
```

## Memory Map Visualization

```
+--------------------------------------------------+
| Memory Map                                        |
+--------------------------------------------------+
| Slot 0 (0000-3FFF): [ROM 0  ] SOS48  Basic       |
| Slot 1 (4000-7FFF): [RAM 5  ] Screen             |
| Slot 2 (8000-BFFF): [RAM 2  ] User               |
| Slot 3 (C000-FFFF): [RAM 0  ] System             |
+--------------------------------------------------+
| [ ] DOS mode active                               |
| [ ] Show breakpoint coverage                      |
+--------------------------------------------------+
|   0000   4000   8000   C000   FFFF               |
|   |------|------|------|------|                  |
|   [ROM 0 ][RAM 5][RAM 2][RAM 0]                  |
|   ▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░                  |
|   ^ breakpoints highlighted                       |
+--------------------------------------------------+
```

## Debugger Window Layout with New Widgets

```
+------------------------------------------------------------------------+
| unreal-qt Debugger                                          [_][□][X] |
+------------------------------------------------------------------------+
| [>] [||] [->] [↓] [↑] [F] [INT] [RST] | [BPT] [LBL] [VIZ]              |
+------------------------------------------------------------------------+
| +-Disassembly------------------+ +-Registers--------+ +-Stack---------+|
| | 8000  3E 05     LD A,#05     | | AF  0500  S-H-P-C| | FFFE: 8003    ||
| |>8002  CD 00 90  CALL #9000   | | BC  0000         | | FFFC: 0000    ||
| | 8005  FE 0A     CP #0A       | | DE  5C00         | | FFFA: 5C00    ||
| | 8007  20 F7     JR NZ,#8000  | | HL  5C00         | | FFF8: 0000    ||
| | 8009  C9        RET          | | IX  5C3A         | |               ||
| |                              | | IY  5C3A         | |               ||
| |                              | | SP  FFFE         | |               ||
| |                              | | PC  8002         | |               ||
| +------------------------------+ +------------------+ +---------------+|
| +-Memory Dump-----------------------------------------------------------+
| | 5C00: 00 3C 40 00 FF 02 00 00 41 42 43 44 45 46 47 48  .<@.....ABCDEFGH|
| | 5C10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................|
| +-----------------------------------------------------------------------+
| +-Port Watch--------+ +-Memory Watch------------------------------------+
| | FE:BF IN  7FFD:10 | | HL      = 5C00: 00 3C 40 00 FF 02 00 00         |
| | FFFD:0E BFFD:00   | | DE + 10 = 5C10: 00 00 00 00 00 00 00 00         |
| +-------------------+ +--------------------------------------------------+
+------------------------------------------------------------------------+
```

## Conditional Breakpoint Visual Indicator

In disassembly view, show condition status:

```
+--Disassembly with Conditions------------------------------------------+
| Addr  Bytes        Instruction             Condition                  |
+-----------------------------------------------------------------------+
| 8000  3E 05        LD A,#05                                           |
|>8002• CD 00 90     CALL #9000              A==5 [✓ true]              |
| 8005  FE 0A        CP #0A                                             |
| 8007○ 20 F7        JR NZ,#8000             (disabled)                 |
| 8009● C9           RET                     M(SP)>#8000 [✗ false]      |
+-----------------------------------------------------------------------+
| Legend: ● active+condition  ○ inactive  • active (no condition)       |
+-----------------------------------------------------------------------+
```

## Global Conditions Panel

```
+--Global Conditions-------------------------------------+
| [x] SP < #8000           | Hits: 0    | Stack overflow |
| [x] FRAME == 50          | Hits: 0    | Frame 50       |
| [ ] A == 'Z' && B == 'X' | Hits: 123  | ZX signature   |
+--------------------------------------------------------+
| [Add Condition...]  [Remove]  [Enable All]  [Disable]  |
+--------------------------------------------------------+
```
