#pragma once

// The source an assembler holds in the machine's RAM (asm-synchronizer.md §5-§7): a descriptor per assembler and
// version says how to recognize it and where its text is; a reader per layout family turns the text into the live
// file, the bytes the assembler's own SAVE would write, so the source codecs read it unchanged. Like the label table
// scanners (symbols/live.h), everything runs on a copy of the memory the caller made at a coherent moment.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/encoding.h"
#include "unrealasm/symbols/live.h"

namespace unrealasm::sync
{
/// The machine's memory as a reader needs it: the RAM pages copied (not every page has to be there) and the RAM page
/// each 16 KB window of the CPU showed at the moment of the copy
struct MachineView
{
    std::vector<symbols::MemoryPage> ram;
    std::array<int, 4> windows{-1, -1, -1, -1};   ///< RAM page at #0000 / #4000 / #8000 / #C000; -1 = ROM or not copied
    int ramPages = 8;                             ///< the machine's RAM size in 16 KB pages (page id rules)

    const symbols::MemoryPage* Page(int page) const;
    /// Bytes of the CPU view; false when a byte lies in a window whose page was not copied
    bool Read(uint16_t address, size_t length, std::vector<uint8_t>& out) const;
    std::optional<uint8_t> Byte(uint16_t address) const;
    std::optional<uint16_t> Word(uint16_t address) const;
};

enum class LayoutFamily : uint8_t
{
    FileImage,   ///< a page holds the file as the assembler saves it: header + lines, the length in a header field
    GapBuffer,   ///< text before the cursor, a gap, text after it; the current line in a line buffer
    Linear,      ///< one buffer from a start to an end pointer
};

enum class PageIdRule : uint8_t
{
    Port7FFD,       ///< a memory driver's page id: bits 0-2 = port #7FFD bits 0-2, bits 6-7 the 512K / 1024K extension
    MappedAtC000,   ///< the page the CPU sees at #C000 (the CPU view is read as it is)
    Fixed,          ///< CPU addresses, no paging (48K assemblers)
};

enum class TypingRule : uint8_t
{
    NotInText,      ///< the line under the cursor stays out of the text until Enter: the live file is without it
    InLineBuffer,   ///< the line under the cursor is only in the line buffer: the reader puts it into the text
    InPlace,        ///< the editor edits the text itself
};

/// Bytes that must be at a CPU address
struct Signature
{
    uint16_t address = 0;
    std::string bytes;
};

struct FileImageParams
{
    uint16_t pageIdAt = 0;          ///< CPU address of the page id of the current text
    uint16_t base = 0;              ///< offset of the file in its page
    uint16_t headerSize = 0;        ///< the length field counts the bytes after the header
    uint16_t lengthField = 0;       ///< offset of the 16-bit length in the header
    uint16_t signatureAt = 0;       ///< offset of the header's signature
    std::string signature;
    uint16_t changedField = 0;      ///< offset of the "changed since saved" byte (0 = none); SAVE writes it as 0
};

struct GapBufferParams
{
    uint16_t start = 0;             ///< CPU addresses of the pointers: the text start, its top (exclusive),
    uint16_t top = 0;
    uint16_t gapStart = 0;          ///< the gap start (end of the text before the cursor),
    uint16_t gapEnd = 0;            ///< the gap end (start of the text after the cursor)
    uint16_t lineCount = 0;         ///< CPU address of the 16-bit line count (the current line counted)
    uint16_t lineBuffer = 0;        ///< CPU address of the current line as text, blank padded
    uint16_t lineBufferLength = 0;
    encoding::CodePage lineCodePage = encoding::CodePage::Cp866;
    std::string end;                ///< the end record SAVE adds
};

struct LinearParams
{
    uint16_t startAt = 0;           ///< CPU address of the start pointer (0 = the text starts at fixedStart)
    uint16_t fixedStart = 0;
    uint16_t endAt = 0;             ///< CPU address of the end pointer (exclusive)
    std::string end;                ///< the end marker the file has after the text
};

struct TypingParams
{
    TypingRule rule = TypingRule::InPlace;
    uint16_t flagAt = 0;            ///< NotInText: CPU address of the "line being edited" flag
    uint8_t flagMask = 0;
};

struct SyncDescriptor
{
    std::string id;                 ///< "alasm-5.07", "tasm-4.12"
    std::string title;
    std::string codec;              ///< the unreal-asm codec of the live file and its version
    std::string version;
    std::string extension;          ///< the TR-DOS type the assembler saves ("H", "A")
    std::vector<Signature> identify;   ///< all must match
    PageIdRule pages = PageIdRule::Fixed;
    LayoutFamily family = LayoutFamily::Linear;
    FileImageParams fileImage;
    GapBufferParams gapBuffer;
    LinearParams linear;
    TypingParams typing;
    std::string labelScanner;       ///< the symbols/live.h scanner of its label table ("" = none)
};

/// Every built-in descriptor (asm-synchronizer.md §7), each checked with golden dumps: ALASM 5.09, 4.44, TASM 4.12
const std::vector<SyncDescriptor>& Descriptors();
const SyncDescriptor* FindDescriptor(const std::string& id);

/// The source in memory as its assembler would save it
struct SyncText
{
    bool ok = false;
    std::string state = "none";     ///< "ok", "inconsistent" (pointers out of range: mid-update, or not that version)
    std::vector<uint8_t> file;      ///< the live file
    std::string name;               ///< the text's name when the layout keeps one (ALASM's header)
    int page = -1;                  ///< the RAM page the text is in (FileImage), -1 = the CPU view
    bool editor = false;            ///< the editor holds a line apart (GapBuffer: the line buffer is in the file)
    bool typing = false;            ///< a line is being typed and is not in the file yet (NotInText)
    bool changed = false;           ///< changed since the assembler last saved it
    size_t lines = 0;               ///< lines in the live file, where the layout counts them
    long currentLine = -1;          ///< 0-based line of the cursor, where known
    Diagnostics diagnostics;
};

SyncText ReadText(const MachineView& machine, const SyncDescriptor& descriptor);

struct ProbeCandidate
{
    const SyncDescriptor* descriptor = nullptr;
    int score = 0;                  ///< 100 = identified and its text reads; 60 = identified, the text does not read
    std::string reason;
};

/// Every descriptor that identifies, best first
std::vector<ProbeCandidate> Probe(const MachineView& machine);
}  // namespace unrealasm::sync
