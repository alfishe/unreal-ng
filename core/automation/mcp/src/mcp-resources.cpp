// MCP resources implementation — see mcp-resources.h
//
// Static reference documents are embedded as markdown raw string literals.
// The dynamic emulator-state resource formats GET /api/v1/emulator as text.

#include "mcp-resources.h"

#include <sstream>

namespace mcp
{

/// region <Embedded content>

namespace
{

constexpr const char* kKeyboardLayout = R"md(# ZX Spectrum Keyboard (Unreal-NG key names)

Use these exact key names with the type_input tool ("key"/"keys" arguments).

## Letters and digits
`a`..`z` (lowercase), `0`..`9`

## Modifiers
| Key name | Aliases | Notes |
|:--|:--|:--|
| `caps` | `shift`, `capsshift`, `caps_shift`, `cs` | CAPS SHIFT — BASIC keyword mode |
| `symbol` | `sym`, `symshift`, `sym_shift`, `ss` | SYMBOL SHIFT — symbol mode |

## Special keys
| Key name | Aliases |
|:--|:--|
| `enter` | `return` |
| `space` | ` ` |

## Extended keys (emulated on host keyboard)
| Key name | Aliases |
|:--|:--|
| `up` / `down` / `left` / `right` | cursor keys (real hardware: cs+5..8) |
| `delete` | `backspace`, `del` (real hardware: cs+0) |
| `break` | cs+space |
| `edit` | cs+1 |
| `dot` | `.` |
| `comma` | `,` |
| `plus` | `+` |
| `minus` | `-` |
| `multiply` | `*` |
| `divide` | `/` |
| `equal` | `=`, `equals` |
| `dblquote` | `"`, `quote` |

## BASIC keyword entry (original 40-keyboard matrix)
Each key produces a BASIC keyword in K-mode: G (THEN), H (GOTO), K (LIST), L (LET), M (RUN), N (NEXT), O (POKE), P (PRINT),
Q (PLOT), R (INPUT), S (SAVE), T (LOAD), U (RANDOMIZE), V (RETURN), W (BORDER), X (NEXT), Y (RETURN), Z (COPY).
Symbol-shift combinations produce the punctuation/symbols above.

## Named macros (type_input action:'macro')
`e_mode` (ENTER E-mode), `g_mode` (G-mode), `format`, `cat`, `erase`, `move`, `break`.

## Tips
- `type` with `tokenized: true` types BASIC commands using tokenized keyword entry (fast and cursor-accurate).
- `combo` presses several keys simultaneously, e.g. keys:["cs","ss"] for true video mode.
)md";

constexpr const char* kBasicReference = R"md(# ZX Spectrum BASIC Quick Reference (48K/128K ROM)

## Program control
| Command | Example | Notes |
|:--|:--|:--|
| RUN | `RUN 5000` | Start program (optionally at line) |
| LIST | `LIST 100-200` | List (line range) |
| NEW | `NEW` | Erase program |
| CLEAR | `CLEAR 30000` | Reset variables, set RAMTOP |
| STOP / CONTINUE | | Break / resume |
| GO TO / GO SUB / RETURN | `GO TO 100`, `GO SUB 5000` | Jump / call / return |
| IF / THEN | `IF x>3 THEN GO TO 100` | No ELSE — use two IFs |
| FOR / NEXT / STEP | `FOR i=1 TO 10 STEP 2: NEXT i` | Loops |
| PAUSE | `PAUSE 50` | Wait frames (50 = 1s); 0 = until key |
| RANDOMIZE | `RANDOMIZE 1` | Seed (1 → fixed for POKE 23672 tricks) |
| REM | | Comment |

## Data and variables
LET (assignment), DIM (arrays), INPUT, PRINT (AT y,x / TAB n; ',' separates print zones), ATTR/POINT/SCREEN$,
PEEK/POKE (memory), IN/OUT (ports), USR (machine code call: `RANDOMIZE USR 32768`), STR$/VAL/CODE/CHR$ (strings),
INT/ABS/SGN/SQR/SIN/COS/TAN/ASN/ACS/ATN/LN/EXP/PI/RND (math), BIN/HEX-not-native (use decimal).

## System interface
| Command | Purpose |
|:--|:--|
| LOAD "" / SAVE "name" | Tape (128K: also LOAD *"m";1;"name" for +3 DOS) |
| BORDER n / PAPER n / INK n / BRIGHT / FLASH / OVER / INVERSE | Colors: 0 black, 1 blue, 2 red, 3 magenta, 4 green, 5 cyan, 6 yellow, 7 white |
| PLOT / DRAW / CIRCLE | Graphics (256x192, origin bottom-left) |
| BEEP duration,pitch | `BEEP 1,0` = 1 second middle C; pitch in semitones |
| CLS / SCROLL | Clear / scroll |
| VERIFY / MERGE | Tape verify / merge |
| CAT / ERASE / FORMAT / MOVE (TR-DOS) | Disk via macros: `RANDOMIZE USR 15616` enters TR-DOS |

## Key system variables (POKE/PEEK targets)
| Address | Meaning |
|:--|:--|
| 23606/23607 | CHARS — font address (minus 256) |
| 23617 | FLAGS — keyboard shift state |
| 23658 | ATTR_P persistent color |
| 23672-23674 | FRAMES — 3-byte frame counter (~50 Hz) |
| 23732 | RAMTOP |
| 23552-23560 | KSTATE keyboard buffer |

## Entry points
- `RANDOMIZE USR 0` → reset. `RANDOMIZE USR 15616` → TR-DOS 128K.
- Interrupt mode 1 vector: address 0x38; IM 2 vector table at I*256.
)md";

constexpr const char* kZ80Isa = R"md(# Z80 Instruction Set Quick Reference

Registers: A F B C D E H L (AF' BC' DE' HL' shadow), IX IY SP PC I R. Flags: S Z Y H X PV N C.

## Loading
`LD r,r'` `LD r,n` `LD r,(HL)` `LD (HL),r/n` `LD r,(IX+d)/(IY+d)` `LD A,(BC)/(DE)/(nn)` `LD (nn),A/HL/BC/DE/IX/IY`
`LD A,I` `LD I,A` `LD A,R` `LD R,A` `LD SP,HL/IX/IY` `PUSH/POP AF/BC/DE/HL/IX/IY` `EX DE,HL` `EX AF,AF'` `EXX` `EX (SP),HL/IX/IY`
`LDI/LDD/LDIR/LDDR` (block transfer BC bytes HL→DE) `CPI/CPD/CPIR/CPDR` (block compare) `INI/IND/INIR/INDR` `OUTI/OUTD/OTIR/OTDR`

## Arithmetic / logic
`ADD/ADC/SUB/SBC/AND/XOR/OR/CP` A,src (src: r n (HL) (IX+d)); 16-bit: `ADD HL,rr` `ADC/SBC HL,rr` `ADD IX/IY,pp`
`INC/DEC r/(HL)/rr` `DAA` (BCD adjust) `NEG` `CPL` `CCF` `SCF`

## Rotates / shifts
`RLCA RLCA` variants: `RLCA RRCA RLA RRA` (A only), `RLC/RRC/RL/RR/SLA/SRA/SLL/SRL m` (m: r (HL) (IX+d)), `RLD/RRD` (BCD via HL)

## Control flow
`JP nn` `JP cc,nn` `JR e` `JR cc,e` `DJNZ e` `CALL nn` `CALL cc,nn` `RET` `RET cc` `RETI` `RETN` `RST p` (p: 00 08 10 18 20 28 30 38)
Conditions: NZ Z NC C PO PE P M

## I/O, CPU
`IN A,(n)` `IN r,(C)` `OUT (n),A` `OUT (C),r` `DI/EI` `IM 0/1/2` `HALT` `NOP` `SET/RES b,m` `BIT b,m` (b: 0-7)

## Common timings (T-states)
4 NOP · 13 CALL · 10 RET · 7 JR taken · 12 JR not taken · 21 LDIR iteration · 4+16 IM1 interrupt.
Timing-critical effects: every instruction matters; use `inspect_state aspects:["timing"]` and the profiler tools.

## Pointers into this emulator
- Disassembly: inspect_state aspects:["disasm"] or debug_code action:"disassemble".
- Assembling patches: debug_code action:"assemble".
)md";

constexpr const char* kTrdosCommands = R"md(# TR-DOS 5.03 / Beta Disk Interface Reference

Enter TR-DOS from 128K BASIC: `RANDOMIZE USR 15616` (or the `cat` macro via type_input). Drives A/B, up to 80 tracks,
16 sectors/track, 256 bytes/sector (TRD image = 655360 bytes for 2 sides).

## Commands
| Command | Purpose |
|:--|:--|
| CAT (or `.`) | Catalog listing |
| FORMAT "name" | Initialize disk |
| SAVE *"name" CODE start,len | Save memory block |
| SAVE *"name" LINE n | Save BASIC with autostart |
| LOAD *"name" | Load BASIC program |
| LOAD *"name" CODE [addr] | Load code block (optionally relocated) |
| LOAD *"name" DATA v$() | Load array |
| MERGE *"name" | Merge BASIC |
| VERIFY *"name" | Verify file |
| ERASE "name" | Delete file |
| MOVE "old" TO "new" | Rename file |
| COPY | File/disk copy utility |
| RUN *"name" | Load and run |
| CAT 1 / CAT 2 | Extended catalog (per drive) |

## TRD file system facts
- Directory: 128 entries at track 0, sectors 1-8 (2048 bytes).
- File types: BASIC (48), NUMBER ARRAY (49), CHAR ARRAY (50), CODE (51+; start/length in directory).
- Disk parameters block at 0x1E1E (894): free sectors, first free track/sector, file count, free directory slots.
- System tracks: 0-1 reserved (SERVICE sector at track 0 sector 8).

## Via WebAPI instead of typing
- Disk catalog: invoke_api GET /api/v1/emulator/{id}/disk/A/catalog
- Disk info/sysinfo: GET .../disk/A/info, .../disk/A/sysinfo
- Raw sectors: GET .../disk/A/sector/{cyl}/{side}/{sec}
)md";

constexpr const char* kMemoryMap = R"md(# ZX Spectrum Memory Map

## 48K address space
| Range | Size | Content |
|:--|:--|:--|
| 0x0000-0x3FFF | 16K | ROM (fixed; 128K bankable later models) |
| 0x4000-0x5AFF | 6912B | Screen bitmap: 0x4000-0x57FF bitmap (6144B), 0x5800-0x5AFF attributes (768B) |
| 0x5B00-0x5BFF | 256B | System variables (SYSVARS area from 0x5C00 down: CHARS 0x5C36...) |
| 0x5C00 | — | VARS start (BASIC variables) |
| 0x8000 (typ.) | — | BASIC program area / user code (RAMTOP up to 0xFF57 stock) |
| 0xFF58-0xFFFF | 136B | Printer buffer (0x5B00), stack below RAMTOP, spare |

System area: 0x4000-0x5BFF is the "lower 16K" fixed screen/vars area on all models.

## Screen layout quirk
Bitmap thirds (0x4000/0x4800/0x5800−256... precisely: thirds at 0x4000, 0x4800, 0x5000), each third has 8 character
rows; within a row, scanlines are interleaved in 256-byte steps. Address = base | (y&7)<<8 | (y&0x38)<<2 | (y&0xC0)<<5 | x>>3.
Attributes: 0x5800 + (y<<5) + (x>>3); byte = FLASH BRIGHT PAPER(0-2) INK(0-2) → 0xF8 paper | 0x07 ink.

## 128K / +2 paging (ports 0x7FFD value bits)
| Bit | Meaning |
|:--|:--|
| 0-2 | RAM bank at 0xC000 (banks 0-7; bank 3 at 0xC000 default, banks 5/2 fixed: 0x4000=5, 0x8000=2) |
| 3 | Screen select: 0 → bank 5, 1 → bank 7 (shadow screen) |
| 4 | ROM select: 0 → 48K editor ROM (bank 0), 1 → 128K editor (bank 1) |
| 5 | Paging disabled when set (write until reset) |
| 6-7 | unused |

Write with bits 0x10 set twice pattern: `LD BC,0x7FFD: LD A,value: OUT (C),A` — note bit 3 shadow screen enables
double-buffering (screen_digest hashes banks 5 and 7 for exactly this reason).

## Pentagon specifics
64/128/256/512K: paging via port 0x7FFD (same bits) + 0x EFF7 for >128K extensions; 71680 T-states/frame (320 lines)
vs Sinclair 69888 (311 lines) — 2.27% faster frame; INT every 71680 T; no M1 wait contended differences.

## Emulator notes
- Memory inspection: inspect_state aspects:["memory"] (CPU view) or aspects:["memory_banks"] (bank state).
- Bank pages: invoke_api GET /api/v1/emulator/{id}/memory/page/ram/{n}.
)md";

struct StaticResource
{
    const char* uri;
    const char* name;
    const char* description;
    const char* mimeType;
    const char* content;
};

const StaticResource kStaticResources[] = {
    {"unreal://keyboard-layout", "keyboard-layout", "ZX Spectrum keyboard: exact key names for type_input, BASIC keyword entry, named macros", "text/markdown", kKeyboardLayout},
    {"unreal://basic-reference", "basic-reference", "ZX BASIC quick reference: commands, colors, system variables, entry points", "text/markdown", kBasicReference},
    {"unreal://z80-isa", "z80-isa", "Z80 instruction set reference: load/arithmetic/rotate/control-flow blocks and T-state notes", "text/markdown", kZ80Isa},
    {"unreal://trdos-commands", "trdos-commands", "TR-DOS 5.03 commands, TRD file system layout, disk inspection via WebAPI", "text/markdown", kTrdosCommands},
    {"unreal://memory-map", "memory-map", "48K/128K/Pentagon memory maps, screen layout math, 0x7FFD paging bits", "text/markdown", kMemoryMap},
};

constexpr size_t kStaticResourceCount = sizeof(kStaticResources) / sizeof(kStaticResources[0]);

} // namespace

/// endregion </Embedded content>

/// region <McpResources>

Json::Value McpResources::ListJson()
{
    Json::Value resources(Json::arrayValue);
    for (size_t i = 0; i < kStaticResourceCount; ++i)
    {
        const StaticResource& resource = kStaticResources[i];
        Json::Value entry;
        entry["uri"] = resource.uri;
        entry["name"] = resource.name;
        entry["description"] = resource.description;
        entry["mimeType"] = resource.mimeType;
        resources.append(entry);
    }

    Json::Value state;
    state["uri"] = "unreal://emulator-state";
    state["name"] = "emulator-state";
    state["description"] = "Live overview of all running emulator instances (dynamic)";
    state["mimeType"] = "text/markdown";
    resources.append(state);

    return resources;
}

void McpResources::Read(const std::string& uri, IApiCaller& caller, ReadCallback done)
{
    for (size_t i = 0; i < kStaticResourceCount; ++i)
    {
        const StaticResource& resource = kStaticResources[i];
        if (uri == resource.uri)
        {
            Json::Value content;
            content["uri"] = uri;
            content["mimeType"] = resource.mimeType;
            content["text"] = resource.content;

            Json::Value result;
            result["contents"].append(std::move(content));
            done(true, std::move(result));
            return;
        }
    }

    if (uri == "unreal://emulator-state")
    {
        caller.Call("GET", "/api/v1/emulator", nullptr, [done](int status, Json::Value body) {
            if (status != 200 || !body.isObject() || !body["emulators"].isArray())
            {
                done(false, Json::Value("Cannot fetch emulator state (HTTP " + std::to_string(status) +
                                            "). Is the emulator process running?"));
                return;
            }

            std::ostringstream out;
            out << "# Emulator instances\n\n";
            const Json::Value& emulators = body["emulators"];
            if (emulators.size() == 0)
            {
                out << "No instances. Create one: emulator_manage action:'create' model:'128k'.\n";
            }
            for (Json::ArrayIndex i = 0; i < emulators.size(); ++i)
            {
                out << "- **" << emulators[i]["id"].asString() << "** — state: " << emulators[i]["state"].asString()
                    << ", running: " << (emulators[i]["is_running"].asBool() ? "yes" : "no")
                    << ", debug: " << (emulators[i]["is_debug"].asBool() ? "yes" : "no") << "\n";
            }
            out << "\nUse 'target' with these ids (or 'auto' when only one instance exists).\n";

            Json::Value content;
            content["uri"] = "unreal://emulator-state";
            content["mimeType"] = "text/markdown";
            content["text"] = out.str();

            Json::Value result;
            result["contents"].append(std::move(content));
            done(true, std::move(result));
        });
        return;
    }

    done(false, Json::Value("Unknown resource: " + uri));
}

/// endregion </McpResources>

} // namespace mcp
