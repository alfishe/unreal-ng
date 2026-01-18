#include "basicencoder.h"
#include "emulator/memory/memory.h"
#include "emulator/spectrumconstants.h"

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
/// @brief Detect current BASIC state based on ROM page and editor flags
/// 
/// Detection Logic:
/// ================
/// 1. ROM PAGE CHECK (from Memory::GetROMPage())
///    - Pages 1, 3 = 48K BASIC ROM  →  Basic48K state
///    - Pages 0, 2 = 128K Editor/Menu ROM  →  continue to step 2
///
/// 2. EDITOR_FLAGS CHECK ($EC0D in RAM bank 7)
///    - Bit 1 SET (value & 0x02) = Menu is currently displayed  →  Menu128K
///    - Bit 1 CLEAR = In 128K BASIC editor, not menu  →  Basic128K
///
/// Key Discovery (2026-01-17):
/// ===========================
/// - FLAGS3 ($5B66) bit 0 is NOT reliable for detecting BASIC vs Menu mode
/// - EDITOR_FLAGS ($EC0D) bit 1 directly indicates if menu is shown
/// - When in 128K BASIC, FLAGS3 can still be 0x00
/// - The SLEB buffer at $EC16 is used by 128K BASIC, E_LINE by 48K
///
BasicEncoder::BasicState BasicEncoder::detectState(Memory* memory)
{
    if (!memory)
        return BasicState::Unknown;
    
    // PRIMARY CHECK: ROM page
    // ROM pages 1 and 3 = 48K BASIC ROM
    // ROM pages 0 and 2 = 128K Editor/Menu ROM
    // This is THE most reliable indicator across all Spectrum models
    uint16_t romPage = memory->GetROMPage();
    
    // DEBUG: Log ROM page
    std::cout << "[DEBUG] detectState: romPage=" << romPage << std::endl;
    
    if (romPage == 1 || romPage == 3)
    {
        // 48K BASIC ROM is active
        std::cout << "[DEBUG] detectState: returning Basic48K (ROM pages 1 or 3)" << std::endl;
        return BasicState::Basic48K;
    }
    
    // ROM 0 or 2 → 128K Editor/Menu ROM active
    // Now check RAM bank 7 to distinguish menu vs BASIC editor
    // CRITICAL: Must verify bank 7 is actually paged at $C000 before reading EC0D
    
    // Check which RAM bank is paged at $C000-$FFFF (BANK_M bits 0-2)
    uint8_t bankM = memory->DirectReadFromZ80Memory(0x5B5C);
    uint8_t upperBank = bankM & 0x07;  // Bits 0-2 = RAM bank at $C000
    
    std::cout << "[DEBUG] detectState: bankM=" << (int)bankM << " upperBank=" << (int)upperBank << std::endl;
    
    if (upperBank != 7)
    {
        // Bank 7 not paged, can't read editor variables
        // Must be in menu or early boot - assume menu
        std::cout << "[DEBUG] detectState: returning Menu128K (bank7 not paged)" << std::endl;
        return BasicState::Menu128K;
    }
    
    // Bank 7 IS paged - now safe to read EDITOR_FLAGS
    uint8_t* ramBank7 = memory->RAMPageAddress(7);
    if (!ramBank7)
    {
        std::cout << "[DEBUG] detectState: returning Unknown (ramBank7 null)" << std::endl;
        return BasicState::Unknown;
    }
    
    // Check EDITOR_FLAGS ($EC0D in RAM bank 7)
    constexpr uint16_t EDITOR_FLAGS_OFFSET = Editor128K::EDITOR_FLAGS - 0xC000;  // $2C0D
    uint8_t editorFlags = ramBank7[EDITOR_FLAGS_OFFSET];
    
    std::cout << "[DEBUG] detectState: editorFlags=" << (int)editorFlags << " bit1=" << ((editorFlags & 0x02) ? 1 : 0) << std::endl;
    
    // Bit 1: 1 = Menu displayed, 0 = In BASIC editor
    if (editorFlags & 0x02)
    {
        std::cout << "[DEBUG] detectState: returning Menu128K (bit1 set)" << std::endl;
        return BasicState::Menu128K;
    }
    
    std::cout << "[DEBUG] detectState: returning Basic128K" << std::endl;
    return BasicState::Basic128K;
}

bool BasicEncoder::isInBasicEditor(Memory* memory)
{
    BasicState state = detectState(memory);
    return (state == BasicState::Basic128K || state == BasicState::Basic48K);
}

void BasicEncoder::navigateToBasic128K(Memory* memory)
{
    if (!memory)
        return;
    
    // When 128K menu is displayed, RAM bank 7 is paged at $C000-$FFFF
    // Set menu index $EC0C to 1 (128 BASIC)
    // Menu items: 0=Tape Loader, 1=128 BASIC, 2=Calculator, 3=48 BASIC, 4=Tape Tester
    memory->DirectWriteToZ80Memory(EC0D_FLAGS - 1, 0x01);  // EC0C = menu index = 1
    
    // Set FLAGS3 bit 0 to indicate BASIC mode
    uint8_t flags3 = memory->DirectReadFromZ80Memory(FLAGS3);
    memory->DirectWriteToZ80Memory(FLAGS3, flags3 | 0x01);
}

std::string BasicEncoder::injectCommand(Memory* memory, const std::string& command)
{
    if (!memory)
        return "Error: Memory not available";
    
    BasicState state = detectState(memory);
    std::ostringstream ss;
    
    if (state == BasicState::Basic128K)
    {
        // 128K BASIC: Write to Screen Line Edit Buffer (SLEB) in RAM bank 7
        // CRITICAL: $EC00-$FFFF is in RAM bank 7, NOT accessible via Z80 address space
        // unless bank 7 is paged at $C000-$FFFF. We must write directly to RAM page.
        using namespace Editor128K;
        
        // Get direct pointer to RAM bank 7
        uint8_t* ramBank7 = memory->RAMPageAddress(7);
        if (!ramBank7)
        {
            ss << "Error: Cannot access RAM bank 7";
            return ss.str();
        }
        
        // SLEB at $EC16 = offset $2C16 in RAM bank 7 (since bank 7 maps to $C000-$FFFF)
        // Offset = $EC16 - $C000 = $2C16
        constexpr uint16_t SLEB_OFFSET = SLEB - 0xC000;  // $EC16 - $C000 = $2C16
        
        // Write command text (max 32 chars per row)
        size_t len = std::min(command.size(), static_cast<size_t>(32));
        for (size_t i = 0; i < len; i++)
        {
            ramBank7[SLEB_OFFSET + i] = static_cast<uint8_t>(command[i]);
        }
        // Pad remaining chars with nulls
        for (size_t i = len; i < 32; i++)
        {
            ramBank7[SLEB_OFFSET + i] = 0x00;
        }
        
        // Set row flags (3 bytes after the 32 characters)
        // Bit 0=first row, Bit 3=last row of logical line
        ramBank7[SLEB_OFFSET + 32] = 0x09;
        ramBank7[SLEB_OFFSET + 33] = 0x00;  // Line number low
        ramBank7[SLEB_OFFSET + 34] = 0x00;  // Line number high
        
        // Set cursor position at $F6EE-$F6EF (required for ENTER handler)
        constexpr uint16_t CURSOR_ROW_OFFSET = 0xF6EE - 0xC000;    // $36EE
        constexpr uint16_t CURSOR_COL_OFFSET = 0xF6EF - 0xC000;    // $36EF
        ramBank7[CURSOR_ROW_OFFSET] = 0x00;                         // Row 0
        ramBank7[CURSOR_COL_OFFSET] = static_cast<uint8_t>(len);   // Column = after text
        
        // CRITICAL: Set "line altered" flag (bit 3 of EDITOR_FLAGS at $EC0D)
        // This tells ROM to tokenize and process the line when ENTER is pressed
        // Use OR to preserve other flags, only set bit 3
        constexpr uint16_t EDITOR_FLAGS_OFFSET = EDITOR_FLAGS - 0xC000;  // $2C0D
        ramBank7[EDITOR_FLAGS_OFFSET] |= 0x08;  // Set bit 3 = line altered
        
        // NOTE: Do NOT set cursor position, edit count, or other variables
        // The ROM maintains these itself. Setting them causes conflicts and reboots.
        
        ss << "[128K BASIC] Injected: " << command;
        ss << "\nPress ENTER to execute.";
    }
    else if (state == BasicState::Basic48K)
    {
        // 48K BASIC: All-at-once buffer injection
        // Based on ROM analysis: populate E-LINE, set WORKSP, CH_ADD, K_CUR
        using namespace SystemVariables48k;
        
        // Tokenize the command (keywords → single byte tokens)
        auto tokenized = BasicEncoder::tokenizeImmediate(command);
        
        uint16_t eLineAddr = memory->DirectReadFromZ80Memory(E_LINE) |
                             (memory->DirectReadFromZ80Memory(E_LINE + 1) << 8);
        
        // Write tokenized command to E_LINE buffer
        for (size_t i = 0; i < tokenized.size(); i++)
        {
            memory->DirectWriteToZ80Memory(eLineAddr + i, tokenized[i]);
        }
        
        // Terminate with ENTER (0x0D)
        memory->DirectWriteToZ80Memory(eLineAddr + tokenized.size(), 0x0D);
        // End marker (0x80 tells ROM this is end of edit area)
        memory->DirectWriteToZ80Memory(eLineAddr + tokenized.size() + 1, 0x80);
        
        // CRITICAL: Set WORKSP to point after the E-LINE buffer content
        // WORKSP (23649) = E_LINE + content_length + 2 (after 0x0D and 0x80)
        // The ROM uses this to know where the workspace begins
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
        
        ss << "[48K BASIC] Injected: " << command;
    }
    else
    {
        ss << "Error: Not in BASIC editor. Please enter 48K or 128K BASIC first.";
    }
    
    return ss.str();
}
