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

#include "unrealasm/dialect.h"
#include "unrealasm/diagnostics.h"
#include "unrealasm/encoding.h"
#include "unrealasm/snapshot.h"
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
    int sp = -1;                                  ///< the CPU's SP when it was copied (the idle rule); -1 = not known

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

/// A variable of the assembler's editor its LOAD sets besides the text (XAS keeps the cursor line, the top line and
/// the text end in instruction operands): found by a code pattern in the CPU view, written by an inject as LOAD would
struct LoadVariable
{
    std::vector<int> pattern;       ///< code bytes; -1 = any byte
    uint8_t at = 0;                 ///< offset in the pattern of the variable's 16-bit address
    enum class From : uint8_t
    {
        Header,                     ///< `size` bytes of the file's header from offset `field`
        TextEnd,                    ///< the CPU address of the text's end byte (lengthFromEnd layouts)
        Constant,                   ///< `value`
        SetBits,                    ///< the byte as it is with the bits of `value` set
        PageBytes,                  ///< `size` bytes of the text's page as written, from offset `field` (the name field)
        SecondLine,                 ///< the CPU address of the second line of [body][length] lines (STORM; the end
                                    ///< sentinel's for a text of one line)
    } from = From::Header;
    uint16_t field = 0;
    uint8_t size = 2;
    uint32_t value = 0;
    int16_t offset = 0;             ///< added to the address the pattern gives (a field of a variable block)
};

struct FileImageParams
{
    uint16_t pageIdAt = 0;          ///< CPU address of the page id of the current text
    uint16_t base = 0;              ///< offset of the file in its page (the other offsets count from the page's start)
    uint16_t headerSize = 0;        ///< the length field counts the bytes after the header
    uint16_t lengthField = 0;       ///< offset of the 16-bit length in the header
    uint16_t signatureAt = 0;       ///< offset of the header's signature
    std::string signature;
    uint16_t changedField = 0;      ///< offset of the "changed since saved" byte (0 = none); SAVE clears changedMask in it
    uint8_t changedMask = 0xFF;
    uint8_t nameLength = 8;         ///< the text's name, blank padded (0 = none)
    uint16_t nameAt = 0;
    /// The length from an end pointer (STORM): the 16-bit CPU address at pointerAt is one past the text's last byte,
    /// which is endSentinel and not part of the file
    bool lengthFromPointer = false;
    uint16_t pointerAt = 0;
    uint8_t endSentinel = 0;
    /// No length field (lengthField unused): the text ends at the first endByte from textOffset; SAVE writes whole
    /// 256-byte sectors from memory when wholeSectors (XAS)
    bool lengthFromEnd = false;
    uint16_t textOffset = 0;
    uint8_t endByte = 0;
    bool wholeSectors = false;
    /// Header bytes SAVE rewrites from the editor's own state (cursor line and column): the reader leaves them as the
    /// memory holds them, so a live file differs from the next save there and only there
    uint16_t editorStateAt = 0;
    uint16_t editorStateLength = 0;
    /// What LOAD sets in the editor's own variables (XAS): an inject writes them too, or refuses when one is not found
    std::vector<LoadVariable> loadVariables;
    /// The editor's own "changed since LOAD / SAVE" flags where the text's page has none (STORM): the byte each pattern
    /// gives, changed when a bit of `value` is set (`from` SetBits) or clear (`from` Constant: a "written" flag)
    std::vector<LoadVariable> changedVariables;
};

struct GapBufferParams
{
    uint16_t start = 0;             ///< CPU addresses of the pointers: the text start, its top (exclusive),
    uint16_t top = 0;
    uint16_t gapStart = 0;          ///< the gap start (end of the text before the cursor),
    uint16_t gapEnd = 0;            ///< the gap end (start of the text after the cursor)
    uint16_t lineCount = 0;         ///< CPU address of the 16-bit line count (the current line counted)
    uint16_t cursorLine = 0;        ///< CPU address of the cursor's line number (0 = none known)
    /// TASM 4.12: LOAD leaves the file as saved (its end record included) at the text start with the other pointers and
    /// the line count 0; MASM 1.1: at its fixed start with the gap start there and the gap end at #FFFF. The editor
    /// makes the gap buffer when it is entered. The reader takes that form, an inject writes it
    bool loadedForm = false;
    /// TASM 4.0 / 3.x: an inject writes the text as their command line holds it after the editor: the file from the start,
    /// the gap start on its end record, the gap end and the top #FFFF, the editor flag cleared
    bool commandLineForm = false;
    uint16_t changedAt = 0;         ///< CPU address of the editor's "changed since LOAD / SAVE" byte (0 = none)
    uint8_t changedMask = 0xFF;
    uint16_t nameAt = 0;            ///< CPU address of the work name, blank padded (0 = none)
    uint8_t nameLength = 8;
    uint16_t lineBuffer = 0;        ///< CPU address of the current line as text, blank padded
    uint16_t lineBufferLength = 0;
    encoding::CodePage lineCodePage = encoding::CodePage::Cp866;
    std::string end;                ///< the end record SAVE adds
    /// Without a line count (MASM, TASM 4.0): the editor known by a non-zero byte in its line buffer; in the editor the
    /// cursor line is out of the text, its last version the length-framed record that ends at the gap end. MASM has a
    /// fixed start and its end marker inside the text up to #FFFF (fixedStart != 0, endInText)
    uint16_t fixedStart = 0;
    bool endInText = false;
    uint16_t editorFlagAt = 0;
    uint16_t editorFlagLength = 0;
    /// MASM 2.0 / 3.0: while the flag is set the cursor line is out of the text altogether, as text in the line buffer;
    /// the text after it starts at the address this word holds (0 = MASM 1.x's record before the gap end)
    uint16_t nextLineAt = 0;
    /// MASM 1.0 demo: in the editor the cursor line's record [n] body [n] starts at the gap start (1.1: ends at the gap end)
    bool recordAtGapStart = false;
    /// TASM 2.0: its files are plain text (CR LF). Outside the editor the buffer is that text from (start)+1 to
    /// (gapStart) inclusive, then (gapEnd) to (top); in the editor it holds 3.x's length-framed records, which the
    /// live file turns into lines
    bool plainTextFile = false;
    /// Bytes of the file SAVE fills with the editor's state (MASM 3.0: the cursor line in its header), not in RAM
    uint16_t editorStateAt = 0;
    uint16_t editorStateLength = 0;
};

struct LinearParams
{
    uint16_t startAt = 0;           ///< CPU address of the start pointer (0 = the text starts at fixedStart)
    uint16_t fixedStart = 0;
    uint16_t endAt = 0;             ///< CPU address of the end pointer (exclusive)
    uint16_t limitAt = 0;           ///< CPU address of the word where the text's space ends (0 = none; Laser Genius)
    /// A block the editor keeps right after the text, from the end pointer to the byte the word at followLastAt points
    /// at (Laser Genius: its screen lines); an inject moves it behind the new text and the words in followPointers
    /// (0 = unset) with it
    uint16_t followLastAt = 0;
    std::vector<uint16_t> followPointers;
    std::string end;                ///< the end marker the file has after the text
    int upperPage = -1;             ///< the RAM page the text's part from #C000 is in (-1 = the one mapped there)
    /// SAVE writes a first line starting with this (the editor's position, ZAsm's ";!") that the editor takes out on
    /// loading: the live file is without it ("" = none)
    std::string stateLine;
    /// GENS: records [number lo] [number hi] text #0D; while a line is typed in, the editor marks the line it goes
    /// before with bit 7 of the number's high byte (numbers stay below 32768): the live file has it clear
    bool clearLineMark = false;
    /// Laser Genius: the text ends with FF FF in memory; its disk file is #AF, the length, the start, four characters
    /// of the name (filled with blanks: SAVE names it, an editor state), then the paragraphs without the FF FF
    bool laserGeniusFile = false;
    /// PASM: the text runs from fixedStart to its #1A end; the cursor line (the address at pasmLineAt) lives in a
    /// 64-byte line buffer (pasmLineBuffer) and goes back with its blank runs packed into TABs (a run of 2-8 blanks
    /// that ends at a multiple of 8), as SAVE does
    uint16_t pasmLineAt = 0;
    uint16_t pasmLineBuffer = 0;
    /// PASM inject, as ZAP and GET leave the editor: the text's length (pasmLengthAt, without the #1A), the number of
    /// its last line (pasmLastLineAt), the cursor line's length (pasmLineLengthAt) and the first line on the screen
    /// (pasmTopAt, the start); the words in zeroWords and the bytes in zeroBytes to 0 (the cursor's line and column, the
    /// block marks), the redraw flag (pasmRedrawAt) to #FF and the work name (pasmNameAt, 8 characters)
    uint16_t pasmLengthAt = 0;
    uint16_t pasmLastLineAt = 0;
    uint16_t pasmLineLengthAt = 0;
    uint16_t pasmTopAt = 0;
    uint16_t pasmRedrawAt = 0;
    uint16_t pasmNameAt = 0;
    std::vector<uint16_t> zeroWords;
    std::vector<uint16_t> zeroBytes;
    /// PROMETHEUS: SAVE writes the records from fixedStart (the access line) up to twelve bytes below the symbol table
    /// (the word at prometheusTableAt), FF XOR those bytes, FF, then the table up to the word at prometheusEndAt. An
    /// inject keeps the end below the word at limitAt (U-TOP) and sets the words in resetWords to the start
    uint16_t prometheusTableAt = 0;
    uint16_t prometheusEndAt = 0;
    std::vector<uint16_t> resetWords;
    uint16_t editorStateAt = 0;     ///< bytes of the file SAVE fills (masked by the tests)
    uint16_t editorStateLength = 0;
    /// An inject writes the text from (startAt) and the end pointer (injectable), and sets the words from resetFrom up
    /// to resetTo (exclusive; the cursor line, the marks: ZAsm's table) to the start, as LOAD leaves them; the words in
    /// zeroWords and the bytes in zeroBytes to 0 (ZAsm's cursor line and column: the editor would go on from the old ones)
    bool injectable = false;
    uint16_t resetFrom = 0;
    uint16_t resetTo = 0;
    std::vector<uint16_t> resetAlso;   ///< other words set to the start (ZX-ASM 3.10: one past an odd byte)
    std::vector<uint16_t> endAlso;     ///< other words set to the end (ZX ASM 3.0: a second end word)
    uint16_t blankFrom = 0;            ///< bytes from blankFrom up to blankTo (exclusive) set to blanks (ZX-ASM 2.x: the
    uint16_t blankTo = 0;              ///< cursor line's buffer, which the editor writes back into the text)
    /// The work name, blank padded: what SAVE writes the text under and the editor's status line shows (ZAsm 3.10 and
    /// later: the name in its "A:NAME    .asm"); an inject gives it the title (0 = none). In RAM page namePage (the
    /// 512K ZAsms: page 27, mapped at #C000 or not), -1: the page mapped at nameAt now (not upperPage)
    uint16_t nameAt = 0;
    uint8_t nameLength = 8;
    int namePage = -1;
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
    bool textsPerPage = false;      ///< the assembler keeps one text per page, each with its header (ALASM): a project's other texts
    /// RAM pages (0-7, the same ids at 128K and 512K) the assembler leaves to texts of their own: a project's other
    /// sources go there, and its INCLUDE "NAME",page finds them loaded (ALASM) instead of reading the disk
    std::vector<int> projectPages;
    /// The assembler holds one text and reads what it INCLUDEs from the disk (XAS: LTEXT): a project's other sources
    /// go onto the disk (ProjectInjectPlan::diskFiles) instead of into memory
    bool projectOnDisk = false;
    /// Return addresses the stack holds in this order (lowest address first, within `idleDepth` bytes above SP) only
    /// while the assembler waits for a command: a text may be written then (part B, §16.2). Empty = no rule known
    std::vector<uint16_t> idleChain;
    /// Other chains that mean the same (builds sharing a descriptor whose code lies elsewhere: STORM 1.3 and 1.3i)
    std::vector<std::vector<uint16_t>> idleAlternatives;
    uint16_t idleDepth = 64;
    /// Where the assembler waits when it is idle, for the refusal's text ("" = its command line)
    std::string idleWhere;
    /// Bytes that are all zero while it is idle (MASM: its line buffer, blank padded in the editor; 0 = none)
    uint16_t idleZeroAt = 0;
    uint16_t idleZeroLength = 0;
    /// An installer that moves the whole code (PROMETHEUS asks its address): installedAt the address this descriptor
    /// describes (0 = the code stays), identify[0]'s absolute words at the offsets relocatedWords move with it. Probe
    /// finds a moved copy when nothing identifies in place; its descriptor is "<id>@<address>" (Relocated)
    uint16_t installedAt = 0;
    std::vector<uint8_t> relocatedWords;
};

/// Every built-in descriptor (asm-synchronizer.md §7), each checked with golden dumps: ALASM 3.8c, 4.42-4.46, 4.5,
/// 5.00-5.09, TASM 4.12, XAS 4.18, 5.05, 7.43c, 7.447, 9.07m, 9.10, STORM 1.3, ZAsm 3.15
const std::vector<SyncDescriptor>& Descriptors();
/// A built-in descriptor by id; "<id>@<address>" is the relocatable one installed at that address (Relocated)
const SyncDescriptor* FindDescriptor(const std::string& id);
/// The code of `base` moved by `shift` bytes: every address of it below the paged window (all of them on a 48K one,
/// upperPage 0) moves, so do identify[0]'s relocatedWords. Kept for the process (the same pointer for the same move)
const SyncDescriptor* Relocated(const SyncDescriptor& base, int shift);

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

/// The other texts a layout keeps one per page (descriptor.textsPerPage: ALASM), every page of the copy but
/// `exceptPage` (the live text's): their files and names, for a project's INCLUDEs (asm-synchronizer.md §5.4)
std::vector<SyncText> ReadOtherTexts(const MachineView& machine, const SyncDescriptor& descriptor, int exceptPage);

/// Part B (asm-synchronizer.md §16): the assembler waits for a command (the descriptor's idle rule holds); false with
/// the reason
bool Idle(const MachineView& machine, const SyncDescriptor& descriptor, std::string& why);

struct InjectOptions
{
    bool force = false;          ///< write even when the text in memory has changes not saved, or no idle rule is known
    bool markChanged = true;     ///< the header says "changed since saved" (ALASM asks before it loads another text)
    /// Files this caller wrote before (the push watch, Z2): a text in memory whose lines are still those of one of them
    /// is the caller's own and is replaced without force, though it is marked changed
    std::vector<std::vector<uint8_t>> written;
    /// The text's name where the assembler keeps it outside the file (TASM 4.12's work name); PlanInjectProject gives
    /// its title. Empty: the name in memory stays
    std::string name;
};

/// The writes that make `file` (the assembler's own file, as its codec encodes it) the live text
struct InjectPlan
{
    bool ok = false;
    std::string error;
    int page = -1;                       ///< the RAM page written (FileImage)
    std::vector<uint8_t> bytes;          ///< its new contents, the whole page
    struct Poke
    {
        int page = -1;
        uint16_t offset = 0;
        uint8_t value = 0;
    };
    std::vector<Poke> pokes;             ///< bytes of other pages written with it (FileImageParams::loadVariables)
    SyncText previous;                   ///< the text it replaces (previous.ok false: none read)
};

/// Plans writing `file` over the live text: the assembler idle, the old text without unsaved changes (or `force`),
/// the file within the page. FileImage layouts (ALASM); the others are refused
InjectPlan PlanInject(const MachineView& machine, const SyncDescriptor& descriptor, const std::vector<uint8_t>& file, const InjectOptions& options = {});

/// A project written into the assembler at once (asm-synchronizer.md §16.4): the main source over the live text, each
/// source it INCLUDEs into a page of its own (descriptor.projectPages), the INCLUDEs naming those pages
struct ProjectInjectPlan
{
    bool ok = false;
    std::string error;
    std::vector<InjectPlan> pages;       ///< main first, then the INCLUDEd sources in the project's order
    std::vector<WriteAsResult> texts;    ///< the files written, in the same order (empty: not written)
    /// projectOnDisk: the INCLUDEd sources as the assembler's disk files (texts[1...] by CatalogFile), for the disk it
    /// reads them from; pages then holds the main text alone
    std::vector<containers::TrdosFile> diskFiles;
    Diagnostics diagnostics;
};

/// Plans a project (HostProject: files[0] the main source): written as the assembler's texts by WriteProjectAs, the
/// main one titled `title` over the live text (PlanInject's rules), the others named by ProjectTextNames into the
/// descriptor's project pages, each refused when it holds a text with changes not
/// saved (unless `force`), or as disk files (projectOnDisk). One file is PlanInject of WriteAs
ProjectInjectPlan PlanInjectProject(const MachineView& machine, const SyncDescriptor& descriptor, const std::vector<ProjectFile>& files,
                                    const std::string& title, const InjectOptions& options = {});

struct ProbeCandidate
{
    const SyncDescriptor* descriptor = nullptr;
    int score = 0;                  ///< 100 = identified and its text reads; 60 = identified, the text does not read
    std::string reason;
};

/// Every descriptor that identifies, best first
std::vector<ProbeCandidate> Probe(const MachineView& machine);

/// The machine a snapshot file holds (Z1): its RAM pages, windows and SP. The view points into `snapshot`'s pages
MachineView ViewOf(const containers::Snapshot& snapshot);

/// Part B into a snapshot (asm-synchronizer.md §16.3): `file` (the assembler's own file) written over the live text of
/// the assembler in `snapshot`, under the same rules as in a running machine (PlanInject); `out` is the snapshot file
/// with that page replaced. The plan says why not
InjectPlan InjectIntoSnapshot(const containers::Snapshot& snapshot, const SyncDescriptor& descriptor, const std::vector<uint8_t>& file,
                              const InjectOptions& options, std::vector<uint8_t>& out);

/// PlanInjectProject on the memory of `snapshot`; `out` is the snapshot file with every page of the plan replaced
ProjectInjectPlan InjectProjectIntoSnapshot(const containers::Snapshot& snapshot, const SyncDescriptor& descriptor,
                                            const std::vector<ProjectFile>& files, const std::string& title,
                                            const InjectOptions& options, std::vector<uint8_t>& out);
}  // namespace unrealasm::sync
