#include "unrealasm/sync/reader.h"

#include <algorithm>
#include <map>

#include "unrealasm/registry.h"

namespace unrealasm::sync
{
namespace
{
constexpr uint32_t kPage = 0x4000;

std::string Hex(uint32_t value, int digits)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out(static_cast<size_t>(digits), '0');
    for (int k = digits - 1; k >= 0; --k, value >>= 4)
        out[static_cast<size_t>(k)] = kDigits[value & 0xF];
    return out;
}

SyncText Inconsistent(SyncText text, const std::string& why)
{
    text.ok = false;
    text.state = "inconsistent";
    text.diagnostics.push_back({Severity::Warning, 0, 0, why});
    return text;
}

bool Matches(const MachineView& machine, const Signature& s)
{
    std::vector<uint8_t> bytes;
    return machine.Read(s.address, s.bytes.size(), bytes) && std::equal(bytes.begin(), bytes.end(), s.bytes.begin(),
                                                                        [](uint8_t a, char b) { return a == static_cast<uint8_t>(b); });
}

// FileImage ---------------------------------------------------------------------------------------------------------

/// The pages a page id may name: the driver's id with its extension bits on a larger machine, then the 128K bits
std::vector<int> PagesForId(const MachineView& machine, PageIdRule rule, uint8_t id)
{
    std::vector<int> pages;
    if (rule == PageIdRule::Port7FFD)
    {
        if (machine.ramPages > 8)
            pages.push_back((id & 7) | ((id >> 3) & 0x18));
        pages.push_back(id & 7);
    }
    else if (rule == PageIdRule::MappedAtC000 && machine.windows[3] >= 0)
        pages.push_back(machine.windows[3]);
    return pages;
}

/// The text a FileImage layout keeps in one page: 1 read, 0 no text header there, -1 a header but the text does not read
int ReadFileImageInPage(const MachineView& machine, const SyncDescriptor& d, int candidate, SyncText& text)
{
    const FileImageParams& p = d.fileImage;
    const symbols::MemoryPage* page = machine.Page(candidate);
    if (!page || page->bytes.size() < kPage)
        return 0;
    const auto bytes = page->bytes;
    if (bytes.size() < size_t(p.base) + p.headerSize || bytes.size() < size_t(p.signatureAt) + p.signature.size() ||
        !std::equal(p.signature.begin(), p.signature.end(), bytes.begin() + p.signatureAt,
                    [](char a, uint8_t b) { return static_cast<uint8_t>(a) == b; }))
        return 0;
    size_t length = 0;
    if (p.lengthFromEnd)
    {
        const auto end = std::find(bytes.begin() + p.base + p.textOffset, bytes.end(), p.endByte);
        if (end == bytes.end())
        {
            Inconsistent(text, "no end of text in page " + std::to_string(candidate));
            return -1;
        }
        length = static_cast<size_t>(end - bytes.begin()) - p.base + 1;
        if (p.wholeSectors)
            length = std::min<size_t>((length + 255) / 256 * 256, bytes.size() - p.base);
    }
    else if (p.lengthFromPointer)
    {
        const uint32_t end = bytes[p.pointerAt] | (bytes[p.pointerAt + 1u] << 8);
        const uint32_t first = kPage * 3 + p.base;   // the text's CPU address at #C000
        if (end <= first || end > kPage * 4 || bytes[end - kPage * 3 - 1] != p.endSentinel)
        {
            Inconsistent(text, "the end pointer #" + Hex(end, 4) + " does not end the text in page " + std::to_string(candidate));
            return -1;
        }
        length = end - first - 1;
    }
    else
        length = p.headerSize + (bytes[p.base + p.lengthField] | (bytes[p.base + p.lengthField + 1u] << 8));
    if (size_t(p.base) + length > bytes.size())
    {
        Inconsistent(text, "the text in page " + std::to_string(candidate) + " is longer than the page");
        return -1;
    }
    text.page = candidate;
    text.file.assign(bytes.begin() + p.base, bytes.begin() + p.base + static_cast<std::ptrdiff_t>(length));
    if (p.changedField)
    {
        text.changed = (text.file[p.changedField] & p.changedMask) != 0;
        text.file[p.changedField] &= static_cast<uint8_t>(~p.changedMask);   // SAVE writes the file as saved
    }
    size_t nameLength = p.nameLength;
    while (nameLength > 0 && bytes[p.nameAt + nameLength - 1] == ' ')
        --nameLength;
    text.name.assign(bytes.begin() + p.nameAt, bytes.begin() + p.nameAt + static_cast<std::ptrdiff_t>(nameLength));
    text.ok = true;
    text.state = "ok";
    return 1;
}

SyncText ReadFileImage(const MachineView& machine, const SyncDescriptor& d)
{
    SyncText text;
    const FileImageParams& p = d.fileImage;
    std::optional<uint8_t> id;
    if (d.pages == PageIdRule::Port7FFD)
    {
        id = machine.Byte(p.pageIdAt);
        if (!id)
            return Inconsistent(text, "the page id at #" + Hex(p.pageIdAt, 4) + " is not in the memory copied");
    }
    for (const int candidate : PagesForId(machine, d.pages, id.value_or(0)))
        if (ReadFileImageInPage(machine, d, candidate, text) != 0)
            return text;
    return Inconsistent(text, id ? "no text header in the page the id #" + Hex(*id, 2) + " names" : std::string("no text header at #C000"));
}

// GapBuffer ---------------------------------------------------------------------------------------------------------

/// Records framed by their length on both sides (TASM 4.x: n, body of n bytes, n); -1 when the bytes are not that
long CountFramed(const std::vector<uint8_t>& bytes)
{
    long count = 0;
    size_t i = 0;
    while (i < bytes.size())
    {
        const size_t n = bytes[i];
        if (i + n + 1 >= bytes.size() || bytes[i + n + 1] != n)
            return -1;
        i += n + 2;
        ++count;
    }
    return count;
}

/// The current line as the codec writes it: one line encoded, its end record cut off
bool EncodeLine(const SyncDescriptor& d, const std::string& line, std::vector<uint8_t>& out, std::string& error)
{
    const ISourceCodec* codec = CodecRegistry::Builtin().Find(d.codec);
    if (!codec)
    {
        error = "no codec " + d.codec;
        return false;
    }
    SourceDocument document;
    document.dialect = codec->Info().dialect;
    document.format = d.codec;
    document.subversion = d.version;
    document.codePage = d.gapBuffer.lineCodePage;
    document.lines.push_back({line, {}, -1});
    EncodeOptions options;
    options.subversion = d.version;
    EncodeResult encoded = codec->Encode(document, options);
    // A header the codec writes before the lines (MASM 2.0 FF, 3.0 FF lo hi FF): what an empty document gets before
    // the end record
    SourceDocument empty = document;
    empty.lines.clear();
    const EncodeResult framing = codec->Encode(empty, options);
    const std::string& end = d.gapBuffer.end;
    if (framing.ok && framing.bytes.size() > end.size() && encoded.bytes.size() >= framing.bytes.size())
    {
        const size_t header = framing.bytes.size() - end.size();
        if (std::equal(framing.bytes.begin(), framing.bytes.begin() + static_cast<std::ptrdiff_t>(header), encoded.bytes.begin()))
            encoded.bytes.erase(encoded.bytes.begin(), encoded.bytes.begin() + static_cast<std::ptrdiff_t>(header));
    }
    if (!encoded.ok || encoded.bytes.size() < end.size() ||
        !std::equal(end.begin(), end.end(), encoded.bytes.end() - static_cast<std::ptrdiff_t>(end.size()),
                    [](char a, uint8_t b) { return static_cast<uint8_t>(a) == b; }))
    {
        error = "the current line does not encode: " + line;
        return false;
    }
    out.assign(encoded.bytes.begin(), encoded.bytes.end() - static_cast<std::ptrdiff_t>(end.size()));
    return true;
}

/// A gap buffer whose editor state shows in its line buffer (MASM, TASM 4.0): [start, gap start) + [gap end, top), and
/// in the editor the cursor line's record that ends at the gap end
SyncText ReadGapBufferFixed(const MachineView& machine, const SyncDescriptor& d)
{
    SyncText text;
    const GapBufferParams& p = d.gapBuffer;
    const auto gapStart = machine.Word(p.gapStart), gapEnd = machine.Word(p.gapEnd);
    const std::optional<uint16_t> startWord = p.fixedStart ? std::optional<uint16_t>(p.fixedStart) : machine.Word(p.start);
    const std::optional<uint16_t> topWord = p.endInText ? std::optional<uint16_t>(0) : machine.Word(p.top);
    std::vector<uint8_t> flag;
    if (!gapStart || !gapEnd || !startWord || !topWord || !machine.Read(p.editorFlagAt, p.editorFlagLength, flag))
        return Inconsistent(text, "the text pointers are not in the memory copied");
    const uint32_t start = *startWord;
    const uint32_t top = p.endInText ? 0x10000u : *topWord;
    if (!(start <= *gapStart && *gapStart <= *gapEnd && *gapEnd <= top))
        return Inconsistent(text, "the text pointers are out of order: #" + Hex(start, 4) + " #" + Hex(*gapStart, 4) + " #" +
                                      Hex(*gapEnd, 4) + " #" + Hex(top, 4));
    text.editor = std::any_of(flag.begin(), flag.end(), [](uint8_t b) { return b != 0; });
    if (p.plainTextFile && !text.editor)
    {
        std::vector<uint8_t> lower, upper;
        if (*gapStart + 1u < start + 1u || !machine.Read(static_cast<uint16_t>(start + 1), static_cast<size_t>(*gapStart - start), lower) ||
            !machine.Read(static_cast<uint16_t>(*gapEnd), static_cast<size_t>(top - *gapEnd), upper))
            return Inconsistent(text, "the text lies in a page that was not copied");
        text.file = std::move(lower);
        text.file.insert(text.file.end(), upper.begin(), upper.end());
        text.ok = true;
        text.state = "ok";
        return text;
    }
    uint32_t upperFrom = *gapEnd;
    std::vector<uint8_t> current;
    if (text.editor && p.nextLineAt)
    {
        // The cursor line is in the line buffer as text; the text goes on at the next line
        const std::optional<uint16_t> next = machine.Word(p.nextLineAt);
        std::vector<uint8_t> buffer;
        if (!next || *next < *gapEnd || !machine.Read(p.lineBuffer, p.lineBufferLength, buffer))
            return Inconsistent(text, "the line after the cursor or the line buffer is not in the memory copied");
        const auto stop = std::find(buffer.begin(), buffer.end(), uint8_t(0));
        buffer.erase(stop, buffer.end());
        while (!buffer.empty() && buffer.back() == ' ')
            buffer.pop_back();
        std::string error;
        if (!EncodeLine(d, encoding::ToUtf8(buffer, p.lineCodePage), current, error))
            return Inconsistent(text, error);
        upperFrom = *next;
    }
    else if (text.editor && p.recordAtGapStart)
    {
        const std::optional<uint8_t> n = machine.Byte(static_cast<uint16_t>(*gapStart));
        if (!n || uint32_t(*gapStart) + *n + 2 > *gapEnd ||
            machine.Byte(static_cast<uint16_t>(*gapStart + *n + 1)) != n ||
            !machine.Read(static_cast<uint16_t>(*gapStart), size_t(*n) + 2, current))
            return Inconsistent(text, "no line record at the gap start #" + Hex(*gapStart, 4));
    }
    else if (text.editor)
    {
        // The cursor line's record [n] body [n] ends at the gap end
        const std::optional<uint8_t> n = machine.Byte(static_cast<uint16_t>(*gapEnd - 1));
        if (!n || *gapEnd < uint32_t(*n) + 2 || uint32_t(*gapEnd) - *n - 2 < *gapStart ||
            machine.Byte(static_cast<uint16_t>(*gapEnd - *n - 2)) != n)
            return Inconsistent(text, "no line record before the gap end #" + Hex(*gapEnd, 4));
        upperFrom = *gapEnd - *n - 2u;
    }
    std::vector<uint8_t> lower, upper;
    if (!machine.Read(static_cast<uint16_t>(start), static_cast<size_t>(*gapStart - start), lower) ||
        !machine.Read(static_cast<uint16_t>(upperFrom), static_cast<size_t>(top - upperFrom), upper))
        return Inconsistent(text, "the text lies in a page that was not copied");
    if (p.endInText && (upper.empty() || upper.back() != static_cast<uint8_t>(p.end.empty() ? 0xFF : p.end.back())))
        return Inconsistent(text, "the text does not end with its end marker at #FFFF");
    text.file = std::move(lower);
    text.file.insert(text.file.end(), current.begin(), current.end());
    text.file.insert(text.file.end(), upper.begin(), upper.end());
    if (p.plainTextFile)
    {
        // The editor's records [n] body [n] as the lines SAVE writes
        std::vector<uint8_t> lines;
        for (size_t k = 0; k < text.file.size();)
        {
            const size_t n = text.file[k];
            if (k + n + 1 >= text.file.size() || text.file[k + n + 1] != n)
                return Inconsistent(text, "the text is not made of length-framed lines");
            lines.insert(lines.end(), text.file.begin() + static_cast<std::ptrdiff_t>(k + 1),
                         text.file.begin() + static_cast<std::ptrdiff_t>(k + 1 + n));
            lines.push_back('\r');
            lines.push_back('\n');
            k += n + 2;
        }
        text.file = std::move(lines);
    }
    else if (!p.endInText)
        text.file.insert(text.file.end(), p.end.begin(), p.end.end());
    text.ok = true;
    text.state = "ok";
    return text;
}

SyncText ReadGapBuffer(const MachineView& machine, const SyncDescriptor& d)
{
    SyncText text;
    const GapBufferParams& p = d.gapBuffer;
    if (p.editorFlagLength)
        return ReadGapBufferFixed(machine, d);
    const auto start = machine.Word(p.start), top = machine.Word(p.top);
    const auto gapStart = machine.Word(p.gapStart), gapEnd = machine.Word(p.gapEnd), total = machine.Word(p.lineCount);
    if (!start || !top || !gapStart || !gapEnd || !total)
        return Inconsistent(text, "the text pointers are not in the memory copied");
    if (!(*start <= *gapStart && *gapStart <= *gapEnd && *gapEnd <= *top))
        return Inconsistent(text, "the text pointers are out of order: #" + Hex(*start, 4) + " #" + Hex(*gapStart, 4) + " #" +
                                      Hex(*gapEnd, 4) + " #" + Hex(*top, 4));
    std::vector<uint8_t> lower, upper;
    if (!machine.Read(*start, static_cast<size_t>(*gapStart - *start), lower) ||
        !machine.Read(*gapEnd, static_cast<size_t>(*top - *gapEnd), upper))
        return Inconsistent(text, "the text lies in a page that was not copied");
    const long before = CountFramed(lower), after = CountFramed(upper);
    if (before < 0 || after < 0)
        return Inconsistent(text, "the text is not made of length-framed lines");
    // All lines in the text (the command line: the editor put its line back), or all but the one in the line buffer
    text.file = std::move(lower);
    if (before + after + 1 == *total)
    {
        std::vector<uint8_t> buffer;
        if (!machine.Read(p.lineBuffer, p.lineBufferLength, buffer))
            return Inconsistent(text, "the line buffer is not in the memory copied");
        while (!buffer.empty() && (buffer.back() == ' ' || buffer.back() == 0))
            buffer.pop_back();
        std::vector<uint8_t> record;
        std::string error;
        if (!EncodeLine(d, encoding::ToUtf8(buffer, p.lineCodePage), record, error))
            return Inconsistent(text, error);
        text.file.insert(text.file.end(), record.begin(), record.end());
        text.editor = true;
        text.currentLine = before;
    }
    else if (before + after != *total)
        return Inconsistent(text, "the line count (" + std::to_string(*total) + ") is neither the lines in the text (" +
                                      std::to_string(before + after) + ") nor one more");
    text.file.insert(text.file.end(), upper.begin(), upper.end());
    text.file.insert(text.file.end(), p.end.begin(), p.end.end());
    text.lines = *total;
    text.ok = true;
    text.state = "ok";
    return text;
}

// Linear ------------------------------------------------------------------------------------------------------------

SyncText ReadLinear(const MachineView& mapped, const SyncDescriptor& d)
{
    SyncText text;
    const LinearParams& p = d.linear;
    MachineView machine = mapped;
    if (p.upperPage >= 0)
        machine.windows[3] = p.upperPage;   // the text's upper part, whatever the program maps at #C000 just now
    const std::optional<uint16_t> start = p.startAt ? machine.Word(p.startAt) : std::optional<uint16_t>(p.fixedStart);
    if (p.pasmLineAt)
    {
        std::vector<uint8_t> all, buffer;
        const std::optional<uint16_t> line = machine.Word(p.pasmLineAt);
        if (!line || !machine.Read(p.fixedStart, static_cast<size_t>(0xC000 - p.fixedStart), all) ||
            !machine.Read(p.pasmLineBuffer, 64, buffer))
            return Inconsistent(text, "the text lies in a page that was not copied");
        const auto stop = std::find(all.begin(), all.end(), uint8_t(0x1A));
        if (stop == all.end())
            return Inconsistent(text, "no #1A end after the text");
        all.erase(stop, all.end());
        const size_t from = static_cast<size_t>(*line - p.fixedStart);
        if (*line < p.fixedStart || from > all.size())
            return Inconsistent(text, "the cursor line is outside the text");
        size_t to = from;
        while (to + 1 < all.size() && !(all[to] == '\r' && all[to + 1] == '\n'))
            ++to;
        size_t length = buffer.size();
        while (length > 0 && buffer[length - 1] == ' ')
            --length;
        buffer.resize(length);
        // PASM's packing, right to left: a run of 2..8 blanks that ends at a tab stop becomes a TAB
        for (size_t stopAt = ((length + 1) / 8) * 8; stopAt >= 8; stopAt -= 8)
        {
            size_t run = 0;
            while (run < 8 && buffer[stopAt - 1 - run] == ' ')
                ++run;
            if (run >= 2)
            {
                buffer.erase(buffer.begin() + static_cast<std::ptrdiff_t>(stopAt - run), buffer.begin() + static_cast<std::ptrdiff_t>(stopAt));
                buffer.insert(buffer.begin() + static_cast<std::ptrdiff_t>(stopAt - run), uint8_t('\t'));
            }
        }
        text.file.assign(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(from));
        text.file.insert(text.file.end(), buffer.begin(), buffer.end());
        text.file.insert(text.file.end(), all.begin() + static_cast<std::ptrdiff_t>(std::min(to, all.size())), all.end());
        text.ok = true;
        text.state = "ok";
        return text;
    }
    if (p.prometheusTableAt)
    {
        const std::optional<uint16_t> table = machine.Word(p.prometheusTableAt), tableEnd = machine.Word(p.prometheusEndAt);
        if (!start || !table || !tableEnd || *table < *start + 12u || *tableEnd < *table)
            return Inconsistent(text, "the source / symbol table pointers are not in order");
        std::vector<uint8_t> records, symbols;
        if (!machine.Read(*start, static_cast<size_t>(*table - 12 - *start), records) ||
            !machine.Read(*table, static_cast<size_t>(*tableEnd - *table), symbols))
            return Inconsistent(text, "the text lies in a page that was not copied");
        uint8_t check = 0xFF;
        for (const uint8_t b : records)
            check ^= b;
        text.file = std::move(records);
        text.file.push_back(check);
        text.file.push_back(0xFF);
        text.file.insert(text.file.end(), symbols.begin(), symbols.end());
        text.ok = true;
        text.state = "ok";
        return text;
    }
    const std::optional<uint16_t> end = machine.Word(p.endAt);
    if (!start || !end)
        return Inconsistent(text, "the text pointers are not in the memory copied");
    if (*end < *start)
        return Inconsistent(text, "the text ends (#" + Hex(*end, 4) + ") before it starts (#" + Hex(*start, 4) + ")");
    if (!machine.Read(*start, static_cast<size_t>(*end - *start), text.file))
        return Inconsistent(text, "the text lies in a page that was not copied");
    if (p.clearLineMark)
    {
        for (size_t k = 0; k + 1 < text.file.size();)
        {
            text.file[k + 1] &= 0x7F;
            const auto cr = std::find(text.file.begin() + static_cast<std::ptrdiff_t>(k + 2), text.file.end(), uint8_t(0x0D));
            if (cr == text.file.end())
                return Inconsistent(text, "a line record without its #0D end");
            k = static_cast<size_t>(cr - text.file.begin()) + 1;
        }
    }
    if (p.laserGeniusFile)
    {
        if (text.file.size() < 2 || text.file[text.file.size() - 1] != 0xFF || text.file[text.file.size() - 2] != 0xFF)
            return Inconsistent(text, "the text does not end with FF FF");
        text.file.resize(text.file.size() - 2);
        const auto length = static_cast<uint16_t>(text.file.size());
        const std::vector<uint8_t> header = {0xAF, static_cast<uint8_t>(length & 0xFF), static_cast<uint8_t>(length >> 8),
                                             static_cast<uint8_t>(*start & 0xFF), static_cast<uint8_t>(*start >> 8), ' ', ' ', ' ', ' '};
        text.file.insert(text.file.begin(), header.begin(), header.end());
    }
    text.file.insert(text.file.end(), p.end.begin(), p.end.end());
    text.ok = true;
    text.state = "ok";
    return text;
}

// Descriptors -------------------------------------------------------------------------------------------------------

/// The title is looked for where the code keeps it (each build elsewhere); #BE00 also holds it, but the editor uses
/// that place as a buffer. `typingFlag` false: the build keeps no "line modified" bit at IX+#0C (4.43)
/// `idle`: the return chain of its command line (line input #91E3, then the command loop #9707 in 5.09; a command runs
/// with #9733 there instead). `projectPages`: the pages none of its own uses take (sysvars IX+#16 macros, #21
/// debugger, #27-#2A post labels, #36 pocket; SYSTEM and the overlay from its source), for INCLUDE "NAME",page
SyncDescriptor Alasm(const std::string& version, const std::string& codecVersion, uint16_t titleAt, std::string title = "",
                     bool typingFlag = true, std::vector<uint16_t> idle = {}, std::vector<int> projectPages = {})
{
    SyncDescriptor d;
    d.id = "alasm-" + version;
    d.title = "ALASM " + version;
    d.codec = "alasm";
    d.version = codecVersion;
    d.extension = "H";
    d.identify = {{titleAt, title.empty() ? "ALASM v" + version : title}};
    d.pages = PageIdRule::Port7FFD;
    d.textsPerPage = true;
    d.family = LayoutFamily::FileImage;
    // IX = #80BF: IX+#0D the page of the current text; the text from #C000 of it is the file (asm-synchronizer.md §7.1)
    d.fileImage.pageIdAt = 0x80CC;
    d.fileImage.base = 0;
    d.fileImage.headerSize = 0x40;
    d.fileImage.lengthField = 0x21;
    d.fileImage.signatureAt = 0x28;
    d.fileImage.signature = std::string("\xF3\x76\xC7\xDD\xFD\xED\xB0\xD9", 8);
    d.fileImage.changedField = 0x27;
    d.typing = {TypingRule::NotInText, 0x80CB, static_cast<uint8_t>(typingFlag ? 0x01 : 0x00)};   // IX+#0C bit 0: the line is modified
    d.labelScanner = "alasm-table";
    d.idleChain = std::move(idle);
    d.projectPages = std::move(projectPages);
    return d;
}

/// XAS (asm-synchronizer.md §7.11): the file as it is at #C000 of the page mapped there, edited in place; the line
/// on the screen row is packed into the text on Enter. Identified by the title a new text gets, kept in the code
SyncDescriptor Xas(const std::string& id, const std::string& codecVersion, uint16_t titleAt, const std::string& title)
{
    SyncDescriptor d;
    d.id = "xas-" + id;
    d.title = "XAS " + id;
    d.codec = "xas";
    d.version = codecVersion;
    d.extension = "X";
    d.identify = {{titleAt, title}};
    d.pages = PageIdRule::MappedAtC000;
    d.family = LayoutFamily::FileImage;
    d.fileImage.base = 0;
    d.fileImage.headerSize = 36;
    d.fileImage.signatureAt = 35;   // the start sentinel
    d.fileImage.signature = std::string("\x01", 1);
    d.fileImage.changedField = 34;
    d.fileImage.changedMask = 0x80;
    d.fileImage.nameLength = 0;
    d.fileImage.lengthFromEnd = true;
    d.fileImage.textOffset = 36;
    d.fileImage.endByte = 0x00;
    d.fileImage.wholeSectors = true;
    d.fileImage.editorStateAt = 29;
    d.fileImage.editorStateLength = 6;
    d.typing = {TypingRule::NotInText, 0, 0};
    d.labelScanner = "xas-table";
    return d;
}

/// STORM (asm-synchronizer.md §7.8): the text in RAM page 6 at #C000: the end pointer, (1.2 / 1.3: the name, an #FF
/// sentinel at #C00A, the file from #C00B; 1.0beta: the #FF at #C002, the file from #C003) up to the #FF the end pointer
/// follows. The line under the cursor joins the text when the cursor leaves it. Identified by its entry code at #8000,
/// the same in every version, and the address it loads at #8027 (#87xx in 1.3 / 1.3i, #85xx in 1.0beta)
SyncDescriptor Storm(const std::string& id, const std::string& title, const std::string& codecVersion, uint16_t base, char loadHigh)
{
    SyncDescriptor d;
    d.id = id;
    d.title = title;
    d.codec = "storm";
    d.version = codecVersion;
    d.extension = "C";
    d.identify = {{0x8000, std::string("\xFD\x21\x3A\x5C\xED\x56\xFB\x2A\x3D\x5C\x01\xFC\x5F\xB7\xED\x42\x28\x02\xCF\x03\x09\xF9\x76\xF3", 24)},
                  {0x8028, std::string(1, loadHigh)}};
    d.pages = PageIdRule::MappedAtC000;
    d.family = LayoutFamily::FileImage;
    d.fileImage.base = base;
    d.fileImage.headerSize = 0;
    d.fileImage.signatureAt = static_cast<uint16_t>(base - 1);
    d.fileImage.signature = std::string("\xFF", 1);
    d.fileImage.nameAt = 0x02;
    d.fileImage.nameLength = base == 0x0B ? 8 : 0;
    d.fileImage.lengthFromPointer = true;
    d.fileImage.pointerAt = 0x00;
    d.fileImage.endSentinel = 0xFF;
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// ZAsm 3.15 (asm-synchronizer.md §7.9): one buffer from (#8829) to (#8837), its part above #C000 in RAM page 6; a
/// typed line joins it on Enter. SAVE puts the editor's position as a ";!" first line, which loading takes out.
/// Identified by its resident code at #8000 (the program unpacks itself; above the buffer is no fixed code)
SyncDescriptor Zasm315()
{
    SyncDescriptor d;
    d.id = "zasm-3.15";
    d.title = "ZAsm 3.15";
    d.codec = "zxasm";
    d.version = "3.15";
    d.extension = "a";
    d.identify = {{0x8000, std::string("\x12\xCB\x13\xC9\x7A\x1F\xCB\x1B\xCB\x1A\xC9\xCB\x03\xC9\xCB\x0B\xC9\x5A\x16\x00\xC9\x7A\x2F\x57", 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.startAt = 0x8829;
    d.linear.endAt = 0x8837;
    d.linear.upperPage = 6;
    d.linear.stateLine = ";!";
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// MASM 1.1 and 1.3 (asm-synchronizer.md §7.12; the same layout and prompt): the text from #970B; while it is edited a gap at (#96CC)-(#96CE), the part
/// after it up to #FFFF (the #FF end inside the text). Identified by its prompt in the code below the text
/// MASM 1.0 demo (asm-synchronizer.md §7.12, Spectrofon #15): 1.1's gap buffer from #913A with (#8663) / (#8665);
/// in the editor the cursor line's record is at the gap start, and the tail of the line buffer (#8600, blank padded in
/// the editor, zeros in the menu) tells the editor. Identified by its prompt at #88B5
SyncDescriptor Masm10Demo()
{
    SyncDescriptor d;
    d.id = "masm-1.0-demo";
    d.title = "MASM 1.0 demo";
    d.codec = "masm";
    d.version = "1.0";
    d.extension = "a";
    d.identify = {{0x88B5, std::string("MASM128>\0", 9)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::GapBuffer;
    d.gapBuffer.fixedStart = 0x913A;
    d.gapBuffer.endInText = true;
    d.gapBuffer.gapStart = 0x8663;
    d.gapBuffer.gapEnd = 0x8665;
    d.gapBuffer.editorFlagAt = 0x8600;
    d.gapBuffer.editorFlagLength = 32;
    d.gapBuffer.recordAtGapStart = true;
    d.gapBuffer.end = std::string("\xFF", 1);
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

SyncDescriptor Masm11()
{
    SyncDescriptor d;
    d.id = "masm-1.1";
    d.title = "MASM 1.1 / 1.3";
    d.codec = "masm";
    d.version = "1.1";
    d.extension = "a";
    d.identify = {{0x85B1, "MASM128> "}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::GapBuffer;
    d.gapBuffer.fixedStart = 0x970B;
    d.gapBuffer.endInText = true;
    d.gapBuffer.gapStart = 0x96CC;
    d.gapBuffer.gapEnd = 0x96CE;
    d.gapBuffer.editorFlagAt = 0x851A;
    d.gapBuffer.editorFlagLength = 31;
    d.gapBuffer.end = std::string("\xFF", 1);
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// MASM 2.0 / 3.0 (asm-synchronizer.md §7.12): a gap buffer from the address its files carry (#94DA / #9123) to
/// #FFFF, lines ended by 00. The gap start, the editor flag and the gap end are operands its code rewrites (the gap
/// closing routine LD DE,gs / LD HL,linebuf / LD A,flag / ... / LD HL,ge); while the flag is set the cursor line is in
/// the line buffer as text and the text goes on at the word nextLineAt
SyncDescriptor Masm2(const std::string& id, const std::string& title, const std::string& codecVersion, Signature identify,
                     uint16_t fixedStart, uint16_t routine, uint16_t lineBuffer, uint16_t nextLineAt)
{
    SyncDescriptor d;
    d.id = id;
    d.title = title;
    d.codec = "masm";
    d.version = codecVersion;
    d.extension = "a";
    d.identify = {identify};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::GapBuffer;
    d.gapBuffer.fixedStart = fixedStart;
    d.gapBuffer.endInText = true;
    d.gapBuffer.gapStart = static_cast<uint16_t>(routine + 1);      // LD DE,nn
    d.gapBuffer.editorFlagAt = static_cast<uint16_t>(routine + 7);  // LD A,n
    d.gapBuffer.editorFlagLength = 1;
    d.gapBuffer.gapEnd = static_cast<uint16_t>(routine + 13);       // LD HL,nn
    d.gapBuffer.lineBuffer = lineBuffer;
    d.gapBuffer.lineBufferLength = 64;
    d.gapBuffer.nextLineAt = nextLineAt;
    if (codecVersion == "3.0")
    {
        d.gapBuffer.editorStateAt = 1;      // FF lo hi FF: the cursor line SAVE writes
        d.gapBuffer.editorStateLength = 2;
    }
    d.gapBuffer.end = std::string("\xFF", 1);
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// TASM 4.0 / 4.4 and 3.0 / 3.2 (asm-synchronizer.md §7.6, §7.7): the pointers of 4.12 (start, top, gap start, gap
/// end) elsewhere; no line count, so the editor shows in the tail of its line buffer: blank padded in the editor, zeros
/// at the command line (whose head keeps the last line edited)
SyncDescriptor TasmGap(const std::string& version, const std::string& title, const std::string& codecVersion, Signature identify,
                       uint16_t pointers, uint16_t lineBuffer)
{
    SyncDescriptor d;
    d.id = "tasm-" + version;
    d.title = title;
    d.codec = "tasm";
    d.version = codecVersion;
    d.extension = "A";
    d.identify = {std::move(identify)};
    d.pages = PageIdRule::MappedAtC000;
    d.family = LayoutFamily::GapBuffer;
    d.gapBuffer.start = pointers;
    d.gapBuffer.top = static_cast<uint16_t>(pointers + 2);
    d.gapBuffer.gapStart = static_cast<uint16_t>(pointers + 4);
    d.gapBuffer.gapEnd = static_cast<uint16_t>(pointers + 6);
    d.gapBuffer.editorFlagAt = lineBuffer;
    d.gapBuffer.editorFlagLength = 32;
    d.gapBuffer.end = std::string("\xFF\xFF", 2);
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// TASM 2.0 (asm-synchronizer.md §7.7): 3.x's pointers at #8847 (the start one byte before the text, the gap start
/// on the last byte before the gap), plain CR LF text outside the editor, 3.x's records in it (plainTextFile); the
/// editor known by the tail of its line buffer at #8BA0. Identified by its prompt TASM128> at #92BF
SyncDescriptor Tasm20()
{
    SyncDescriptor d = TasmGap("2.0", "TASM 2.0 (Rst7)", "2.0", {0x92BF, "TASM128>"}, 0x8847, 0x8BA0);
    d.extension = "C";
    d.gapBuffer.end.clear();
    d.gapBuffer.plainTextFile = true;
    return d;
}

/// ZAsm 3.2x and later, Rubts0FF's Pentagon 512 versions (asm-synchronizer.md §7.9): the buffer below #C000 from its
/// start word, the part above #C000 in RAM page 30, the end word right after the start word (the words before them
/// follow the cursor and the screen); SAVE writes a ";*" position line first. Each version has its table elsewhere;
/// 3.2x, 3.3.02 and 3.3.51 share the code at #8048 up to the address of a variable (its low byte tells them apart)
SyncDescriptor ZasmRubtsoff(const std::string& id, const std::string& title, const std::string& codecVersion, uint16_t signatureAt,
                            const char* signature, uint16_t startAt)
{
    SyncDescriptor d;
    d.id = id;
    d.title = title;
    d.codec = "zxasm";
    d.version = codecVersion;
    d.extension = "a";
    d.identify = {{signatureAt, std::string(signature, 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.startAt = startAt;
    d.linear.endAt = static_cast<uint16_t>(startAt + 2);
    d.linear.upperPage = 30;
    d.linear.stateLine = ";*";
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// ZX-ASM 3.10 (asm-synchronizer.md §7.9): the layout of 3.15 with its table at #868F: the start, then the end at
/// #869E; the part above #C000 in RAM page 6; no ";!" line on SAVE
SyncDescriptor Zxasm310()
{
    SyncDescriptor d;
    d.id = "zasm-3.10";
    d.title = "ZX-ASM 3.10";
    d.codec = "zxasm";
    d.version = "3.0";
    d.extension = "a";
    d.identify = {{0x816C, std::string("\xC8\x00\xC8\x00\xDB\x6B\xC0\x00\x00\x0A\x0A\x8E\x00\x20\x80\x17\x00\x20\x01\x8F\x00\xF3\xFD\xE5", 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.startAt = 0x868F;
    d.linear.endAt = 0x869E;
    d.linear.upperPage = 6;
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// GENS (asm-synchronizer.md §7.13): the text follows the program's code, records [number] text #0D from the start
/// word to the end word (exclusive); the editor marks the insert position in a number's bit 15 (clearLineMark).
/// Relocatable: one descriptor per build at its usual address
SyncDescriptor Gens(const std::string& id, const std::string& title, Signature identify, uint16_t startAt, uint16_t endAt)
{
    SyncDescriptor d;
    d.id = id;
    d.title = title;
    d.codec = "gens";
    d.version = "2";
    d.extension = "A";
    d.identify = {std::move(identify)};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.startAt = startAt;
    d.linear.endAt = endAt;
    d.linear.upperPage = 0;
    d.linear.clearLineMark = true;
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// PROMETHEUS 48K tape edition installed at 24000 (asm-synchronizer.md §7.17, from its reconstructed source): the
/// records from sourceBufferAccessLine (#9C28) to twelve bytes below the symbol table (operand of varcSymbolTablePt at
/// #87D7), the table up to the operand of varcCodeEndPt (#881A); the save adds FF XOR the records and FF between
SyncDescriptor Prometheus48()
{
    SyncDescriptor d;
    d.id = "prometheus-48";
    d.title = "PROMETHEUS 48K (Proxima, at 24000)";
    d.codec = "prometheus";
    d.version = "";
    d.extension = "C";
    d.identify = {{0x8000, std::string("\xC9\xF5\xD6\x34\x5F\x16\x37\xCD\xE6\x82\xC5\x21\xE4\x8A\xAF\x4F\x23\x0C\xBE\x20\xFB\x11\xD1\x8A", 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.fixedStart = 0x9C28;
    d.linear.prometheusTableAt = 0x87D7;
    d.linear.prometheusEndAt = 0x881A;
    d.linear.upperPage = 0;
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// Power Assembler 3.0 (asm-synchronizer.md §7.18, from its code): CR LF text from #79AF to #1A; the cursor line's
/// address at #74E9, its 64-byte line buffer at #7342, packed into TABs when it goes back (the routine at #7881)
SyncDescriptor Pasm30()
{
    SyncDescriptor d;
    d.id = "pasm-3.0";
    d.title = "Power Assembler 3.0 (Sergeyev)";
    d.codec = "pasm";
    d.version = "";
    d.extension = "C";
    d.identify = {{0x7881, std::string("\x21\x82\x73\x06\x40\x3E\x20\x2B\xBE\x20\x04\x10\xFA\x18\x28\x23\x22\xFD\x77\x04\x78\xFE\x08\x38", 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.fixedStart = 0x79AF;
    d.linear.pasmLineAt = 0x74E9;
    d.linear.pasmLineBuffer = 0x7342;
    d.linear.upperPage = -1;
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// Laser Genius 1.04 Beta (asm-synchronizer.md §7.16): paragraphs from #7F2E to (#A973) (without the hash
/// extensions; (#ADA1) with them), then FF FF; the disk file adds its #AF header. Identified by its title
SyncDescriptor LaserGenius()
{
    SyncDescriptor d;
    d.id = "lasergenius-1.04";
    d.title = "Laser Genius 1.04 (Beta Disk)";
    d.codec = "lasergenius";
    d.version = "";
    d.extension = "C";
    d.identify = {{0xAC00, std::string("\xA9\xCD\x79\xAF\xE5\x2A\xDA\xA9\x06\x2A\xCB\x7E\x23\x20\x02\x10\xF9\x22\xDA\xA9\xE1\xE5\x2A\x32", 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.fixedStart = 0x7F2E;
    d.linear.endAt = 0xA973;
    d.linear.upperPage = 0;
    d.linear.laserGeniusFile = true;
    d.linear.editorStateAt = 5;
    d.linear.editorStateLength = 4;
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// ZEUS (asm-synchronizer.md §7.14): the text where "o" put it, records [number] tokens 00, then FF FF; the start
/// word and, after it, the end word (the address of the last FF). 1983, 1.1 and GG keep them at #FD00, v7.E at #FAAB
SyncDescriptor Zeus(const std::string& id, const std::string& title, const std::string& codecVersion, uint16_t signatureAt,
                    const char* signature, uint16_t startAt, const std::string& extension)
{
    SyncDescriptor d;
    d.id = id;
    d.title = title;
    d.codec = "zeus";
    d.version = codecVersion;
    d.extension = extension;
    d.identify = {{signatureAt, std::string(signature, 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.startAt = startAt;
    d.linear.endAt = static_cast<uint16_t>(startAt + 2);
    d.linear.end = std::string("\xFF", 1);
    d.linear.upperPage = 0;
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// ZX-ASM 2.4 / 2.5 / 2.6 (asm-synchronizer.md §7.9): plain text (blank runs only) in one buffer, the start word at
/// the program's load address + 3, the end word after it; the part above #C000 in RAM page 0 (the 48K memory). 2.6
/// keeps its ";*" line in the buffer and saves it as text
SyncDescriptor ZxAsm2(const std::string& id, const std::string& title, uint16_t signatureAt, const char* signature,
                      uint16_t startAt)
{
    SyncDescriptor d;
    d.id = id;
    d.title = title;
    d.codec = "zxasm";
    d.version = "2";
    d.extension = "C";
    d.identify = {{signatureAt, std::string(signature, 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.startAt = startAt;
    d.linear.endAt = static_cast<uint16_t>(startAt + 2);
    d.linear.upperPage = 0;
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// ZX ASM 3.0 (asm-synchronizer.md §7.9): the same buffer from #894F (the start of its saved files), the table in
/// page 5: the start at #61C6, the end at #61C8; the part above #C000 in RAM page 6. Saved as type C
SyncDescriptor Zxasm30()
{
    SyncDescriptor d;
    d.id = "zasm-3.0";
    d.title = "ZX ASM 3.0";
    d.codec = "zxasm";
    d.version = "3.0";
    d.extension = "C";
    d.identify = {{0x8000, std::string("\x57\xCD\x38\x79\xCD\xE0\x7A\xD4\xCD\x77\x82\x57\x3A\xB6\x7B\xFE\x01\xCC\xD3\x77\xB7\x28\x13\x3A", 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.startAt = 0x61C6;
    d.linear.endAt = 0x61C8;
    d.linear.upperPage = 6;
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

SyncDescriptor Tasm412()
{
    SyncDescriptor d;
    d.id = "tasm-4.12";
    d.title = "TASM 4.12 (Rst7)";
    d.codec = "tasm";
    d.version = "4.12";
    d.extension = "A";
    d.identify = {{0x9CDD, "TASM128>"}, {0xA394, "Import TASM3.0 source file: "}};
    d.pages = PageIdRule::MappedAtC000;
    d.family = LayoutFamily::GapBuffer;
    d.gapBuffer.start = 0x8F59;
    d.gapBuffer.top = 0x8F5B;
    d.gapBuffer.gapStart = 0x8F5D;
    d.gapBuffer.gapEnd = 0x8F5F;
    d.gapBuffer.lineCount = 0x94D9;
    d.gapBuffer.lineBuffer = 0x928A;
    d.gapBuffer.lineBufferLength = 128;
    d.gapBuffer.lineCodePage = encoding::CodePage::Cp866;
    d.gapBuffer.end = std::string("\xFF\xFF", 2);
    d.typing = {TypingRule::InLineBuffer, 0, 0};
    return d;
}
}  // namespace

MachineView ViewOf(const containers::Snapshot& snapshot)
{
    MachineView view;
    for (const auto& [page, bytes] : snapshot.pages)
        view.ram.push_back({static_cast<uint16_t>(page), std::span<const uint8_t>(bytes)});
    view.windows = snapshot.windows;
    view.ramPages = snapshot.ramPages;
    view.sp = snapshot.sp;
    return view;
}

InjectPlan InjectIntoSnapshot(const containers::Snapshot& snapshot, const SyncDescriptor& descriptor, const std::vector<uint8_t>& file,
                              const InjectOptions& options, std::vector<uint8_t>& out)
{
    InjectPlan plan = PlanInject(ViewOf(snapshot), descriptor, file, options);
    if (!plan.ok)
        return plan;
    std::string error;
    if (!containers::WriteSnapshot(snapshot, {{plan.page, plan.bytes}}, out, error))
    {
        plan.ok = false;
        plan.error = error;
    }
    return plan;
}

const symbols::MemoryPage* MachineView::Page(int page) const
{
    for (const symbols::MemoryPage& p : ram)
        if (p.page == page)
            return &p;
    return nullptr;
}

bool MachineView::Read(uint16_t address, size_t length, std::vector<uint8_t>& out) const
{
    out.clear();
    out.reserve(length);
    for (size_t k = 0; k < length; ++k)
    {
        const uint32_t at = address + static_cast<uint32_t>(k);
        if (at > 0xFFFF)
            return false;
        const int window = windows[at >> 14];
        const symbols::MemoryPage* page = window >= 0 ? Page(window) : nullptr;
        if (!page || page->bytes.size() < kPage)
            return false;
        out.push_back(page->bytes[at & (kPage - 1)]);
    }
    return true;
}

std::optional<uint8_t> MachineView::Byte(uint16_t address) const
{
    std::vector<uint8_t> b;
    if (!Read(address, 1, b))
        return std::nullopt;
    return b[0];
}

std::optional<uint16_t> MachineView::Word(uint16_t address) const
{
    std::vector<uint8_t> b;
    if (!Read(address, 2, b))
        return std::nullopt;
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

bool Idle(const MachineView& machine, const SyncDescriptor& d, std::string& why)
{
    if (d.idleChain.empty())
    {
        why = "no idle rule is known for " + d.title;
        return false;
    }
    if (machine.sp < 0)
    {
        why = "the CPU's SP was not copied";
        return false;
    }
    std::vector<uint8_t> stack;
    const uint16_t sp = static_cast<uint16_t>(machine.sp);
    const size_t depth = std::min<size_t>(d.idleDepth, 0x10000 - sp);
    if (!machine.Read(sp, depth, stack))
    {
        why = "the stack at #" + Hex(sp, 4) + " is not in the memory copied";
        return false;
    }
    const size_t n = d.idleChain.size();
    for (size_t at = 0; at + 2 * n <= stack.size(); ++at)
    {
        bool all = true;
        for (size_t k = 0; k < n && all; ++k)
            all = static_cast<uint16_t>(stack[at + 2 * k] | (stack[at + 2 * k + 1] << 8)) == d.idleChain[k];
        if (all)
            return true;
    }
    why = d.title + " is not waiting for a command (assembling, saving, loading, or in its editor)";
    return false;
}

InjectPlan PlanInject(const MachineView& machine, const SyncDescriptor& d, const std::vector<uint8_t>& file, const InjectOptions& options)
{
    InjectPlan plan;
    if (d.family != LayoutFamily::FileImage)
    {
        plan.error = "writing a text into " + d.title + " is not supported yet";
        return plan;
    }
    std::string why;
    if (!Idle(machine, d, why) && !(options.force && d.idleChain.empty()))
    {
        plan.error = why + (d.idleChain.empty() ? " (force writes anyway)" : "");
        return plan;
    }
    const FileImageParams& p = d.fileImage;
    plan.previous = ReadText(machine, d);
    if (plan.previous.ok && plan.previous.changed && !options.force)
    {
        plan.error = "the text " + plan.previous.name + " in " + d.title + " has changes not saved (force overwrites them)";
        return plan;
    }
    int page = plan.previous.ok ? plan.previous.page : -1;
    if (page < 0)
    {
        std::optional<uint8_t> id;
        if (d.pages == PageIdRule::Port7FFD)
            id = machine.Byte(p.pageIdAt);
        for (const int candidate : PagesForId(machine, d.pages, id.value_or(0)))
            if (const symbols::MemoryPage* found = machine.Page(candidate); found && found->bytes.size() >= kPage)
            {
                page = candidate;
                break;
            }
    }
    const symbols::MemoryPage* target = page >= 0 ? machine.Page(page) : nullptr;
    if (!target || target->bytes.size() < kPage)
    {
        plan.error = "the page of " + d.title + "'s text is not in the memory copied";
        return plan;
    }
    if (file.size() < p.headerSize || p.base + file.size() > kPage)
    {
        plan.error = "the text takes " + std::to_string(file.size()) + " bytes; the page holds " + std::to_string(kPage - p.base);
        return plan;
    }
    plan.page = page;
    plan.bytes.assign(target->bytes.begin(), target->bytes.begin() + kPage);
    std::fill(plan.bytes.begin() + p.base, plan.bytes.end(), uint8_t{0});
    std::copy(file.begin(), file.end(), plan.bytes.begin() + p.base);
    if (options.markChanged && p.changedField)
        plan.bytes[p.base + p.changedField] |= p.changedMask;
    plan.ok = true;
    return plan;
}

ProjectInjectPlan PlanInjectProject(const MachineView& machine, const SyncDescriptor& d, const std::vector<ProjectFile>& files,
                                    const std::string& title, const InjectOptions& options)
{
    ProjectInjectPlan result;
    if (files.empty())
    {
        result.error = "no source";
        return result;
    }
    if (files.size() == 1)
    {
        WriteAsResult written = WriteAs(files[0].document, d.codec, d.version, title);
        result.diagnostics = written.diagnostics;
        if (!written.ok)
        {
            result.error = written.error;
            return result;
        }
        result.texts.push_back(written);
        result.pages.push_back(PlanInject(machine, d, written.bytes, options));
        result.ok = result.pages[0].ok;
        result.error = result.pages[0].error;
        return result;
    }
    const size_t others = files.size() - 1;
    if (others > d.projectPages.size())
    {
        std::string pages;
        for (const int page : d.projectPages)
            pages += (pages.empty() ? "" : ", ") + std::to_string(page);
        result.error = d.title + (d.projectPages.empty() ? " keeps no other text in memory"
                                                         : " keeps " + std::to_string(d.projectPages.size()) + " more text(s) in memory (pages " + pages + ")") +
                       "; the source INCLUDEs " + std::to_string(others) + " (put the project onto its disk instead: zxasm inject <source> <disk>)";
        return result;
    }
    const std::vector<std::string> names = ProjectTextNames(files, title);
    std::map<std::string, std::string> includeAs;
    for (size_t k = 1; k < files.size(); ++k)
        includeAs[files[k].name] = "\"" + names[k] + "\"," + std::to_string(d.projectPages[k - 1]);
    WriteProjectResult written = WriteProjectAs(files, d.codec, d.version, names, includeAs);
    result.diagnostics = std::move(written.diagnostics);
    if (!written.ok)
    {
        result.error = written.error;
        return result;
    }
    result.texts = std::move(written.files);
    result.pages.push_back(PlanInject(machine, d, result.texts[0].bytes, options));
    if (!result.pages[0].ok)
    {
        result.error = result.pages[0].error;
        return result;
    }
    const FileImageParams& p = d.fileImage;
    for (size_t k = 1; k < files.size(); ++k)
    {
        InjectPlan plan;
        const int page = d.projectPages[k - 1];
        const std::vector<uint8_t>& file = result.texts[k].bytes;
        const symbols::MemoryPage* target = machine.Page(page);
        if (!target || target->bytes.size() < kPage)
        {
            result.error = "RAM page " + std::to_string(page) + " is not in the memory copied";
            return result;
        }
        if (page == result.pages[0].page)
        {
            result.error = "RAM page " + std::to_string(page) + " holds the live text";
            return result;
        }
        SyncText previous;
        if (ReadFileImageInPage(machine, d, page, previous) == 1)
        {
            if (previous.changed && !options.force)
            {
                result.error = "RAM page " + std::to_string(page) + " holds the text " + previous.name + " with changes not saved (force overwrites them)";
                return result;
            }
            plan.previous = std::move(previous);
        }
        if (file.size() < p.headerSize || p.base + file.size() > kPage)
        {
            result.error = files[k].name + " takes " + std::to_string(file.size()) + " bytes; a page holds " + std::to_string(kPage - p.base);
            return result;
        }
        plan.page = page;
        plan.bytes.assign(target->bytes.begin(), target->bytes.begin() + kPage);
        std::fill(plan.bytes.begin() + p.base, plan.bytes.end(), uint8_t{0});
        std::copy(file.begin(), file.end(), plan.bytes.begin() + p.base);
        if (options.markChanged && p.changedField)
            plan.bytes[p.base + p.changedField] |= p.changedMask;
        plan.ok = true;
        result.pages.push_back(std::move(plan));
    }
    result.ok = true;
    return result;
}

ProjectInjectPlan InjectProjectIntoSnapshot(const containers::Snapshot& snapshot, const SyncDescriptor& descriptor,
                                            const std::vector<ProjectFile>& files, const std::string& title,
                                            const InjectOptions& options, std::vector<uint8_t>& out)
{
    ProjectInjectPlan plan = PlanInjectProject(ViewOf(snapshot), descriptor, files, title, options);
    if (!plan.ok)
        return plan;
    std::map<int, std::vector<uint8_t>> changed;
    for (const InjectPlan& page : plan.pages)
        changed[page.page] = page.bytes;
    std::string error;
    if (!containers::WriteSnapshot(snapshot, changed, out, error))
    {
        plan.ok = false;
        plan.error = error;
    }
    return plan;
}

const std::vector<SyncDescriptor>& Descriptors()
{
    static const std::vector<SyncDescriptor> descriptors = {
        // Each checked with dumps (testdata/sync): the text, the file it saves, the title where the editor leaves it.
        // The idle chain: the line input's return and the command loop's above SP at the A> prompt, read in each build
        // at the prompt and in its editor (research-sjasmplus-to-alasm.md §4; the 64-column loaders, 4.5's and 3.8c's own)
        Alasm("5.09", "5.07", 0x97C5, "", true, {0x91E3, 0x9707}, {4, 0}),
        Alasm("5.08", "5.07", 0x97CA, "", true, {0x91E8, 0x970C}, {4, 0}),
        Alasm("5.07", "5.07", 0x97BC, "", true, {0x91DA, 0x96FE}, {4, 0}),
        Alasm("5.05", "5.05", 0x97BC, "", true, {0x91DA, 0x96FE}, {4, 0}),
        Alasm("5.00", "5.0", 0x9E22, "", true, {0x99EA, 0x8B93}, {4}),
        Alasm("4.5", "4.5", 0x9E06, "", true, {0x998F, 0x8B6B}),
        Alasm("4.46", "4.44", 0x9E6E, "", true, {0x9A07, 0x8B92}, {4}),
        Alasm("4.45", "4.44", 0x9E53, "", true, {0x99E1, 0x8B93}, {4}),
        Alasm("4.44", "4.44", 0x9E7E, "", true, {0x9A08, 0x8BA8}, {4}),
        Alasm("4.43", "4.5", 0x9E1B, "", false, {0x99A4, 0x8B99}, {4}),
        Alasm("4.42", "4.42", 0x9E2F, "", true, {0x99B2, 0x8B8F}, {4}),
        Alasm("3.8c", "3.8", 0x9951, "3.8c\r Written by", true, {0x943A, 0x8149}, {4}),
        Xas("4.18", "4.18", 0xB51A, "XAS by Max Petrov (HPM) 3.091"),
        Xas("5.05", "5.05", 0xB601, "XAS by Max Petrov (HPM) 5.05 "),   // 5.05 and 5.05SE alike
        Xas("7.43c", "7.43c", 0xB961, "by Max Petrov & Creator v7.43"),
        Xas("7.447", "7.43", 0x9B00, "by Max Petrov & Creator v7.44"),
        Xas("9.07m", "9.07m", 0xB8EA, "XAS 9.07 ReCompiled by Mythos"),
        Xas("9.10", "9.10", 0xB912, "XAS by Max Petrov,64sm by STS"),
        Storm("storm-1.3", "STORM 1.3", "1.3", 0x0B, '\x87'),
        Storm("storm-1.0b", "STORM 1.0beta", "1.0", 0x03, '\x85'),
        Zasm315(),
        ZasmRubtsoff("zasm-3.2x", "ZAsm 3.2x", "3.15", 0x8048,
                     "\xCA\xFC\x7F\x3E\x20\x18\xC4\xF1\x3E\x00\xB7\x37\xC8\xCD\x54\x1F\xD4\x93\x79\x37\xC9\xF5\x3A\x02", 0x8538),
        ZasmRubtsoff("zasm-lite-1.07", "ZAsm Lite 1.07", "lite", 0x8000,
                     "\xE5\x2A\x48\x62\xE5\xB7\xED\x42\x4D\x44\xE1\x09\x1B\x7A\xB3\x20\xFA\x22\x48\x62\xE1\xC9\xCD\xC8", 0x859D),
        ZasmRubtsoff("zasm-3.3.02", "ZAsm 3.3.02", "3.15", 0x8048,
                     "\xCA\xFC\x7F\x3E\x20\x18\xC4\xF1\x3E\x00\xB7\x37\xC8\xCD\x54\x1F\xD4\x93\x79\x37\xC9\xF5\x3A\xD3", 0x850A),
        ZasmRubtsoff("zasm-3.3.51", "ZAsm 3.3.51", "3.15", 0x8048,
                     "\xCA\xFC\x7F\x3E\x20\x18\xC4\xF1\x3E\x00\xB7\x37\xC8\xCD\x54\x1F\xD4\x93\x79\x37\xC9\xF5\x3A\xC6", 0x84FC),
        ZasmRubtsoff("zasm-3.3.final", "ZAsm 3.3.Final", "3.15", 0x8000,
                     "\xD0\xEB\x29\x44\x4D\x29\x29\x09\x06\x00\xD6\x30\x4F\x09\xEB\x23\x18\xEA\x7E\x23\xFE\x0A\x28\xFA", 0x8401),
        ZasmRubtsoff("zasm-3.80.4", "ZAsm 3.80.4", "3.15", 0x8000,
                     "\x41\x20\xE6\xE5\x3C\x2A\xB7\x66\xCD\xC1\x63\x3A\xB6\x66\xB7\x28\x05\xED\x4B\x83\x80\x09\x73\x23", 0x6D02),
        ZasmRubtsoff("zasm-3.4", "ZAsm 3.4 (ZXTA34X, Z34_04)", "3.15", 0x8130,
                     "\xC5\xD5\x7E\x23\xFE\xFF\x28\x08\xA7\x28\x05\xCD\x44\x81\x18\xF2\xD1\xC1\xF1\xC9\xFE\x18\xD2\x02", 0x8520),
        ZasmRubtsoff("zasm-x64.1", "ZAsm x64.1", "3.15", 0x8000,
                     "\x28\x15\x30\x04\xFE\x02\x30\x2D\xFE\x0D\xC8\x12\x13\x10\xE5\x3E\x0D\x01\x00\x00\xED\xB1\xC9\x7E", 0x852D),
        ZasmRubtsoff("zasm-4.0x8", "ZAsm 4.0x8", "3.15", 0x8034,
                     "\x76\x6C\x70\x0C\x66\x30\xCC\xF8\x78\xDC\x76\xDC\x7C\x7C\xCC\xCC\xC6\xC6\xCC\xFC\x30\x18\x30\x00", 0x692B),
        ZasmRubtsoff("zasm-4.x64", "ZAsm 4.x64", "3.15", 0x8000,
                     "\x28\x0E\xCD\xC8\x6B\xCD\x3B\x6E\xDD\xCB\x1A\xD6\xD7\x96\x18\x03\xCD\x6B\x67\xCD\xAB\x70\xCD\x8B", 0x6929),
        ZasmRubtsoff("zasm-4.20", "ZAsm 4.20", "3.15", 0x8000,
                     "\xD4\xD5\xD6\xD7\xD8\xD9\xDA\xDB\xDC\xDD\xDE\xDF\x72\x73\x74\x75\x66\x68\x63\x7E\x7B\x7D\x5C\x79", 0x68FB),
        Zxasm310(),
        Zxasm30(),
        LaserGenius(),
        Prometheus48(),
        Pasm30(),
        Zeus("zeus-1983", "ZEUS 1983 (Crystal)", "1983", 0xE028,
             "\x21\x6A\x5C\xA6\x77\xCD\xE3\xE4\x0F\x20\x20\x20\x5A\x45\x55\x53\x20\x5A\x38\x30\x20\x41\x53\x53", 0xFD00, "C"),
        Zeus("zeus-1.1", "ZEUS 1.1 beta (PHT)", "pht", 0xE020,
             "\x5C\x3E\xF7\x21\x6A\x5C\xA6\x77\xCD\x27\xF2\x0F\x5A\x45\x55\x53\x20\x5A\x38\x30\x20\x41\x53\x53", 0xFD00, "C"),
        Zeus("zeus-gg", "ZEUS (GG, B-disk)", "gg", 0xE028,
             "\x21\x6A\x5C\xA6\x77\xCD\xE3\xE4\x0F\x5A\x45\x55\x53\x20\x77\x69\x74\x68\x20\x42\x2D\x64\x69\x73", 0xFD00, "C"),
        Zeus("zeus-7e", "ZEUS v7.E (ZKSoft)", "pht", 0xE020,
             "\x5C\x3E\xF7\x21\x6A\x5C\xA6\x77\xCD\x27\xF2\x0F\x00\x18\x1B\xCD\xC0\xDA\xDA\x12\xE1\xC3\xC5\xE0", 0xFAAB, "Z"),
        Zeus("primus-2.9", "Primus Assembler 2.9 (Trunov)", "primus", 0x6D17, "PRIMUS ASSEMBLER Version", 0x61B1, "C"),
        Gens("gens-4", "GENS4 (HiSoft Devpac 4, tape, at 26000)", {0x88F0, "Copyright (C) HiSoft 1987"}, 0x67A8, 0x65C6),
        Gens("gens-3m21", "GENS3M21 (HiSoft Devpac 3, tape, at 49000)", {0xE001, "HISOFT GENS3M2 ASSEMBLER"}, 0xDE54, 0xBF9E),
        Gens("gens-4b", "GENS4B 4.1 (MOA B-Disk, at 30000)", {0x9A70, "MOA B-Disk version."}, 0x7928, 0x7566),
        Gens("gens-4.3t", "GENS 4.3t (Aysoft, TR-DOS)", {0x8A9F, "BETA DISK VERSION 4.3t"}, 0x689C, 0x61FB),
        ZxAsm2("zasm-2.4", "ZX-ASM 2.4", 0x806C,
               "\x70\x65\x00\x70\x00\x6D\x00\x7E\xFE\x20\x20\x08\x23\x7E\xFE\x20\x28\xFA\xB7\xC9\xFE\x06\x37\xC0", 0x6273),
        ZxAsm2("zasm-2.5", "ZX-ASM 2.5", 0x804C,
               "\x6F\x00\x70\x65\x00\x70\x00\x6D\x00\x7E\xFE\x20\x20\x08\x23\x7E\xFE\x20\x28\xFA\xB7\xC9\xFE\x06", 0x6273),
        ZxAsm2("zasm-2.6", "ZX-ASM 2.6", 0x8000,
               "\xCD\x2E\x61\x38\x65\xFE\x5F\x28\x61\x23\xFE\x23\x28\x35\xFE\x25\x28\x1B\xFE\x22\x28\x08\xFE\x24", 0x6003),
        Masm11(),
        Masm10Demo(),
        Masm2("masm-2.0", "MASM 2.0 TURBO", "2.0", {0x80E0, std::string("MASM2.0>\0", 9)}, 0x94DA, 0x92C6, 0x89D1, 0x929F),
        Masm2("masm-3.0", "MASM 3.0 MACRO", "3.0", {0x80D3, std::string("MASM128>\0", 9)}, 0x9123, 0x8C24, 0x864F, 0x8BFD),
        TasmGap("4.0", "TASM 4.0 (XL Design)", "4.0", {0x9859, "TASM4.0>"}, 0x8DD0, 0x91AC),
        TasmGap("4.4", "TASM 4.4 (KVA)", "4.0", {0x9859, "TASM4.4>"}, 0x8DD0, 0x91AC),
        TasmGap("3.0", "TASM 3.0 (Rst7)", "3", {0x9721, "TASM2.0 source file: "}, 0x8910, 0x8C82),
        TasmGap("3.2", "TASM 3.2 (Rst7)", "3", {0x9728, "TASM2.0 file: "}, 0x8910, 0x8C82),
        Tasm20(),
        TasmGap("3.5", "TASM 3.5 FLASHVERSION", "3", {0x9350, "TASM128 FLASHVERSION"}, 0x8910, 0x8C82),
        Tasm412(),
    };
    return descriptors;
}

const SyncDescriptor* FindDescriptor(const std::string& id)
{
    for (const SyncDescriptor& d : Descriptors())
        if (d.id == id)
            return &d;
    return nullptr;
}

std::vector<SyncText> ReadOtherTexts(const MachineView& machine, const SyncDescriptor& descriptor, int exceptPage)
{
    std::vector<SyncText> texts;
    if (!descriptor.textsPerPage || descriptor.family != LayoutFamily::FileImage)
        return texts;
    for (const symbols::MemoryPage& page : machine.ram)
    {
        if (static_cast<int>(page.page) == exceptPage)
            continue;
        SyncText text;
        if (ReadFileImageInPage(machine, descriptor, page.page, text) == 1)
            texts.push_back(std::move(text));
    }
    return texts;
}

SyncText ReadText(const MachineView& machine, const SyncDescriptor& descriptor)
{
    SyncText text;
    switch (descriptor.family)
    {
        case LayoutFamily::FileImage: text = ReadFileImage(machine, descriptor); break;
        case LayoutFamily::GapBuffer: text = ReadGapBuffer(machine, descriptor); break;
        case LayoutFamily::Linear: text = ReadLinear(machine, descriptor); break;
    }
    if (text.ok && descriptor.typing.rule == TypingRule::NotInText && descriptor.typing.flagMask)
    {
        const std::optional<uint8_t> flag = machine.Byte(descriptor.typing.flagAt);
        text.typing = flag && (*flag & descriptor.typing.flagMask);
    }
    return text;
}

std::vector<ProbeCandidate> Probe(const MachineView& machine)
{
    std::vector<ProbeCandidate> candidates;
    for (const SyncDescriptor& d : Descriptors())
    {
        if (d.identify.empty() ||
            !std::all_of(d.identify.begin(), d.identify.end(), [&](const Signature& s) { return Matches(machine, s); }))
            continue;
        const SyncText text = ReadText(machine, d);
        candidates.push_back({&d, text.ok ? 100 : 60,
                              text.ok ? "identified, the text reads" : "identified; " + (text.diagnostics.empty() ? text.state : text.diagnostics.back().message)});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const ProbeCandidate& a, const ProbeCandidate& b) { return a.score > b.score; });
    return candidates;
}
}  // namespace unrealasm::sync
