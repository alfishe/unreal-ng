#include "basicencoder.h"
#include "emulator/memory/memory.h"
#include "emulator/emulator.h"
#include "emulator/spectrumconstants.h"
#include "emulator/io/keyboard/keyboard.h"
#include "3rdparty/message-center/messagecenter.h"

#include <sstream>
#include <cctype>
#include <algorithm>

// Initialize the keyword-to-token mapping
// Multi-word keywords MUST come before their single-word components
const std::map<std::string, uint8_t> BasicEncoder::BasicKeywords = {
    // Multi-word keywords first (longest match)
    {" DEF FN ", 0xCE},
    {" GO TO ", 0xEC},
    {" GO SUB ", 0xED},
    {" OPEN #", 0xD3},
    {" CLOSE #", 0xD4},
    
    // Single keywords (alphabetically for clarity)
    {" ABS ", 0xBD},
    {" ACS ", 0xB6},
    {" AND ", 0xC6},
    {" ASN ", 0xB5},
    {" AT ", 0xAC},
    {" ATN ", 0xB7},
    {" ATTR ", 0xAB},
    {" BEEP ", 0xD7},
    {" BIN ", 0xC4},
    {" BORDER ", 0xE7},
    {" BRIGHT ", 0xDC},
    {" CAT ", 0xCF},
    {" CHR$ ", 0xC2},
    {" CIRCLE ", 0xD8},
    {" CLEAR ", 0xFD},
    {" CLS ", 0xFB},
    {" CODE ", 0xAF},
    {" CONTINUE ", 0xE8},
    {" COPY ", 0xFF},
    {" COS ", 0xB3},
    {" DATA ", 0xE4},
    {" DIM ", 0xE9},
    {" DRAW ", 0xFC},
    {" ERASE ", 0xD2},
    {" EXP ", 0xB9},
    {" FLASH ", 0xDB},
    {" FN ", 0xA8},
    {" FOR ", 0xEB},
    {" FORMAT ", 0xD0},
    {" IF ", 0xFA},
    {" IN ", 0xBF},
    {"INKEY$", 0xA6},
    {" INK ", 0xD9},
    {" INPUT ", 0xEE},
    {" INT ", 0xBA},
    {" INVERSE ", 0xDD},
    {" LEN ", 0xB1},
    {" LET ", 0xF1},
    {" LINE ", 0xCA},
    {" LIST ", 0xF0},
    {" LLIST ", 0xE1},
    {" LN ", 0xB8},
    {" LOAD ", 0xEF},
    {" LPRINT ", 0xE0},
    {" MERGE ", 0xD5},
    {" MOVE ", 0xD1},
    {" NEW ", 0xE6},
    {" NEXT ", 0xF3},
    {" NOT ", 0xC3},
    {" OR ", 0xC5},
    {" OUT ", 0xDF},
    {" OVER ", 0xDE},
    {" PAPER ", 0xDA},
    {" PAUSE ", 0xF2},
    {" PEEK ", 0xBE},
    {"PI", 0xA7},
    {" PLAY ", 0xA4},
    {" PLOT ", 0xF6},
    {" POINT ", 0xA9},
    {" POKE ", 0xF4},
    {" PRINT ", 0xF5},
    {" RANDOMIZE ", 0xF9},
    {" READ ", 0xE3},
    {" REM ", 0xEA},
    {" RESTORE ", 0xE5},
    {" RETURN ", 0xFE},
    {"RND", 0xA5},
    {" RUN ", 0xF7},
    {" SAVE ", 0xF8},
    {"SCREEN$ ", 0xAA},
    {" SGN ", 0xBC},
    {" SIN ", 0xB2},
    {" SPECTRUM ", 0xA3},
    {" SQR ", 0xBB},
    {" STEP ", 0xCD},
    {" STOP ", 0xE2},
    {"STR$ ", 0xC1},
    {" TAB ", 0xAD},
    {" TAN ", 0xB4},
    {" THEN ", 0xCB},
    {" TO ", 0xCC},
    {" USR ", 0xC0},
    {"VAL$ ", 0xAE},
    {" VAL ", 0xB0},
    {" VERIFY ", 0xD6},
    
    // Operators
    {"<=", 0xC7},
    {">=", 0xC8},
    {"<>", 0xC9}
};

std::vector<uint8_t> BasicEncoder::tokenize(const std::string& basicText)
{
    std::vector<uint8_t> result;
    std::istringstream iss(basicText);
    std::string line;
    
    while (std::getline(iss, line))
    {
        // Skip empty lines
        if (trim(line).empty())
            continue;
            
        // Parse line number
        size_t pos = 0;
        while (pos < line.length() && std::isspace(line[pos]))
            pos++;
            
        if (pos >= line.length() || !std::isdigit(line[pos]))
            continue;  // Skip lines without line numbers
            
        // Extract line number
        size_t numEnd = pos;
        while (numEnd < line.length() && std::isdigit(line[numEnd]))
            numEnd++;
            
        uint16_t lineNumber = std::stoi(line.substr(pos, numEnd - pos));
        
        // Extract line text (everything after line number)
        std::string lineText = line.substr(numEnd);
        
        // Tokenize this line
        auto tokenizedLine = tokenizeLine(lineNumber, lineText);
        result.insert(result.end(), tokenizedLine.begin(), tokenizedLine.end());
    }
    
    return result;
}

std::vector<uint8_t> BasicEncoder::tokenizeLine(uint16_t lineNumber, const std::string& lineText)
{
    std::vector<uint8_t> result;
    
    // Line number (big-endian)
    result.push_back((lineNumber >> 8) & 0xFF);
    result.push_back(lineNumber & 0xFF);
    
    // Tokenize the line content
    auto lineData = replaceKeywords(lineText);
    
    // Line length (little-endian) - includes line data + terminator
    uint16_t lineLength = lineData.size() + 1;  // +1 for 0x0D terminator
    result.push_back(lineLength & 0xFF);
    result.push_back((lineLength >> 8) & 0xFF);
    
    // Line data
    result.insert(result.end(), lineData.begin(), lineData.end());
    
    // Line terminator
    result.push_back(LINE_END);
    
    return result;
}

std::vector<uint8_t> BasicEncoder::replaceKeywords(const std::string& text)
{
    std::vector<uint8_t> result;
    std::string upperText = toUpper(text);
    size_t pos = 0;
    bool inString = false;
    
    while (pos < upperText.length())
    {
        // Handle string literals - preserve as-is
        if (upperText[pos] == '"')
        {
            result.push_back(text[pos]);
            inString = !inString;
            pos++;
            continue;
        }
        
        if (inString)
        {
            result.push_back(text[pos]);
            pos++;
            continue;
        }
        
        // Try to match keywords (longest match first)
        bool matched = false;
        for (const auto& [keyword, token] : BasicKeywords)
        {
            size_t keyLen = keyword.length();
            if (pos + keyLen <= upperText.length())
            {
                std::string candidate = upperText.substr(pos, keyLen);
                if (candidate == keyword)
                {
                    result.push_back(token);
                    pos += keyLen;
                    matched = true;
                    break;
                }
            }
        }
        
        if (!matched)
        {
            // Not a keyword, copy character as-is
            result.push_back(text[pos]);
            pos++;
        }
    }
    
    return result;
}

/// Tokenize immediate commands with proper word boundary checking
/// Unlike replaceKeywords (designed for numbered program lines with " KEYWORD " format),
/// this method handles immediate commands where keywords can appear at line start
/// without requiring space prepending hacks. Uses word boundary detection.
std::vector<uint8_t> BasicEncoder::tokenizeImmediate(const std::string& command)
{
    std::vector<uint8_t> result;
    std::string upperCmd = toUpper(command);
    size_t pos = 0;
    bool inString = false;
    
    while (pos < upperCmd.length())
    {
        // Handle string literals - preserve as-is
        if (upperCmd[pos] == '"')
        {
            result.push_back(command[pos]);
            inString = !inString;
            pos++;
            continue;
        }
        
        if (inString)
        {
            result.push_back(command[pos]);
            pos++;
            continue;
        }
        
        // Try to match keywords with proper boundary checking
        bool matched = false;
        for (const auto& [keyword, token] : BasicKeywords)
        {
            // Strip spaces from keyword table entry
            std::string trimmedKeyword = trim(keyword);
            size_t keyLen = trimmedKeyword.length();
            
            if (pos + keyLen <= upperCmd.length())
            {
                std::string candidate = upperCmd.substr(pos, keyLen);
                
                if (candidate == trimmedKeyword)
                {
                    // Check word boundaries
                    bool isWordStart = (pos == 0 || !std::isalnum(upperCmd[pos-1]));
                    bool isWordEnd = (pos + keyLen >= upperCmd.length() || !std::isalnum(upperCmd[pos + keyLen]));
                    
                    if (isWordStart && isWordEnd)
                    {
                        result.push_back(token);
                        pos += keyLen;
                        
                        // CRITICAL: If keyword table entry had trailing space, skip it in input too
                        // This prevents "PRINT 1" from becoming [0xF5, ' ', '1'] instead of [0xF5, '1']
                        if (keyword.back() == ' ' && pos < upperCmd.length() && upperCmd[pos] == ' ')
                        {
                            pos++;  // Skip the trailing space
                        }
                        
                        matched = true;
                        break;
                    }
                }
            }
        }
        
        if (!matched)
        {
            // Not a keyword, copy character as-is
            result.push_back(command[pos]);
            pos++;
        }
    }
    
    return result;
}

bool BasicEncoder::injectIntoMemory(Memory* memory, 
                                     const std::vector<uint8_t>& tokenizedProgram,
                                     uint16_t progStart)
{
    if (!memory || tokenizedProgram.empty())
        return false;
        
    // Sanity check: program size
    if (tokenizedProgram.size() > 0xC000)  // Max reasonable size
        return false;
        
    // Write program to memory
    for (size_t i = 0; i < tokenizedProgram.size(); i++)
    {
        memory->DirectWriteToZ80Memory(progStart + i, tokenizedProgram[i]);
    }
    
    
    // Update system variables
    // Program end is immediately after the last tokenized byte
    // Note: ZX Spectrum BASIC does NOT use a 0x00 0x00 terminator.
    // The end of program is defined strictly by the VARS system variable.
    size_t progSize = tokenizedProgram.size();
    uint16_t progEnd = progStart + progSize;
    updateSystemVariables(memory, progStart, progEnd);
    
    return true;
}

bool BasicEncoder::loadProgram(Memory* memory, 
                                const std::string& basicText,
                                uint16_t progStart)
{
    auto tokenized = tokenize(basicText);
    if (tokenized.empty())
        return false;
        
    return injectIntoMemory(memory, tokenized, progStart);
}

void BasicEncoder::updateSystemVariables(Memory* memory, uint16_t progStart, uint16_t progEnd)
{
    using namespace SystemVariables48k;
    
    // PROG: Start of BASIC program
    writeWord(memory, PROG, progStart);
    
    // VARS: Start of variables area (immediately after program)
    // Write 0x80 to mark end of variables (empty for now)
    memory->DirectWriteToZ80Memory(progEnd, 0x80);
    writeWord(memory, VARS, progEnd);
    
    // E_LINE: Editor line buffer (after variables marker)
    uint16_t eLine = progEnd + 1;
    writeWord(memory, E_LINE, eLine);
    
    // Write empty editor line: just 0x0D terminator and newline marker
    memory->DirectWriteToZ80Memory(eLine, 0x0D);
    memory->DirectWriteToZ80Memory(eLine + 1, 0x80);  // Newline marker
    
    // WORKSP: Work space (after E_LINE)
    uint16_t worksp = eLine + 2;
    writeWord(memory, WORKSP, worksp);
    
    // STKBOT: Bottom of calculator stack
    writeWord(memory, STKBOT, worksp);
    
    // STKEND: End of used memory
    writeWord(memory, STKEND, worksp);
    
    // NXTLIN: Next line to execute (set to start of program for RUN)
    writeWord(memory, NXTLIN, progStart);
    
    // CH_ADD: Character address (set to start of program)
    writeWord(memory, CH_ADD, progStart);
    
    // Clear ERR_NR (no error)
    memory->DirectWriteToZ80Memory(ERR_NR, 0xFF);
}

void BasicEncoder::writeWord(Memory* memory, uint16_t address, uint16_t value)
{
    memory->DirectWriteToZ80Memory(address, value & 0xFF);
    memory->DirectWriteToZ80Memory(address + 1, (value >> 8) & 0xFF);
}

std::string BasicEncoder::trim(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::string BasicEncoder::toUpper(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

void BasicEncoder::injectKeypress(Memory* memory, uint8_t keyCode)
{
    if (!memory)
        return;
        
    using namespace SystemVariables48k;
    
    // Write key code to LAST_K
    memory->DirectWriteToZ80Memory(LAST_K, keyCode);
    
    // Set FLAGS bit 5 to indicate new key available
    uint8_t flags = memory->DirectReadFromZ80Memory(FLAGS);
    memory->DirectWriteToZ80Memory(FLAGS, flags | 0x20);
}

void BasicEncoder::injectEnter(Memory* memory)
{
    injectKeypress(memory, 0x0D);  // ENTER = 0x0D
}

void BasicEncoder::injectText(Memory* memory, const std::string& text)
{
    if (!memory)
        return;
        
    for (char c : text)
    {
        injectKeypress(memory, static_cast<uint8_t>(c));
    }
}

/// @brief Check if TR-DOS system variables have been initialized
/// 
/// TR-DOS initialization is detected by:
/// - RAM stub at $5CC2 contains RET opcode ($C9)
/// - CHANS system variable equals $5D25 (TR-DOS extends channel area)
///
/// @param memory Pointer to emulator memory instance
/// @return true if TR-DOS has been entered at least once this session
bool BasicEncoder::isTRDOSInitialized(Memory* memory)
{
    if (!memory)
        return false;
    
    using namespace TRDOS::ROMSwitch;
    using namespace SystemVariables48k;
    
    // Check for RAM stub containing RET instruction
    uint8_t stubValue = memory->DirectReadFromZ80Memory(RAM_STUB);
    if (stubValue != RAM_STUB_OPCODE)
        return false;
    
    // Check CHANS value - TR-DOS extends system variables to $5D25
    uint16_t chansValue = memory->DirectReadFromZ80Memory(CHANS) |
                          (memory->DirectReadFromZ80Memory(CHANS + 1) << 8);
    
    return (chansValue == CHANS_TRDOS_VALUE);
}

/// @brief Scan stack for TR-DOS trap return addresses ($3D00-$3DFF)
/// 
/// When TR-DOS temporarily calls SOS ROM (for printing, keyboard, etc.),
/// the stack contains return addresses in the trap range. Finding such
/// addresses indicates DOS is "logically active" even though SOS ROM is paged.
///
/// @param memory Pointer to emulator memory instance
/// @param z80SP Current Z80 stack pointer
/// @param maxDepth Maximum stack entries to scan (default 16)
/// @return true if stack contains DOS return address, false to fallback to HW detection
bool BasicEncoder::stackContainsDOSReturnAddress(Memory* memory, uint16_t z80SP, int maxDepth)
{
    if (!memory)
        return false;
    
    // Stack validation: SP must be in valid RAM range
    // Stack typically lives in upper RAM ($C000-$FFFF or $8000-$BFFF)
    // SP < $4000 means stack in ROM area - invalid/garbage
    if (z80SP < 0x4000)
        return false;
    
    // SP near top of memory with no room for even one entry - invalid
    if (z80SP >= 0xFFFE)
        return false;
    
    using namespace TRDOS::ROMSwitch;
    
    // Garbage detection counters
    int zeroCount = 0;       // Consecutive $0000 entries
    int suspiciousCount = 0; // Entries that look like garbage (e.g., $FFFF, repeated patterns)
    
    // Scan stack for trap range return addresses
    for (int i = 0; i < maxDepth; i++)
    {
        uint16_t stackAddr = z80SP + (i * 2);
        
        // Don't read beyond valid RAM
        if (stackAddr >= 0xFFFE)
            break;
        
        // Read 16-bit return address from stack (little-endian)
        uint16_t retAddr = memory->DirectReadFromZ80Memory(stackAddr) |
                           (memory->DirectReadFromZ80Memory(stackAddr + 1) << 8);
        
        // Garbage detection: too many zeros indicates uninitialized memory
        if (retAddr == 0x0000)
        {
            zeroCount++;
            if (zeroCount >= 4)
            {
                // Stack looks uninitialized - don't trust it
                return false;
            }
            continue;
        }
        else
        {
            zeroCount = 0;  // Reset counter on non-zero value
        }
        
        // Garbage detection: $FFFF is suspicious (uninitialized RAM)
        if (retAddr == 0xFFFF)
        {
            suspiciousCount++;
            if (suspiciousCount >= 3)
            {
                // Too much garbage - don't trust stack
                return false;
            }
            continue;
        }
        
        // Check if return address is in TR-DOS trap range
        if (retAddr >= TRAP_START && retAddr <= TRAP_END)
        {
            return true;  // Found DOS return address on stack
        }
        
        // Continue scanning - DOS return address may be deeper in stack
        // (e.g., when SOS is calling its own subroutines)
    }
    
    return false;
}

/// @brief Helper: Check if address looks like a plausible return address
/// @param addr 16-bit address to check
/// @return true if address could reasonably be on a stack
static bool isPlausibleReturnAddress(uint16_t addr)
{
    // ROM area (valid code region, excluding high ROM data areas)
    if (addr < 0x3E00)
        return true;
    
    // TR-DOS trap range
    if (addr >= 0x3D00 && addr <= 0x3DFF)
        return true;
    
    // RAM trampolines in system variable area ($5C00-$5E00)
    if (addr >= 0x5C00 && addr < 0x5E00)
        return true;
    
    // RAM program/code area (typical BASIC/code locations)
    if (addr >= 0x5E00 && addr < 0xFF00)
        return true;
    
    return false;
}

/// @brief Verify stack contains plausible return addresses (sanity check)
/// 
/// Checks that first few stack entries look like valid ROM/RAM addresses
/// rather than garbage data. Helps avoid false positives when stack is
/// corrupted or uninitialized.
///
/// @param memory Pointer to emulator memory instance
/// @param z80SP Current Z80 stack pointer
/// @param checkDepth Number of entries to check (default 4)
/// @return true if stack appears valid, false if garbage detected
bool BasicEncoder::isStackSane(Memory* memory, uint16_t z80SP, int checkDepth)
{
    if (!memory)
        return false;
    
    // Stack validation: SP must be in valid RAM range
    if (z80SP < 0x4000 || z80SP >= 0xFFFE)
        return false;
    
    int plausibleCount = 0;
    int garbageCount = 0;
    
    for (int i = 0; i < checkDepth; i++)
    {
        uint16_t stackAddr = z80SP + (i * 2);
        if (stackAddr >= 0xFFFE)
            break;
        
        uint16_t retAddr = memory->DirectReadFromZ80Memory(stackAddr) |
                           (memory->DirectReadFromZ80Memory(stackAddr + 1) << 8);
        
        // Check for obvious garbage patterns
        if (retAddr == 0x0000 || retAddr == 0xFFFF)
        {
            garbageCount++;
            continue;
        }
        
        // Check if this looks like a reasonable return address
        if (isPlausibleReturnAddress(retAddr))
        {
            plausibleCount++;
        }
        else
        {
            garbageCount++;
        }
    }
    
    // Stack is sane if we found at least one plausible address
    // and garbage doesn't dominate
    return (plausibleCount > 0) && (plausibleCount >= garbageCount);
}

/// @brief Check if TR-DOS is logically active (even during temporary SOS calls)
/// 
/// Uses three-tier detection algorithm:
/// - Tier 1: DOS ROM hardware paging state (instant, definitive)
/// - Tier 2: Stack context analysis (detects SOS calls from DOS)  
/// - Tier 3: System variable initialization check (prerequisite)
///
/// @param memory Pointer to emulator memory instance
/// @param z80SP Current Z80 stack pointer
/// @return true if TR-DOS is in control (actively or via SOS call)
bool BasicEncoder::isTRDOSLogicallyActive(Memory* memory, uint16_t z80SP)
{
    if (!memory)
        return false;
    
    // TIER 1: Check hardware ROM paging state
    // If DOS ROM is paged, TR-DOS is definitely active
    uint16_t romPage = memory->GetROMPage();
    
    // ROM page 2 = TR-DOS ROM (on Pentagon/Scorpion with Beta 128)
    // Note: This depends on machine configuration
    // TODO: Add proper Beta 128 paging state detection via port $FF mirror
    if (romPage == 2)
    {
        return true;  // DOS ROM is physically paged in
    }
    
    // TIER 3 (checked before Tier 2): Verify TR-DOS was ever initialized
    if (!isTRDOSInitialized(memory))
    {
        return false;  // TR-DOS was never entered this session
    }
    
    // TIER 2: Stack context analysis
    // TR-DOS may be temporarily using SOS ROM for I/O (printing, keyboard, etc.)
    // The stack will contain return addresses in the trap range ($3D00-$3DFF)
    return stackContainsDOSReturnAddress(memory, z80SP);
}

/// @brief Detect current BASIC/DOS state based on ROM page, stack, and system variables
/// 
/// Detection Logic (Three-Tier):
/// =============================
/// 
/// TIER 1: TR-DOS HARDWARE CHECK
///   - If DOS ROM paged → TRDOS_Active
///   - If SOS ROM paged but stack contains $3D00-$3DFF → TRDOS_SOS_Call
/// 
/// TIER 2: BASIC ROM PAGE CHECK
///   - Pages 1, 3 = 48K BASIC ROM → Basic48K
///   - Pages 0, 2 = 128K Editor/Menu ROM → continue to step 3
///
/// TIER 3: 128K EDITOR STATE CHECK ($EC0D in RAM bank 7)
///   - Bit 1 SET = Menu displayed → Menu128K
///   - Bit 1 CLEAR = In BASIC editor → Basic128K
///
/// Key Discoveries:
/// ================
/// - TR-DOS frequently calls SOS ROM for I/O (printing, keyboard)
/// - During these calls, SOS ROM is paged but DOS is still "in control"
/// - Stack analysis reveals DOS activity: look for $3D2F return addresses
/// - RAM stub at $5CC2 contains $C9 (RET) when TR-DOS initialized
///
BasicEncoder::BasicState BasicEncoder::detectState(Memory* memory, uint16_t z80SP)
{
    if (!memory)
        return BasicState::Unknown;
    
    // =========================================================================
    // TIER 1: TR-DOS Detection (highest priority)
    // =========================================================================
    
    uint16_t romPage = memory->GetROMPage();
    
    // Pentagon ROM layout:
    //   Page 0: Service ROM
    //   Page 1: TR-DOS ROM  <-- TR-DOS is here
    //   Page 2: 128K Editor/Menu ROM
    //   Page 3: 48K BASIC ROM (SOS)
    //
    // Check if TR-DOS ROM is physically paged (page 1 on Pentagon/Scorpion)
    if (romPage == 1)
    {
        // TR-DOS ROM is active
        return BasicState::TRDOS_Active;
    }
    
    // Check if TR-DOS is calling SOS ROM temporarily
    // This happens during printing, keyboard scanning, etc.
    // Only trust stack analysis if the stack looks valid
    if (z80SP != 0 && isTRDOSInitialized(memory) && isStackSane(memory, z80SP))
    {
        if (stackContainsDOSReturnAddress(memory, z80SP))
        {
            // SOS ROM is paged but DOS is waiting on stack
            return BasicState::TRDOS_SOS_Call;
        }
    }
    
    // =========================================================================
    // TIER 2: 48K BASIC ROM Detection (page 3 = SOS ROM on Pentagon)
    // =========================================================================
    
    if (romPage == 3)
    {
        // SOS ROM (48K BASIC) is active
        // BUT: TR-DOS frequently pages in SOS ROM for I/O (keyboard, printing)
        // If TR-DOS has been initialized (CHANS=$5D25, stub at $5CC2), 
        // assume we're in TR-DOS mode calling SOS
        if (isTRDOSInitialized(memory))
        {
            return BasicState::TRDOS_Active;  // TR-DOS calling SOS ROM
        }
        
        // True 48K BASIC mode
        return BasicState::Basic48K;
    }
    
    // =========================================================================
    // TIER 3: 128K Editor/Menu Detection (ROM pages 0, 2)
    // =========================================================================
    
    // Check which RAM bank is paged at $C000-$FFFF (BANK_M bits 0-2)
    uint8_t bankM = memory->DirectReadFromZ80Memory(0x5B5C);
    uint8_t upperBank = bankM & 0x07;  // Bits 0-2 = RAM bank at $C000
    
    if (upperBank != 7)
    {
        // Bank 7 not paged, can't read editor variables
        // Must be in menu or early boot - assume menu
        return BasicState::Menu128K;
    }
    
    // Bank 7 IS paged - now safe to read EDITOR_FLAGS
    uint8_t* ramBank7 = memory->RAMPageAddress(7);
    if (!ramBank7)
    {
        return BasicState::Unknown;
    }
    
    // Check EDITOR_FLAGS ($EC0D in RAM bank 7)
    constexpr uint16_t EDITOR_FLAGS_OFFSET = Editor128K::EDITOR_FLAGS - 0xC000;  // $2C0D
    uint8_t editorFlags = ramBank7[EDITOR_FLAGS_OFFSET];
    
    // Bit 1: 1 = Menu displayed, 0 = In BASIC editor
    if (editorFlags & 0x02)
    {
        return BasicState::Menu128K;
    }
    
    return BasicState::Basic128K;
}

bool BasicEncoder::isInBasicEditor(Memory* memory)
{
    BasicState state = detectState(memory);
    return (state == BasicState::Basic128K || state == BasicState::Basic48K);
}

void BasicEncoder::navigateToBasic128K(Emulator* emulator)
{
    if (!emulator)
        return;
    
    Memory* memory = emulator->GetMemory();
    if (!memory)
        return;
    
    // When 128K menu is displayed, RAM bank 7 is paged at $C000-$FFFF
    // Set menu index $EC0C to 1 (128 BASIC)
    // Menu items: 0=Tape Loader, 1=128 BASIC, 2=Calculator, 3=48 BASIC, 4=Tape Tester
    memory->DirectWriteToZ80Memory(EC0D_FLAGS - 1, 0x01);  // EC0C = menu index = 1
    
    // Inject ENTER keypress to trigger menu selection
    // The emulator will process this on the next frame naturally
    injectKeypress(memory, 0x0D);  // 0x0D = ENTER
    
    // Note: Menu transition is asynchronous - caller should poll for state change
    // or wait a few frames before injecting commands
}

void BasicEncoder::navigateToBasic48K(Emulator* emulator)
{
    if (!emulator)
        return;
    
    Memory* memory = emulator->GetMemory();
    if (!memory)
        return;
    
    // When 128K menu is displayed, RAM bank 7 is paged at $C000-$FFFF
    // Set menu index $EC0C to 3 (48 BASIC)
    // Menu items: 0=Tape Loader, 1=128 BASIC, 2=Calculator, 3=48 BASIC, 4=Tape Tester
    memory->DirectWriteToZ80Memory(EC0D_FLAGS - 1, 0x03);  // EC0C = menu index = 3
    
    // Inject ENTER keypress to trigger menu selection
    // The emulator will process this on the next frame naturally
    injectKeypress(memory, 0x0D);  // 0x0D = ENTER
    
    // Note: Menu transition is asynchronous - caller should poll for state change
}

void BasicEncoder::navigateToTapeLoader(Memory* memory)
{
    if (!memory)
        return;
    
    // Menu item 0: Tape Loader
    memory->DirectWriteToZ80Memory(EC0D_FLAGS - 1, 0x00);  // EC0C = menu index = 0
    injectKeypress(memory, 0x0D);  // 0x0D = ENTER
}

void BasicEncoder::navigateToCalculator(Memory* memory)
{
    if (!memory)
        return;
    
    // Menu item 2: Calculator
    memory->DirectWriteToZ80Memory(EC0D_FLAGS - 1, 0x02);  // EC0C = menu index = 2
    injectKeypress(memory, 0x0D);  // 0x0D = ENTER
}

void BasicEncoder::navigateToTRDOS(Memory* memory)
{
    if (!memory)
        return;
    
    // Menu item 4: Tape Tester (TR-DOS on Pentagon)
    memory->DirectWriteToZ80Memory(EC0D_FLAGS - 1, 0x04);  // EC0C = menu index = 4
    injectKeypress(memory, 0x0D);  // 0x0D = ENTER
}

// ============================================================================
// ROM-Specific Injection Functions
// ============================================================================

BasicEncoder::InjectionResult BasicEncoder::injectTo48K(Memory* memory, const std::string& command)
{
    InjectionResult result;
    result.success = false;
    result.state = BasicState::Basic48K;
    
    if (!memory)
    {
        result.message = "Error: Memory not available";
        return result;
    }
    
    using namespace SystemVariables48k;
    
    // Tokenize the command (keywords -> single byte tokens)
    auto tokenized = BasicEncoder::tokenizeImmediate(command);
    
    // Read E_LINE address from system variables
    uint16_t eLineAddr = memory->DirectReadFromZ80Memory(E_LINE) |
                         (memory->DirectReadFromZ80Memory(E_LINE + 1) << 8);
    
    if (eLineAddr < 0x5B00 || eLineAddr > 0xFF00)
    {
        result.message = "Error: Invalid E_LINE address";
        return result;
    }
    
    // Write tokenized command to E_LINE buffer
    for (size_t i = 0; i < tokenized.size(); i++)
    {
        memory->DirectWriteToZ80Memory(eLineAddr + i, tokenized[i]);
    }
    
    // Terminate with ENTER (0x0D)
    memory->DirectWriteToZ80Memory(eLineAddr + tokenized.size(), 0x0D);
    // End marker (0x80 tells ROM this is end of edit area)
    memory->DirectWriteToZ80Memory(eLineAddr + tokenized.size() + 1, 0x80);
    
    // Set WORKSP to point after the E-LINE buffer content
    uint16_t workspVal = eLineAddr + tokenized.size() + 2;
    memory->DirectWriteToZ80Memory(WORKSP, workspVal & 0xFF);
    memory->DirectWriteToZ80Memory(WORKSP + 1, (workspVal >> 8) & 0xFF);
    
    // Set K_CUR to cursor position (after command, before 0x0D)
    uint16_t kCurVal = eLineAddr + tokenized.size();
    memory->DirectWriteToZ80Memory(K_CUR, kCurVal & 0xFF);
    memory->DirectWriteToZ80Memory(K_CUR + 1, (kCurVal >> 8) & 0xFF);
    
    // Set CH_ADD to E_LINE for LINE-SCAN to parse from start
    memory->DirectWriteToZ80Memory(CH_ADD, eLineAddr & 0xFF);
    memory->DirectWriteToZ80Memory(CH_ADD + 1, (eLineAddr >> 8) & 0xFF);
    
    // Trigger screen refresh by injecting a keypress
    // Cursor right (0x09) doesn't modify buffer content but triggers editor redraw
    injectKeypress(memory, 0x09);
    
    result.success = true;
    result.message = "[48K BASIC] Injected: " + command;
    
    return result;
}

BasicEncoder::InjectionResult BasicEncoder::injectTo128K(Emulator* emulator, const std::string& command)
{
    InjectionResult result;
    result.success = false;
    result.state = BasicState::Basic128K;
    
    if (!emulator)
    {
        result.message = "Error: Emulator not available";
        return result;
    }
    
    Memory* memory = emulator->GetMemory();
    if (!memory)
    {
        result.message = "Error: Memory not available";
        return result;
    }
    
    // Method 1 from docs: Keystroke simulation with frame stepping
    // For 128K, we inject each ASCII character as a keypress.
    // The 128K editor's character handler (L28F1) will:
    // 1. Insert the character into SLEB
    // 2. Update the screen display
    // This way both buffer AND screen are properly updated.
    //
    // Unlike 48K where we inject pre-tokenized bytes,
    // 128K editor expects plain ASCII (tokenization happens on ENTER).
    //
    // We run 2 frames per character to allow ROM to process the keypress.
    // Pause once for entire injection, then resume. Avoids repeated pause/resume overhead.
    
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning)
    {
        emulator->Pause(false);  // Pause without broadcasting
    }
    
    for (char c : command)
    {
        injectKeypress(memory, static_cast<uint8_t>(c));
        
        // Run 2 frames inline without pause/resume (we're already paused)
        // This is more efficient than calling RunNFrames which has its own pause/resume logic
        for (int frame = 0; frame < 2; frame++)
        {
            emulator->RunSingleCPUCycle(true);
            // Run until frame boundary
            EmulatorContext* ctx = emulator->GetContext();
            uint64_t frameStart = ctx->emulatorState.frame_counter;
            while (ctx->emulatorState.frame_counter == frameStart)
            {
                emulator->RunSingleCPUCycle(true);
            }
        }
    }
    
    if (wasRunning)
    {
        emulator->Resume(false);  // Resume without broadcasting
    }
    
    result.success = true;
    result.message = "[128K BASIC] Injected: " + command;
    
    return result;
}

BasicEncoder::InjectionResult BasicEncoder::injectToTRDOS(Memory* memory, const std::string& command)
{
    InjectionResult result;
    result.success = false;
    result.state = BasicState::TRDOS_Active;
    
    if (!memory)
    {
        result.message = "Error: Memory not available";
        return result;
    }
    
    // TR-DOS uses the EXACT same E_LINE buffer mechanism as 48K BASIC!
    // IMPORTANT DISCOVERY: TR-DOS commands ARE tokenized!
    //   - LIST appears as 0xF0 (not ASCII "LIST")
    //   - LOAD appears as 0xEF (not ASCII "LOAD")
    // We must tokenize the command before injecting.
    //
    // From TR-DOS ROM at $027B (RUN "boot" command injection):
    //   LD HL,($5C59)   ; E_LINE - get command buffer address
    //   LD (HL),0xF7    ; RUN token
    //   ...
    //   LD ($5C5B),HL   ; K_CUR - set cursor position  
    //   LD (HL),$0D     ; ENTER terminator
    //   LD ($5C61),HL   ; WORKSP - set workspace
    
    using namespace SystemVariables48k;
    
    // Read E_LINE address from system variables
    uint16_t eLineAddr = memory->DirectReadFromZ80Memory(E_LINE) |
                         (memory->DirectReadFromZ80Memory(E_LINE + 1) << 8);
    
    // Validate E_LINE points to reasonable RAM area
    // After TR-DOS initialization, E_LINE should point past TR-DOS sys vars (~$5D3C)
    if (eLineAddr < 0x5B00 || eLineAddr > 0xFFFF)
    {
        result.message = "Error: Invalid E_LINE address: " + std::to_string(eLineAddr);
        return result;
    }
    
    // TOKENIZE the command (same as 48K BASIC)
    // This converts "LOAD" → 0xEF, "LIST" → 0xF0, etc.
    auto tokenized = tokenizeImmediate(command);
    
    // Write tokenized command to E_LINE buffer
    uint16_t currentAddr = eLineAddr;
    for (size_t i = 0; i < tokenized.size(); i++)
    {
        memory->DirectWriteToZ80Memory(currentAddr++, tokenized[i]);
    }
    
    // Set K_CUR to cursor position (after command text, BEFORE 0x0D)
    // This must be set BEFORE writing the terminator
    memory->DirectWriteToZ80Memory(K_CUR, currentAddr & 0xFF);
    memory->DirectWriteToZ80Memory(K_CUR + 1, (currentAddr >> 8) & 0xFF);
    
    // Write ENTER terminator
    memory->DirectWriteToZ80Memory(currentAddr++, 0x0D);
    
    // Write end-of-area marker
    memory->DirectWriteToZ80Memory(currentAddr++, 0x80);
    
    
    // Set WORKSP to point after the buffer content
    memory->DirectWriteToZ80Memory(WORKSP, currentAddr & 0xFF);
    memory->DirectWriteToZ80Memory(WORKSP + 1, (currentAddr >> 8) & 0xFF);
    
    // DO NOT update STKBOT/STKEND in TR-DOS!
    // TR-DOS manages its own stack, and modifying these system variables
    // corrupts the return stack, causing hangs and screen corruption.
    // Unlike 48K BASIC which uses these for calculator stack management,
    // TR-DOS relies on the Z80 SP register and ROM stack handling.
    
    // Set CH_ADD to E_LINE for LINE-SCAN to parse from start
    // This is CRITICAL - without it, ENTER doesn't know where to find the command!
    memory->DirectWriteToZ80Memory(CH_ADD, eLineAddr & 0xFF);
    memory->DirectWriteToZ80Memory(CH_ADD + 1, (eLineAddr >> 8) & 0xFF);
    
    // Trigger screen refresh by injecting a keypress
    // This makes the injected text visible on screen by triggering the ROM's
    // editor redraw routine. Cursor right (0x09) doesn't modify buffer content.
    // The ROM will process this on the next frame and redraw the line.
    injectKeypress(memory, 0x09);  // Cursor right - triggers redraw without changing buffer
    
    result.success = true;
    result.message = "[TR-DOS] Injected tokenized command to E_LINE at " + std::to_string(eLineAddr) + ": " + command;
    result.state = BasicState::TRDOS_Active;
    
    return result;
}

// ============================================================================
// Dispatcher Functions
// ============================================================================

BasicEncoder::InjectionResult BasicEncoder::injectCommand(Emulator* emulator, const std::string& command)
{
    InjectionResult result;
    result.success = false;
    result.state = BasicState::Unknown;
    
    if (!emulator)
    {
        result.message = "Error: Emulator not available";
        return result;
    }
    
    Memory* memory = emulator->GetMemory();
    if (!memory)
    {
        result.message = "Error: Memory not available";
        return result;
    }
    
    // Detect current BASIC state
    result.state = detectState(memory);
    
    // Dispatch to ROM-specific injection function
    switch (result.state)
    {
        case BasicState::Basic48K:
            return injectTo48K(memory, command);
            
        case BasicState::Basic128K:
            return injectTo128K(emulator, command);
            
        case BasicState::TRDOS_Active:
        case BasicState::TRDOS_SOS_Call:
            // TR-DOS uses SOS ROM for keyboard input, so E_LINE buffer still works!
            // Commands are entered at the A> prompt using standard 48K input mechanism
            return injectToTRDOS(memory, command);
            
        case BasicState::Menu128K:
            result.message = "Error: On 128K menu. Use autoNavigateAndInject() or enter BASIC first.";
            break;
            
        default:
            result.message = "Error: Not in BASIC editor. State: " + std::to_string(static_cast<int>(result.state));
            break;
    }
    
    return result;
}

BasicEncoder::InjectionResult BasicEncoder::autoNavigateAndInject(Emulator* emulator, const std::string& command)
{
    InjectionResult result;
    result.success = false;
    result.state = BasicState::Unknown;
    
    if (!emulator)
    {
        result.message = "Error: Emulator not available";
        return result;
    }
    
    Memory* memory = emulator->GetMemory();
    if (!memory)
    {
        result.message = "Error: Memory not available";
        return result;
    }
    
    // Detect current BASIC state
    result.state = detectState(memory);
    
    // If on 128K menu, navigate to 128K BASIC first
    if (result.state == BasicState::Menu128K)
    {
        navigateToBasic128K(emulator);
        
        // Re-detect state after navigation
        result.state = detectState(memory);
        
        // Verify we're now in BASIC
        if (result.state != BasicState::Basic128K && result.state != BasicState::Basic48K)
        {
            result.message = "Error: Failed to navigate from menu to BASIC. Current state: " + 
                             std::to_string(static_cast<int>(result.state));
            return result;
        }
    }
    
    // Now delegate to the regular injectCommand
    return injectCommand(emulator, command);
}

BasicEncoder::InjectionResult BasicEncoder::runCommand(Emulator* emulator, const std::string& command)
{
    InjectionResult result;
    result.success = true;
    
    if (!command.empty())
    {
        // Auto-navigate and inject the command
        result = autoNavigateAndInject(emulator, command);
        if (!result.success)
        {
            return result;
        }
    }
    else
    {
        // Just detect current state for proper ENTER injection logic below
        result.state = detectState(emulator->GetMemory());
    }
    
    Memory* memory = emulator->GetMemory();
    if (memory)
    {
        if (result.state == BasicState::TRDOS_Active)
        {
            // Trigger screen refresh once more before ENTER, ensures buffer is definitely synchronized
            // and visible to the ROM's keyboard loop.
            injectKeypress(memory, 0x09);  // Cursor right
            emulator->RunNFrames(2);       // Wait for refresh to process

            // For TR-DOS, we prefer physical events to ensure the ROM's keyboard loop
            // correctly picks up the command.
            injectEnterPhysical(emulator);
        }
        else
        {
            // Standard 48K/128K BASIC handles LAST_K injection fine
            injectEnter(memory);
        }
    }
    // Update message to indicate execution was triggered
    result.message += "\nExecuting...";
    
    return result;
}

void BasicEncoder::injectEnterPhysical(Emulator* emulator)
{
    if (!emulator) return;

    // Post physical key events with a delay in between
    // This ensures the ROM's keyboard scanner (running on INT) sees the key pressed
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    mc.Post(MC_KEY_PRESSED, new KeyboardEvent(ZXKEY_ENTER, KEY_PRESSED, emulator->GetUUID()));
    emulator->RunNFrames(4); // Hold for 4 frames (80ms) - be slightly more generous
    mc.Post(MC_KEY_RELEASED, new KeyboardEvent(ZXKEY_ENTER, KEY_RELEASED, emulator->GetUUID()));
    emulator->RunNFrames(2); // Wait for release to be processed
}
