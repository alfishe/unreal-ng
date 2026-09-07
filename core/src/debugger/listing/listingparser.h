#pragma once
#include "stdafx.h"

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <istream>

#include "emulator/platform.h"

// Forward declarations
class ModuleLogger;
class EmulatorContext;

/// @brief Structure representing one logical source line from a listing file
struct ListingLine
{
    // Member variables
    int lineNumber = 0;              // Source line number
    uint16_t addressStart = 0;       // First emitted byte address (valid when hasCode)
    uint16_t addressEnd = 0;         // Last emitted byte address (inclusive, rows merged)
    std::vector<uint8_t> bytes;      // All emitted machine-code bytes (continuation rows merged)
    std::string source;              // Source text (first non-empty row wins)
    bool hasCode = false;            // Line emits at least one byte

    ListingLine() = default;
    ~ListingLine() = default;
};

/// @brief sjasmplus .lst listing parser — source-level debugging support
///
/// Row grammar (tolerant, basic .lst format first):
///   [lineNum] [ADDR:] [BB BB ...] source-text
///   - lineNum: decimal source line number (sjasmplus prints it on every row)
///   - ADDR:    1-8 hex digits followed by ':' — start address of the row's bytes
///   - BB:      exactly-2-hex-digit machine-code byte tokens
/// Rows sharing a line number (long DEFW/byte wraps) merge into one ListingLine.
/// Rows with an address above 0xFFFF are kept in the line list but not byte-mapped.
/// Lines without emitted bytes are kept for source context but never address-mapped.
class ListingParser
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
    const uint16_t _SUBMODULE = PlatformDebuggerSubmodulesEnum::SUBMODULE_DEBUG_GENERIC;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;

    std::string _sourcePath;
    std::vector<ListingLine> _lines;
    std::map<int, size_t> _indexByLineNumber;      // line number -> index into _lines
    std::vector<int32_t> _addressToLine;           // 65536 entries, -1 = not covered
    bool _loaded = false;
    uint16_t _minAddress = 0xFFFF;
    uint16_t _maxAddress = 0;
    size_t _codeLineCount = 0;
    size_t _totalBytes = 0;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    ListingParser(EmulatorContext* context);
    virtual ~ListingParser();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    // File operations
    bool LoadListing(const std::string& path);
    void Clear();
    bool IsLoaded() const { return _loaded; }
    const std::string& GetSourcePath() const { return _sourcePath; }

    // Line lookup
    const ListingLine* FindLineByAddress(uint16_t address) const;
    const ListingLine* FindLineByNumber(int lineNumber) const;

    // Navigation — first line with emitted code at/after (or at/before) a line number
    const ListingLine* FindNextCodeLine(int fromLineNumber) const;
    const ListingLine* FindPrevCodeLine(int fromLineNumber) const;

    // Statistics
    size_t GetLineCount() const { return _lines.size(); }
    size_t GetCodeLineCount() const { return _codeLineCount; }
    size_t GetTotalBytes() const { return _totalBytes; }
    uint16_t GetMinAddress() const { return _minAddress; }
    uint16_t GetMaxAddress() const { return _maxAddress; }
    const std::vector<ListingLine>& GetLines() const { return _lines; }

protected:
    // Parsing
    bool ParseStream(std::istream& input);
    void MapAddressRange(size_t lineIndex, uint32_t start, size_t count);
    void RecountStatistics();

    // Token helpers
    static size_t NextToken(const std::string& line, size_t pos, std::string& token);
    static std::string TrimRight(const std::string& str);
    static bool IsAllDigits(const std::string& str);
    static bool IsHexDigit(char c);
    static bool IsHexPair(const std::string& str);
    static bool IsHexString(const std::string& str);
    static uint32_t ParseHex32(const std::string& str);
    /// endregion </Methods>
};
