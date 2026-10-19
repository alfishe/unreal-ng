#include "unrealasm/sync/reader.h"

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>

#include "codecs/prometheus/prometheuscodec.h"

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
    const std::optional<uint16_t> topWord = p.endInText ? std::optional<uint16_t>(uint16_t{0}) : machine.Word(p.top);
    std::vector<uint8_t> flag;
    if (!gapStart || !gapEnd || !startWord || !topWord || !machine.Read(p.editorFlagAt, p.editorFlagLength, flag))
        return Inconsistent(text, "the text pointers are not in the memory copied");
    const uint32_t start = *startWord;
    const uint32_t top = p.endInText ? 0x10000u : *topWord;
    if (!(start <= *gapStart && *gapStart <= *gapEnd && *gapEnd <= top))
        return Inconsistent(text, "the text pointers are out of order: #" + Hex(start, 4) + " #" + Hex(*gapStart, 4) + " #" +
                                      Hex(*gapEnd, 4) + " #" + Hex(top, 4));
    text.editor = std::any_of(flag.begin(), flag.end(), [](uint8_t b) { return b != 0; });
    if (p.loadedForm && !text.editor && *gapStart == start && *gapEnd == 0xFFFF)
    {
        // as LOAD left it (MASM 1.1; an inject into 2.0 / 3.0): the file at the start, up to its end record
        std::vector<uint8_t> all;
        if (!machine.Read(static_cast<uint16_t>(start), static_cast<size_t>(0x10000 - start), all))
            return Inconsistent(text, "the text lies in a page that was not copied");
        size_t at = 0;
        long lines = 0;
        if (p.nextLineAt)
        {
            // MASM 2.0 / 3.0: FF (3.0: FF lo hi FF), lines ended by 00, FF
            at = p.editorStateLength ? 4 : 1;
            if (all.size() < at || all[0] != 0xFF)
                return Inconsistent(text, "no FF header at the text start");
            for (; at < all.size() && all[at] != 0xFF; ++at)
                lines += all[at] == 0 ? 1 : 0;
        }
        while (!p.nextLineAt && at < all.size() && all[at] != static_cast<uint8_t>(p.end[0]))
        {
            const size_t n = all[at];
            if (at + n + 1 >= all.size() || all[at + n + 1] != n)
                return Inconsistent(text, "the text is not made of length-framed lines");
            at += n + 2;
            ++lines;
        }
        if (at + p.end.size() > all.size())
            return Inconsistent(text, "no end record after the text");
        text.file.assign(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(at + p.end.size()));
        text.lines = static_cast<int>(lines);
        text.ok = true;
        text.state = "ok";
        return text;
    }
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
    if (p.loadedForm && *top == 0 && *gapStart == 0 && *gapEnd == 0 && *total == 0)
    {
        // as LOAD left it: the file at the start, up to its end record
        std::vector<uint8_t> all;
        if (!machine.Read(*start, static_cast<size_t>(0x10000 - *start), all))
            return Inconsistent(text, "the text lies in a page that was not copied");
        size_t at = 0;
        long lines = 0;
        if (p.nextLineAt)
        {
            // MASM 2.0 / 3.0: FF (3.0: FF lo hi FF), lines ended by 00, FF
            at = p.editorStateLength ? 4 : 1;
            if (all.size() < at || all[0] != 0xFF)
                return Inconsistent(text, "no FF header at the text start");
            for (; at < all.size() && all[at] != 0xFF; ++at)
                lines += all[at] == 0 ? 1 : 0;
        }
        while (!p.nextLineAt && at < all.size() && all[at] != static_cast<uint8_t>(p.end[0]))
        {
            const size_t n = all[at];
            if (at + n + 1 >= all.size() || all[at + n + 1] != n)
                return Inconsistent(text, "the text is not made of length-framed lines");
            at += n + 2;
            ++lines;
        }
        if (at + p.end.size() > all.size())
            return Inconsistent(text, "no end record after the text");
        text.file.assign(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(at + p.end.size()));
        text.lines = static_cast<int>(lines);
        text.ok = true;
        text.state = "ok";
        return text;
    }
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

/// PASM's line buffer as it goes back into the text (the routine at #7881): the trailing blanks dropped, then right
/// to left a run of 2..8 blanks that ends at a tab stop becomes a TAB
void PackPasmLine(std::vector<uint8_t>& buffer)
{
    size_t length = buffer.size();
    while (length > 0 && buffer[length - 1] == ' ')
        --length;
    buffer.resize(length);
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
}

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
        PackPasmLine(buffer);
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
/// debugger, #27-#2A post labels, #36 pocket; SYSTEM and the overlay from its source; 4.5, without a source, by texts
/// left there intact through ASSEMBLE and a catalogue), for INCLUDE "NAME",page
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
/// `idle`: the return chain of its command line (EXT): the line input's return, then the command handler's (an editor
/// key has another address there). An inject sets the editor's variables as LOAD does (the routine after a load copies
/// the header's top line, cursor line and flag into them; the editor then counts the lines from the cursor to the end)
SyncDescriptor Xas(const std::string& id, const std::string& codecVersion, uint16_t titleAt, const std::string& title,
                   std::vector<uint16_t> idle)
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
    // LD HL,(#C01F) / LD (top),HL / LD HL,(#C01D) / LD (cursor),HL / LD A,(#C021) / LD (flag),A / RET; and
    // LD HL,(cursor) / PUSH BC / LD B,H / XOR A / CPIR / DEC HL / LD (end),HL / POP BC / RET (every version, 4.18-9.10)
    const std::vector<int> afterLoad = {0x2A, 0x1F, 0xC0, 0x22, -1, -1, 0x2A, 0x1D, 0xC0, 0x22, -1, -1, 0x3A, 0x21, 0xC0, 0x32, -1, -1, 0xC9};
    const std::vector<int> textEnd = {0x2A, -1, -1, 0xC5, 0x44, 0xAF, 0xED, 0xB1, 0x2B, 0x22, -1, -1, 0xC1, 0xC9};
    d.fileImage.loadVariables = {{afterLoad, 4, LoadVariable::From::Header, 31, 2},
                                 {afterLoad, 10, LoadVariable::From::Header, 29, 2},
                                 {afterLoad, 16, LoadVariable::From::Header, 33, 1},
                                 {textEnd, 10, LoadVariable::From::TextEnd, 0, 2}};
    d.typing = {TypingRule::NotInText, 0, 0};
    d.labelScanner = "xas-table";
    d.projectOnDisk = true;   // LTEXT reads the disk
    d.idleChain = std::move(idle);
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
    if (base == 0x0B)
    {
        // 1.3 / 1.3i: a text is written while the editor waits for a key, its cursor line written into the text (the
        // main loop's CALL WTKPR returns to #87F5 / #87CB). As STORM's own LOAD (source XED: ABLINE, ETEXT, INIT)
        // the inject resets the editor, here through STORM's PGUP (control key 4): from the second line with the
        // cursor on the first screen line PGUP goes back to the text's start and redraws the screen from it (PSCR0,
        // which sets the cursor line, ABLINE / AELINE and the line buffer). The variables by STORM 1.3's source names
        const std::vector<int> scrp0 = {0xFD, 0x21, 0x00, 0x00, 0x22, -1, -1, 0xAF, 0x57, 0x2B, 0x5E, 0x1C, 0x28, -1, 0xCB,
                                        0xB3, 0xFD, 0x23, 0xED, 0x52, 0x5E, 0x1C, 0xC2, -1, -1, 0xFD, 0x22, -1, -1};
        const std::vector<int> init2 = {0xAF, 0x32, -1, -1, 0x32, -1, -1, 0x21, 0x02, 0xC0, 0x11, -1, -1, 0x01, 0x08, 0x00, 0xED, 0xB0};
        const std::vector<int> init = {0xF3, 0xCD, -1, -1, 0xDD, 0x36, 0x00, 0x82, 0xDD, 0xCB, 0x04, 0x86, 0xAF, 0x67, 0x6F, 0x22, -1, -1};
        const std::vector<int> start = {0xF3, 0xED, 0x73, -1, -1, 0xCD, -1, -1, 0xDD, 0x21, -1, -1};
        const std::vector<int> inp4 = {0xCD, -1, -1, 0xAF, 0xD3, 0xFE, 0x3A, -1, -1, 0xFE, 0x14, 0xDA};
        using From = LoadVariable::From;
        d.fileImage.loadVariables = {
            {scrp0, 5, From::SecondLine, 0, 2},          // ABLINE: the cursor line, the second
            {scrp0, 27, From::Constant, 0, 2, 1},        // NLINE: its number from 0
            {init2, 5, From::Constant, 0, 1, 0},         // MBSTAT: no block marked
            {init2, 11, From::PageBytes, 0x02, 8},       // WNAME: the name SAVE offers, the text's (TXNAME)
            {init, 16, From::Constant, 0, 2, 0},         // CACHEST
            {start, 10, From::Constant, 0, 1, 1, 2},     // VAR+2 (PY): the cursor on the first screen line
            {start, 10, From::SetBits, 0, 1, 0x02, 4},   // VAR+4 bit 1: the line buffer is in the text
            {inp4, 7, From::Constant, 0, 1, 4},          // LASTK: PGUP
            {start, 10, From::SetBits, 0, 1, 0x01, 0},   // VAR+0 bit 0: a key was pressed
        };
        d.fileImage.changedVariables = {
            {init2, 5, From::SetBits, 0, 1, 0x10},       // MBSTAT bit 4: a line written since LOAD / SAVE
            {start, 10, From::Constant, 0, 1, 0x02, 4},  // VAR+4 bit 1 clear: the cursor line typed in, not in the text
        };
        d.idleChain = {0x87F5};
        d.idleAlternatives = {{0x87CB}};
        d.idleWhere = "a key in its editor";
    }
    else
    {
        // 1.0beta the same way (its code read in unreal-ng): SCRP0 with LD D,0 for XOR A / LD D,A, LASTK the system
        // variable (23560); no name in its page, no MBSTAT (no block or "changed" state to clear). The main loop's
        // CALL returns to #866C
        const std::vector<int> scrp0 = {0xFD, 0x21, 0x00, 0x00, 0x22, -1, -1, 0x16, 0x00, 0x2B, 0x5E, 0x1C, 0x28, -1, 0xCB,
                                        0xB3, 0xFD, 0x23, 0xED, 0x52, 0x5E, 0x1C, 0xC2, -1, -1, 0xFD, 0x22, -1, -1};
        const std::vector<int> init = {0xF3, 0xCD, -1, -1, 0xDD, 0x36, 0x00, 0x82, 0xDD, 0xCB, 0x04, 0x86, 0xAF, 0x67, 0x6F, 0x22, -1, -1};
        const std::vector<int> start = {0xF3, 0xED, 0x73, -1, -1, 0xCD, -1, -1, 0xDD, 0x21, -1, -1};
        const std::vector<int> inp4 = {0xCD, -1, -1, 0xAF, 0xD3, 0xFE, 0x3A, -1, -1, 0xFE, 0x14, 0xDA};
        using From = LoadVariable::From;
        d.fileImage.loadVariables = {
            {scrp0, 5, From::SecondLine, 0, 2},          // ABLINE: the cursor line, the second
            {scrp0, 27, From::Constant, 0, 2, 1},        // NLINE: its number from 0
            {init, 16, From::Constant, 0, 2, 0},         // CACHEST
            {start, 10, From::Constant, 0, 1, 1, 2},     // VAR+2 (PY): the cursor on the first screen line
            {start, 10, From::SetBits, 0, 1, 0x02, 4},   // VAR+4 bit 1: the line buffer is in the text
            {inp4, 7, From::Constant, 0, 1, 4},          // LASTK: PGUP
            {start, 10, From::SetBits, 0, 1, 0x01, 0},   // VAR+0 bit 0: a key was pressed
        };
        d.fileImage.changedVariables = {
            {start, 10, From::Constant, 0, 1, 0x02, 4},  // VAR+4 bit 1 clear: the cursor line typed in, not in the text
        };
        d.idleChain = {0x866C};
        d.idleWhere = "a key in its editor";
    }
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
    d.linear.injectable = true;
    d.linear.resetFrom = 0x882B;   // the cursor line and the marks, up to the end word
    d.linear.resetTo = 0x8837;
    d.linear.zeroWords = {0x8827};   // the cursor's line number (from 0), or the editor counts on from the old one
    d.linear.zeroBytes = {0x8630};   // and its column
    d.linear.nameAt = 0x883F;        // the work name in "A:STUB    .asm": SAVE writes under it (seen in unreal-ng)
    d.typing = {TypingRule::NotInText, 0, 0};
    // A text is written at its COMMAND prompt (CS+SS in the editor): its key wait returns through #C404, #E630, #E6F0
    // (code in the page at #C000 while ZAsm runs)
    d.idleChain = {0xC404, 0xE630, 0xE6F0};
    d.idleWhere = "its COMMAND prompt (CS+SS)";
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
    // A text is written at its menu prompt (MASM128>) as 1.1's LOAD leaves one (the demo loads no file): the key wait
    // returns through #8736 there (the editor's stack holds others); the line buffer's tail is zeros only in the menu
    d.gapBuffer.loadedForm = true;
    d.gapBuffer.nameAt = 0x8694;   // the work name in the menu's "Work name:" text: Save writes under it
    d.idleChain = {0x8736};
    d.idleZeroAt = 0x8600;
    d.idleZeroLength = 32;
    d.idleWhere = "its MASM128> prompt";
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
    d.gapBuffer.loadedForm = true;
    d.typing = {TypingRule::NotInText, 0, 0};
    // A text is written at its menu prompt (MASM128>): the key wait returns through #85C8 there and in the editor; the
    // line buffer is zeros only in the menu
    d.idleChain = {0x85C8};
    d.idleZeroAt = 0x851A;
    d.idleZeroLength = 31;
    d.idleWhere = "its MASM128> prompt";
    return d;
}

/// MASM 2.0 / 3.0 (asm-synchronizer.md §7.12): a gap buffer from the address its files carry (#94DA / #9123) to
/// #FFFF, lines ended by 00. The gap start, the editor flag and the gap end are operands its code rewrites (the gap
/// closing routine LD DE,gs / LD HL,linebuf / LD A,flag / ... / LD HL,ge); while the flag is set the cursor line is in
/// the line buffer as text and the text goes on at the word nextLineAt
SyncDescriptor Masm2(const std::string& id, const std::string& title, const std::string& codecVersion, Signature identify,
                     uint16_t fixedStart, uint16_t routine, uint16_t lineBuffer, uint16_t nextLineAt, std::vector<uint16_t> idleChain,
                     uint16_t nameAt)
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
    d.gapBuffer.loadedForm = true;   // an inject: the file at the start, the gap start there, the gap end #FFFF
    d.gapBuffer.nameAt = nameAt;     // the work name in the menu's "Work file:" text: Save writes under it
    d.idleChain = std::move(idleChain);
    d.idleWhere = "its menu prompt";
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
    // the buffer's last byte: a line of more than 96 characters left at the command line reaches into the tail
    d.gapBuffer.editorFlagAt = static_cast<uint16_t>(lineBuffer + 31);
    d.gapBuffer.editorFlagLength = 1;
    d.gapBuffer.end = std::string("\xFF\xFF", 2);
    d.typing = {TypingRule::NotInText, 0, 0};
    return d;
}

/// A TASM 4.0 / 3.x text written at its command line (find-idle.py: its key wait returns through `chain`) in the form
/// the command line keeps after the editor, the "changed" byte (#FF after an edit, 0 after LOAD / SAVE) and the work
/// name (in the menu's WORK> text: Assemble takes the text in memory under it)
SyncDescriptor TasmInjectable(SyncDescriptor d, std::vector<uint16_t> chain, uint16_t changedAt, uint16_t nameAt)
{
    d.gapBuffer.commandLineForm = true;
    d.gapBuffer.changedAt = changedAt;
    d.gapBuffer.nameAt = nameAt;
    d.idleChain = std::move(chain);
    d.idleWhere = "its command line";
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
    return TasmInjectable(std::move(d), {0x92CB, 0x807C}, 0x921D, 0x9241);
}

/// ZAsm 3.2x and later, Rubts0FF's Pentagon 512 versions (asm-synchronizer.md §7.9): the buffer below #C000 from its
/// start word, the part above #C000 in RAM page 30, the end word right after the start word (the words before them
/// follow the cursor and the screen); SAVE writes a ";*" position line first. Each version has its table elsewhere;
/// 3.2x, 3.3.02 and 3.3.51 share the code at #8048 up to the address of a variable (its low byte tells them apart)
SyncDescriptor ZasmRubtsoff(const std::string& id, const std::string& title, const std::string& codecVersion, uint16_t signatureAt,
                            const char* signature, uint16_t startAt, std::vector<uint16_t> idleChain = {}, uint16_t nameAt = 0)
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
    if (!idleChain.empty())
    {
        // A text is written at its COMMAND prompt (CS+SS in the editor; the key wait's chain by find-idle.py) as LOAD
        // leaves one: the four words before the start word (the cursor and the screen) at the start, the line number
        // before them and the column 0
        d.linear.injectable = true;
        d.linear.resetFrom = static_cast<uint16_t>(startAt - 8);
        d.linear.resetTo = startAt;
        d.linear.zeroWords = {static_cast<uint16_t>(startAt - 10)};   // the cursor's line number (from 0)
        d.linear.zeroBytes = {static_cast<uint16_t>(startAt - 14)};   // and its column
        // the work name in "A:STUB    .asm": SAVE writes under it (seen in unreal-ng); from #C000 in page 27 (not
        // the text's page 30), mapped there at the prompt but in Lite
        d.linear.nameAt = nameAt;
        if (nameAt >= 0xC000)
            d.linear.namePage = 27;
        d.idleChain = std::move(idleChain);
        d.idleWhere = "its COMMAND prompt (CS+SS)";
    }
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
    d.identify = {{0x8171, std::string("\x6B\xC0\x00\x00\x0A\x0A\x8E\x00\x20\x80\x17\x00\x20\x01\x8F\x00\xF3\xFD\xE5\xDD\xE5\xE5\xD5\xC5", 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.startAt = 0x868F;
    d.linear.endAt = 0x869E;
    d.linear.upperPage = 6;
    d.typing = {TypingRule::NotInText, 0, 0};
    // A text is written at its COMMAND prompt (CS+SS in the editor: the key wait returns through #C4D1, #D58A) as
    // LOAD leaves one: the five words after the start word and the one at #869C at the start, the line and column 0
    d.linear.injectable = true;
    d.linear.resetFrom = 0x8691;
    d.linear.resetTo = 0x869B;
    d.linear.resetAlso = {0x869C};
    d.linear.zeroWords = {0x868D};   // the cursor's line number (from 0)
    d.linear.zeroBytes = {0x867F};   // and its column
    d.linear.nameAt = 0x89B7;        // the work name in "A:STUB    .A  ": SAVE writes under it (seen in unreal-ng)
    d.idleChain = {0xC4D1, 0xD58A};
    d.idleWhere = "its COMMAND prompt (CS+SS)";
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

/// GENS4 at 26000 also takes a text at its command prompt (>): its key wait returns through #685D, #68DD, #67D9 (the
/// line input of I: #685D, #69C6); the text from the start word, the end word one past it, as G leaves them
SyncDescriptor Gens4Tape()
{
    SyncDescriptor d = Gens("gens-4", "GENS4 (HiSoft Devpac 4, tape, at 26000)", {0x88F0, "Copyright (C) HiSoft 1987"}, 0x67A8, 0x65C6);
    d.linear.injectable = true;
    d.idleChain = {0x685D, 0x68DD, 0x67D9};
    d.idleWhere = "its > prompt";
    return d;
}

/// Another GENS build at its command prompt (>), as GENS4: the key wait's chain found by find-idle.py
SyncDescriptor GensInjectable(SyncDescriptor d, std::vector<uint16_t> chain)
{
    d.linear.injectable = true;
    d.idleChain = std::move(chain);
    d.idleWhere = "its > prompt";
    return d;
}

/// PROMETHEUS (asm-synchronizer.md §7.17, from the 48K edition's reconstructed source): the records from
/// sourceBufferAccessLine (fixedStart) to twelve bytes below the symbol table (the operand of varcSymbolTablePt), the
/// table up to the operand of varcCodeEndPt; the save adds FF XOR the records and FF between. An inject writes them
/// so, below U-TOP (varcUTop's operand; 0 = the end of memory), and sets the active line (varcSourceBufferActiveLine)
/// and the block margins (varcSelectedBlockStart / End) to the start
struct PrometheusBuild
{
    const char* id;
    const char* title;
    uint16_t signatureAt;
    std::string signature;
    uint16_t start, tableAt, endAt, uTopAt;
    std::vector<uint16_t> resetWords;
    std::vector<uint16_t> idleChain;   // its editor's key wait
    int upperPage;
};

SyncDescriptor Prometheus(const PrometheusBuild& b)
{
    SyncDescriptor d;
    d.id = b.id;
    d.title = b.title;
    d.codec = "prometheus";
    d.version = "";
    d.extension = "C";
    d.identify = {{b.signatureAt, b.signature}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.fixedStart = b.start;
    d.linear.prometheusTableAt = b.tableAt;
    d.linear.prometheusEndAt = b.endAt;
    d.linear.upperPage = b.upperPage;
    d.linear.injectable = true;
    d.linear.limitAt = b.uTopAt;
    d.linear.resetWords = b.resetWords;
    d.typing = {TypingRule::NotInText, 0, 0};
    d.idleChain = b.idleChain;
    d.idleWhere = "its editor";
    return d;
}

/// The 48K tape edition installed at 24000: the records from #9C28, varcSymbolTablePt #87D7, varcCodeEndPt #881A,
/// U-TOP #7946, the active line #826D, the block #80E8 / #80EB; the key wait (CALL #86AE from #8626, called from
/// #7CFA) leaves #8629, #7CFD (the word between is the key scan's own). shift: the same code moved up (the TR-DOS
/// adaptation, PROMBD.C installed at its 25000: #514); the signature's three addresses move with it
SyncDescriptor Prometheus48(const char* id = "prometheus-48", const char* title = "PROMETHEUS 48K (Proxima, at 24000)",
                            uint16_t shift = 0)
{
    std::string signature("\xC9\xF5\xD6\x34\x5F\x16\x37\xCD\xE6\x82\xC5\x21\xE4\x8A\xAF\x4F\x23\x0C\xBE\x20\xFB\x11\xD1\x8A", 24);
    for (const size_t at : {size_t(8), size_t(12), size_t(22)})
    {
        const auto word = static_cast<uint16_t>((static_cast<uint8_t>(signature[at]) | static_cast<uint8_t>(signature[at + 1]) << 8) + shift);
        signature[at] = static_cast<char>(word & 0xFF);
        signature[at + 1] = static_cast<char>(word >> 8);
    }
    const auto at = [shift](uint16_t a) { return static_cast<uint16_t>(a + shift); };
    SyncDescriptor d = Prometheus({id, title, at(0x8000), signature, at(0x9C28), at(0x87D7), at(0x881A), at(0x7946),
                                   {at(0x826D), at(0x80E8), at(0x80EB)}, {at(0x8629), at(0x7CFD)}, 0});
    if (!shift)
    {
        // its installer asks the address and moves the code there (the TR-DOS adaptation is that move, found so too)
        d.installedAt = 24000;
        d.relocatedWords = {8, 12, 22};
    }
    return d;
}

/// PROMETHEUS 128 (Proxima 1993; the tape's PROM128LNG / MDM / SHT installed at 24000, "D40/D80" n): the text in a
/// 64K space of its own over RAM pages 1, 3, 4, 6 (address bits 15-14 pick the page, mapped at #C000: LNG #5F85); the
/// records from #C01A, i.e. page 6, so a text stays within that page (16K); no U-TOP but the space's end. Its
/// variables found by the 48K code around them: L varcSymbolTablePt #9186, varcCodeEndPt #9217, the active line
/// #8BF7, the block #8A59 / #8A5C; M and S are L shorter (by #237 / #5CC up to the block, 4 bytes more after it)
SyncDescriptor Prometheus128(const char* id, const char* title, uint16_t shift, uint16_t tableShift, const char* signature)
{
    const auto at = [shift](uint16_t a) { return static_cast<uint16_t>(a - shift); };
    const auto late = [tableShift](uint16_t a) { return static_cast<uint16_t>(a - tableShift); };
    SyncDescriptor d = Prometheus({id, title, at(0x8955), std::string(signature, 24), 0xC01A, late(0x9186), late(0x9217), 0,
                                   {at(0x8BF7), at(0x8A59), at(0x8A5C)}, {at(0x8FD3), at(0x864E)}, 6});
    d.installedAt = 24000;   // its installer moves the code too; the text stays in page 6
    d.relocatedWords = {8, 12, 22};
    return d;
}

/// Power Assembler 3.0 (asm-synchronizer.md §7.18, from its code): CR LF text from #79AF to #1A; the cursor line's
/// address at #74E9, its 64-byte line buffer at #7342, packed into TABs when it goes back (the routine at #7881); an
/// inject leaves the editor as ZAP (#7301) and GET (#6E5E) do
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
    d.linear.injectable = true;
    d.linear.pasmLengthAt = 0x7747;
    d.linear.pasmLastLineAt = 0x7752;
    d.linear.pasmLineLengthAt = 0x7802;
    d.linear.pasmTopAt = 0x76E9;
    d.linear.pasmRedrawAt = 0x73B6;   // the operand of LD A,0 at #73B5: the editor redraws on its next entry
    d.linear.pasmNameAt = 0x7391;
    // what ZAP clears (#7301): the cursor's line, its column and row, the block marks; #6525 the cursor drawn last
    d.linear.zeroWords = {0x775B, 0x7383, 0x6525, 0x63CA, 0x6C54, 0x6DFE, 0x6E00};
    d.linear.zeroBytes = {0x6CD0, 0x6D31, 0x6B48};
    d.typing = {TypingRule::NotInText, 0, 0};
    // its editor's key wait (CALL #65F7 at #73CC, from the editor's entry called at #61A3)
    d.idleChain = {0x73CF, 0x61A6};
    d.idleWhere = "its editor";
    return d;
}

/// Laser Genius 1.04 Beta (asm-synchronizer.md §7.16): paragraphs from (#A975) to (#A973), then FF FF (with the hash
/// extensions too: the start word says #737E instead of #7F2E); the disk file adds its #AF header. Identified by its code at #AC00;
/// an inject writes the paragraphs and FF FF from (#A975) and the end word, up to (#A971) (no "changed" flag)
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
    d.linear.startAt = 0xA975;   // #7F2E; SET SPACE moves it
    d.linear.endAt = 0xA973;
    d.linear.limitAt = 0xA971;
    // its screen lines follow the text (FF, the lines, FF at (#A9DC)); #A9D4-#A9DE point into them (LOAD moves them)
    d.linear.followLastAt = 0xA9DC;
    d.linear.followPointers = {0xA9D4, 0xA9D6, 0xA9D8, 0xA9DA, 0xA9DC, 0xA9DE};
    d.linear.upperPage = 0;
    d.linear.laserGeniusFile = true;
    d.linear.editorStateAt = 5;
    d.linear.editorStateLength = 4;
    d.linear.injectable = true;
    // its editor: the key wait (CALL #B071) returns to #AB04, the loop to the ROM's #1303 (an assembler error waits
    // under #AB0C); a paragraph being typed stays out of the text until ENTER
    d.idleChain = {0xAB04, 0x1303};
    d.idleWhere = "its editor";
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

/// ZEUS 1983 at 57344 also takes a text at its prompt: its line input returns through #F6E5, #E0D5; the records from
/// the start word, the end word at the last FF, as "o" leaves them
SyncDescriptor Zeus1983()
{
    SyncDescriptor d = Zeus("zeus-1983", "ZEUS 1983 (Crystal)", "1983", 0xE028,
             "\x21\x6A\x5C\xA6\x77\xCD\xE3\xE4\x0F\x20\x20\x20\x5A\x45\x55\x53\x20\x5A\x38\x30\x20\x41\x53\x53", 0xFD00, "C");
    d.identify.push_back({0xE040, std::string("EMBLER\r\x7F 1983 Crystal")});   // ZEUS 128 too; ZEUS+ 3.1 says 1993
    d.linear.injectable = true;
    d.idleChain = {0xF6E5, 0xE0D5};
    d.idleWhere = "its prompt";
    return d;
}

/// ZEUS+ 3.1 (H.S.O. 1993, resident; ZEUS31HS: RUN "ZEUS+3.1") is 1983's code with 1983's title up to its second
/// line ("1993 Crystal & H.S.O. Ver 3.1"), its labels of 6 characters (error 1 from the seventh)
SyncDescriptor Zeus31()
{
    SyncDescriptor d = Zeus1983();
    d.id = "zeus-3.1";
    d.title = "ZEUS+ 3.1 (H.S.O. 1993)";
    d.version = "3.1";
    d.identify.back() = {0xE040, std::string("EMBLER\r\x7F 1993 Crystal & H.S.O.")};
    return d;
}

/// ZEUS GG (the B-disk build of the 1983 code) takes a text at its prompt as 1983 does: its line input returns through
/// #F6E5, #E0D5 (find-idle.py)
SyncDescriptor ZeusGg()
{
    SyncDescriptor d = Zeus("zeus-gg", "ZEUS (GG, B-disk)", "gg", 0xE028,
             "\x21\x6A\x5C\xA6\x77\xCD\xE3\xE4\x0F\x5A\x45\x55\x53\x20\x77\x69\x74\x68\x20\x42\x2D\x64\x69\x73", 0xFD00, "C");
    d.linear.injectable = true;
    d.idleChain = {0xF6E5, 0xE0D5};
    d.idleWhere = "its prompt";
    return d;
}

/// The ports that kept ZEUS 1983's code at #E000 and its words at #FD00 (asm-synchronizer.md §7.14, 2026-10-17): each
/// told by its title at #E028, its line input returning through #F6E5, #E0D5 like 1983's (2.2SI's own input #6EAC).
/// ZEUS 128 (Stars Soft 1995, its menu's first line) keeps 1983's title: zeus-1983 reads it
SyncDescriptor ZeusPort(const char* id, const char* title, const char* signature, std::vector<uint16_t> idleChain = {0xF6E5, 0xE0D5})
{
    SyncDescriptor d = Zeus(id, title, "1983", 0xE028, signature, 0xFD00, "C");
    d.linear.injectable = true;
    d.idleChain = std::move(idleChain);
    d.idleWhere = "its prompt";
    return d;
}

/// ZEUS 1.1 beta (PHT) at its prompt: the v7.E code's line input, returning through #F555, #E0F7 (find-idle.py)
SyncDescriptor Zeus11()
{
    SyncDescriptor d = Zeus("zeus-1.1", "ZEUS 1.1 beta (PHT)", "1.1", 0xE020,
             "\x5C\x3E\xF7\x21\x6A\x5C\xA6\x77\xCD\x27\xF2\x0F\x5A\x45\x55\x53\x20\x5A\x38\x30\x20\x41\x53\x53", 0xFD00, "C");
    d.linear.injectable = true;
    d.idleChain = {0xF555, 0xE0F7};
    d.idleWhere = "its prompt";
    return d;
}

/// ZEUS v7.E (ZKSoft) at its prompt: its line input returns through #F555, #E0F7 (find-idle.py)
SyncDescriptor Zeus7e()
{
    SyncDescriptor d = Zeus("zeus-7e", "ZEUS v7.E (ZKSoft)", "pht", 0xE020,
             "\x5C\x3E\xF7\x21\x6A\x5C\xA6\x77\xCD\x27\xF2\x0F\x00\x18\x1B\xCD\xC0\xDA\xDA\x12\xE1\xC3\xC5\xE0", 0xFAAB, "Z");
    d.linear.injectable = true;
    d.idleChain = {0xF555, 0xE0F7};
    d.idleWhere = "its prompt";
    return d;
}

/// Primus 2.9 also takes a text at its prompt (or its BETA-UNIT menu): its key wait returns through #6857 / #6858,
/// then #6836 / #6851 (not while it assembles); the records from the start word, the end word at the last FF
SyncDescriptor Primus29()
{
    SyncDescriptor d = Zeus("primus-2.9", "Primus Assembler 2.9 (Trunov)", "primus", 0x6D17, "PRIMUS ASSEMBLER Version", 0x61B1, "C");
    d.linear.injectable = true;
    d.idleChain = {0x6857, 0x6836};
    d.idleAlternatives = {{0x6858, 0x6836}, {0x6857, 0x6851}, {0x6858, 0x6851}};
    d.idleWhere = "its prompt";
    return d;
}

/// ZX-ASM 2.4 / 2.5 / 2.6 (asm-synchronizer.md §7.9): plain text (blank runs only) in one buffer, the start word at
/// the program's load address + 3, the end word after it; the part above #C000 in RAM page 0 (the 48K memory). 2.6
/// keeps its ";*" line in the buffer and saves it as text
SyncDescriptor ZxAsm2(const std::string& id, const std::string& title, uint16_t signatureAt, const char* signature,
                      uint16_t startAt, std::vector<uint16_t> idleChain = {}, uint16_t lineEnd = 0, uint16_t columnCopy = 0)
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
    if (!idleChain.empty())
    {
        // A text is written at its menu (it starts there with an empty text; the editor's stack holds others) as it
        // starts: the second end word after the end word at the end (an inserted line moves the memory up to it: an
        // old one moved the program's code), the cursor line's and the screen's pointers at the start, the line
        // number and the column 0, the cursor line's buffer (from the start word + #17 to lineEnd) blank, the
        // column's copy and the row after it 0 (else the editor writes the old line into the new text)
        d.linear.injectable = true;
        d.linear.endAlso = {static_cast<uint16_t>(startAt + 4)};
        d.linear.resetAlso = {static_cast<uint16_t>(startAt + 0x0A), static_cast<uint16_t>(startAt + 0x0D)};
        d.linear.zeroWords = {static_cast<uint16_t>(startAt + 0x0F)};
        d.linear.zeroBytes = {static_cast<uint16_t>(startAt + 0x12), columnCopy, static_cast<uint16_t>(columnCopy + 1)};
        d.linear.blankFrom = static_cast<uint16_t>(startAt + 0x17);
        d.linear.blankTo = lineEnd;
        d.idleChain = std::move(idleChain);
        d.idleWhere = "its menu";
    }
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
    d.version = "3.00";   // 3.0's table; the backend writes what 3.0 itself lacks (& | ! . functions, REPT ...) otherwise
    d.extension = "C";
    d.identify = {{0x8000, std::string("\x57\xCD\x38\x79\xCD\xE0\x7A\xD4\xCD\x77\x82\x57\x3A\xB6\x7B\xFE\x01\xCC\xD3\x77\xB7\x28\x13\x3A", 24)}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::Linear;
    d.linear.startAt = 0x61C6;
    d.linear.endAt = 0x61C8;
    d.linear.upperPage = 6;
    d.typing = {TypingRule::NotInText, 0, 0};
    // A text is written at its COMMAND prompt (CS+SS in the editor: the key wait's CALL returns to #6986 there, to
    // #6ABD in the editor) as LOAD leaves one: the table's other words that hold the start at the start, the second
    // end word at the end, the cursor's line and column 0
    d.linear.injectable = true;
    d.linear.resetAlso = {0x61D0, 0x61D4, 0x61DB, 0x61DD};
    d.linear.endAlso = {0x61CA};     // a second end word, as LOAD leaves it (else typing a line crashes the editor)
    d.linear.zeroWords = {0x61D6};   // the cursor's line (from 0): the editor counts on from the old one
    d.linear.zeroBytes = {0x61D9};   // and its column
    d.idleChain = {0x6986};
    d.idleWhere = "its COMMAND prompt (CS+SS)";
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
    d.gapBuffer.cursorLine = 0x94CE;
    d.gapBuffer.loadedForm = true;
    d.gapBuffer.changedAt = 0x9BA6;   // #FF after an edit, 0 after LOAD / SAVE (TASM asks to save before it drops the text)
    d.gapBuffer.nameAt = 0x9C10;      // the work name, in the menu's "Work name:" text: Assemble takes the text in memory under it
    d.gapBuffer.lineBuffer = 0x928A;
    d.gapBuffer.lineBufferLength = 128;
    d.gapBuffer.lineCodePage = encoding::CodePage::Cp866;
    d.gapBuffer.end = std::string("\xFF\xFF", 2);
    d.typing = {TypingRule::InLineBuffer, 0, 0};
    // A text is written at its command line (TASM128>): its key wait returns there through #9CE9, #80BC (the editor's
    // #9EA8, #80BC)
    d.idleChain = {0x9CE9, 0x80BC};
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
    std::map<int, std::vector<uint8_t>> changed;
    if (plan.page >= 0)
        changed[plan.page] = plan.bytes;
    for (const InjectPlan::Poke& poke : plan.pokes)
    {
        auto it = changed.find(poke.page);
        if (it == changed.end())
            it = changed.emplace(poke.page, snapshot.pages.at(poke.page)).first;
        it->second[poke.offset] = poke.value;
    }
    if (!containers::WriteSnapshot(snapshot, changed, out, error))
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
    if (d.idleZeroLength)
    {
        std::vector<uint8_t> zero;
        if (!machine.Read(d.idleZeroAt, d.idleZeroLength, zero) || std::any_of(zero.begin(), zero.end(), [](uint8_t b) { return b != 0; }))
        {
            why = d.title + " is not waiting for a command (" + (d.idleWhere.empty() ? "in its editor" : d.idleWhere) + ")";
            return false;
        }
    }
    std::vector<std::vector<uint16_t>> chains = {d.idleChain};
    chains.insert(chains.end(), d.idleAlternatives.begin(), d.idleAlternatives.end());
    for (const std::vector<uint16_t>& chain : chains)
    {
        const size_t n = chain.size();
        for (size_t at = 0; at + 2 * n <= stack.size(); ++at)
        {
            bool all = true;
            for (size_t k = 0; k < n && all; ++k)
                all = static_cast<uint16_t>(stack[at + 2 * k] | (stack[at + 2 * k + 1] << 8)) == chain[k];
            if (all)
                return true;
        }
    }
    why = d.title + " is not waiting for a command (" + (d.idleWhere.empty() ? "assembling, saving, loading, or in its editor" : d.idleWhere) + ")";
    return false;
}

namespace
{
/// The text is one the caller wrote itself: its lines (after the header, which the editor rewrites) are those of a
/// file in `written`
bool WrittenBefore(const SyncText& text, const std::vector<std::vector<uint8_t>>& written, size_t headerSize)
{
    return std::any_of(written.begin(), written.end(), [&](const std::vector<uint8_t>& file) {
        return file.size() == text.file.size() && file.size() >= headerSize &&
               std::equal(file.begin() + static_cast<std::ptrdiff_t>(headerSize), file.end(), text.file.begin() + static_cast<std::ptrdiff_t>(headerSize));
    });
}
}  // namespace

namespace
{
/// The address a LoadVariable's pattern gives in the code below #C000 (with its offset); nullopt when the pattern is
/// not there exactly once
std::optional<uint16_t> VariableAddress(const std::vector<uint8_t>& code, const LoadVariable& v, size_t& places)
{
    std::vector<size_t> found;
    for (size_t at = 0; at + v.pattern.size() <= code.size(); ++at)
        if (std::equal(v.pattern.begin(), v.pattern.end(), code.begin() + static_cast<std::ptrdiff_t>(at),
                       [](int want, uint8_t got) { return want < 0 || want == got; }))
            found.push_back(at);
    places = found.size();
    if (found.size() != 1)
        return std::nullopt;
    return static_cast<uint16_t>((code[found[0] + v.at] | (code[found[0] + v.at + 1] << 8)) + v.offset);
}
}  // namespace

namespace
{
/// TASM 4.12: the file as LOAD leaves it at the text start, the gap pointers, the line count and the cursor line 0,
/// all as single bytes at their CPU addresses (the text runs from the #8000 window into the page at #C000)
InjectPlan PlanInjectLoaded(const MachineView& machine, const SyncDescriptor& d, const std::vector<uint8_t>& file, const InjectOptions& options)
{
    InjectPlan plan;
    std::string why;
    if (!Idle(machine, d, why) && !(options.force && d.idleChain.empty()))
    {
        plan.error = why;
        return plan;
    }
    const GapBufferParams& p = d.gapBuffer;
    plan.previous = ReadText(machine, d);
    // without a "changed" flag (MASM) a text in memory that is not empty and not the caller's own counts as changed
    const bool changed = p.changedAt ? (machine.Byte(p.changedAt).value_or(0) & p.changedMask) != 0
                                     : plan.previous.file.size() > p.end.size();
    if (plan.previous.ok && changed && !options.force && !WrittenBefore(plan.previous, options.written, 0))
    {
        plan.error = "the text in " + d.title + (p.changedAt ? " has changes not saved" : " may have changes not saved (it keeps no flag)") +
                     " (force overwrites them)";
        return plan;
    }
    const auto start = p.fixedStart ? std::optional<uint16_t>(p.fixedStart) : machine.Word(p.start);
    if (!start)
    {
        plan.error = "the text pointers of " + d.title + " are not in memory";
        return plan;
    }
    if (*start + file.size() + 3 > 0xFFFF)
    {
        plan.error = "the text takes " + std::to_string(file.size()) + " bytes; " + d.title + " holds " + std::to_string(0xFFFF - *start);
        return plan;
    }
    const auto poke = [&](uint32_t at, uint8_t value) {
        const int window = machine.windows[(at >> 14) & 3];
        if (window < 0 || at < 0x4000)
            return false;
        plan.pokes.push_back({window, static_cast<uint16_t>(at & (kPage - 1)), value});
        return true;
    };
    bool ok = true;
    // TASM 2.0: a line feed at the start, the plain text after it, #1A #1A behind it
    const uint32_t base = *start + (p.plainTextFile ? 1u : 0u);
    for (size_t k = 0; k < file.size(); ++k)
        ok = poke(base + static_cast<uint32_t>(k), file[k]) && ok;
    if (p.plainTextFile)
        ok = poke(*start, 0x0A) && poke(base + static_cast<uint32_t>(file.size()), 0x1A) &&
             poke(base + static_cast<uint32_t>(file.size()) + 1u, 0x1A) && ok;
    if (p.fixedStart)
    {
        // MASM: the gap start at the start, the gap end at #FFFF
        ok = poke(p.gapStart, static_cast<uint8_t>(*start & 0xFF)) && poke(p.gapStart + 1u, static_cast<uint8_t>(*start >> 8)) && ok;
        ok = poke(p.gapEnd, 0xFF) && poke(p.gapEnd + 1u, 0xFF) && ok;
        if (p.nextLineAt)
            ok = poke(p.editorFlagAt, 0) && ok;   // MASM 2.0 / 3.0: the flag an operand of their code, 0 = not in the editor
    }
    else if (p.commandLineForm)
    {
        // TASM 4.0 / 3.x: the gap start on the end record (2.0: on the text's last byte), the gap end and the top
        // #FFFF, the editor flag clear
        const uint32_t gap = *start + static_cast<uint32_t>(file.size() - std::min(file.size(), p.end.size()));
        ok = poke(p.gapStart, static_cast<uint8_t>(gap & 0xFF)) && poke(p.gapStart + 1u, static_cast<uint8_t>(gap >> 8)) && ok;
        for (const uint16_t at : {p.gapEnd, p.top})
            ok = poke(at, 0xFF) && poke(at + 1u, 0xFF) && ok;
        for (uint32_t k = 0; k < p.editorFlagLength; ++k)
            ok = poke(p.editorFlagAt + k, 0) && ok;
    }
    else
        for (const uint16_t at : {p.top, p.gapStart, p.gapEnd, p.lineCount, p.cursorLine})
            if (at)
                ok = poke(at, 0) && poke(at + 1u, 0) && ok;
    if (p.changedAt)
        ok = poke(p.changedAt, options.markChanged ? p.changedMask : 0) && ok;
    if (p.nameAt && !options.name.empty())
    {
        // the work name: what Assemble takes the text in memory under (and Save writes)
        std::string name = options.name.substr(0, p.nameLength);
        name.resize(p.nameLength, ' ');
        for (size_t k = 0; k < name.size(); ++k)
            ok = poke(p.nameAt + static_cast<uint32_t>(k), static_cast<uint8_t>(name[k])) && ok;
    }
    if (!ok)
    {
        plan.error = "the memory of " + d.title + "'s text is not in RAM";
        return plan;
    }
    plan.page = -1;
    plan.ok = true;
    return plan;
}
}  // namespace

namespace
{
/// ZAsm: the buffer from (startAt) (its part from #C000 in upperPage), the end pointer one past it, the table's other
/// words at the start, as LOAD leaves them. The editor has no "changed" flag: a text in memory that is not empty and
/// not the caller's own counts as changed
/// PROMETHEUS: a save's records from the access line, its six empty records, then its symbol table; the table and
/// end words follow them, the active line and the block margins go to the start (as CLEAR leaves them), and what the
/// old text held above the new end is cleared (as its deletion does)
InjectPlan PlanInjectPrometheus(const MachineView& machine, const SyncDescriptor& d, const std::vector<uint8_t>& file, const InjectOptions& options)
{
    InjectPlan plan;
    std::string why;
    if (!Idle(machine, d, why))
    {
        plan.error = why;
        return plan;
    }
    const LinearParams& p = d.linear;
    plan.previous = ReadText(machine, d);
    if (plan.previous.ok && !plan.previous.file.empty() && !options.force && !WrittenBefore(plan.previous, options.written, 0))
    {
        plan.error = "the text in " + d.title + " may have changes not saved (it keeps no flag; force overwrites it)";
        return plan;
    }
    size_t length = 0;
    if (!codecs::PrometheusCodec::FindSourceLength(file, length))
    {
        plan.error = "the file is no PROMETHEUS save (records, two bytes, a symbol table)";
        return plan;
    }
    std::vector<uint8_t> records(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(length));
    if (records.empty())
        records = {0x00, 0x30};   // an empty text is one empty line
    const std::vector<uint8_t> table(file.begin() + static_cast<std::ptrdiff_t>(length + 2), file.end());
    const uint32_t start = p.fixedStart;
    const uint32_t tableAt = start + static_cast<uint32_t>(records.size()) + 12;
    const uint32_t end = tableAt + static_cast<uint32_t>(table.size());
    const auto topWord = p.limitAt ? machine.Word(p.limitAt) : std::optional<uint16_t>(0);   // 0: the end of memory
    const auto oldEndWord = machine.Word(p.prometheusEndAt);
    if (!topWord || !oldEndWord)
    {
        plan.error = "the text pointers of " + d.title + " are not in memory";
        return plan;
    }
    // plain values: gcc 14 at -O3 takes *optional for maybe-uninitialized (-Werror)
    const uint32_t top = p.limitAt ? topWord.value_or(0) : 0x10000;
    const uint32_t oldEnd = oldEndWord.value_or(0);
    if (end >= top)
    {
        plan.error = "the text takes " + std::to_string(end - start) + " bytes with its names; " + d.title + " holds " +
                     std::to_string(top > start ? top - start - 1 : 0) + " (up to its U-TOP)";
        return plan;
    }
    const auto poke = [&](uint32_t at, uint8_t value) {
        // the text's part from #C000 in its page (PROMETHEUS 128: page 6 whatever is mapped)
        const int window = at >= 0xC000 && p.upperPage >= 0 ? p.upperPage : machine.windows[(at >> 14) & 3];
        if (window < 0 || at < 0x4000 || at > 0xFFFF)
            return false;
        plan.pokes.push_back({window, static_cast<uint16_t>(at & (kPage - 1)), value});
        return true;
    };
    const auto word = [&](uint32_t at, uint32_t value) { return poke(at, static_cast<uint8_t>(value & 0xFF)) && poke(at + 1, static_cast<uint8_t>(value >> 8)); };
    bool ok = true;
    uint32_t at = start;
    for (const uint8_t b : records)
        ok = poke(at++, b) && ok;
    for (int k = 0; k < 6; ++k)
        ok = poke(at++, 0x00) && poke(at++, 0x30) && ok;
    for (const uint8_t b : table)
        ok = poke(at++, b) && ok;
    for (; at < oldEnd; ++at)
        ok = poke(at, 0x00) && ok;
    ok = word(p.prometheusTableAt, tableAt) && word(p.prometheusEndAt, end) && ok;
    for (const uint16_t w : p.resetWords)
        ok = word(w, start) && ok;
    if (!ok)
    {
        plan.error = "the memory of " + d.title + "'s text is not in RAM";
        return plan;
    }
    plan.page = -1;
    plan.ok = true;
    return plan;
}

/// PASM: the CR LF text from fixedStart and its #1A, the editor's words as ZAP and GET leave them (the cursor on the
/// first line, which its line buffer holds with the TABs spread out); the first line goes in packed as PASM packs it
/// when the cursor leaves it. The text stays below #C000 (what the reader reads). No "changed" flag: a text in memory
/// that is not empty and not the caller's own counts as changed
InjectPlan PlanInjectPasm(const MachineView& machine, const SyncDescriptor& d, const std::vector<uint8_t>& given, const InjectOptions& options)
{
    InjectPlan plan;
    std::string why;
    if (!Idle(machine, d, why))
    {
        plan.error = why;
        return plan;
    }
    const LinearParams& p = d.linear;
    plan.previous = ReadText(machine, d);
    const bool empty = plan.previous.file.empty() || plan.previous.file == std::vector<uint8_t>{'\r', '\n'};
    if (plan.previous.ok && !empty && !options.force && !WrittenBefore(plan.previous, options.written, 0))
    {
        plan.error = "the text in " + d.title + " may have changes not saved (it keeps no flag; force overwrites it)";
        return plan;
    }
    // every line ends with CR LF (a text with LF ends too; an empty text is one empty line), up to a #1A
    std::vector<uint8_t> file;
    for (auto at = given.begin(); at != given.end() && *at != 0x1A; ++at)
    {
        if (*at == '\n' && (file.empty() || file.back() != '\r'))
            file.push_back('\r');
        file.push_back(*at);
    }
    if (file.size() < 2 || file[file.size() - 2] != '\r' || file.back() != '\n')
        file.insert(file.end(), {'\r', '\n'});
    size_t firstEnd = 0;
    while (!(file[firstEnd] == '\r' && file[firstEnd + 1] == '\n'))
        ++firstEnd;
    // the first line in the line buffer: TABs to the next multiple of 8, blanks to 64 columns
    std::vector<uint8_t> buffer;
    for (size_t k = 0; k < firstEnd; ++k)
    {
        if (file[k] == '\t')
            buffer.resize((buffer.size() / 8 + 1) * 8, ' ');
        else
            buffer.push_back(file[k]);
    }
    if (buffer.size() > 64)
    {
        plan.error = "the first line takes " + std::to_string(buffer.size()) + " columns; " + d.title + "'s line holds 64";
        return plan;
    }
    std::vector<uint8_t> packed = buffer;
    PackPasmLine(packed);
    buffer.resize(64, ' ');
    std::vector<uint8_t> text = packed;
    text.insert(text.end(), file.begin() + static_cast<std::ptrdiff_t>(firstEnd), file.end());
    const uint32_t start = p.fixedStart;
    if (start + text.size() + 1 > 0xC000)
    {
        plan.error = "the text takes " + std::to_string(text.size()) + " bytes; " + d.title + " holds " + std::to_string(0xC000 - start - 1) + " (up to #C000)";
        return plan;
    }
    const auto poke = [&](uint32_t at, uint8_t value) {
        // the text's part from #C000 in its page (PROMETHEUS 128: page 6 whatever is mapped)
        const int window = at >= 0xC000 && p.upperPage >= 0 ? p.upperPage : machine.windows[(at >> 14) & 3];
        if (window < 0 || at < 0x4000 || at > 0xFFFF)
            return false;
        plan.pokes.push_back({window, static_cast<uint16_t>(at & (kPage - 1)), value});
        return true;
    };
    const auto word = [&](uint32_t at, uint32_t value) { return poke(at, static_cast<uint8_t>(value & 0xFF)) && poke(at + 1, static_cast<uint8_t>(value >> 8)); };
    bool ok = true;
    uint32_t at = start;
    for (const uint8_t b : text)
        ok = poke(at++, b) && ok;
    ok = poke(at, 0x1A) && ok;
    const auto lines = static_cast<uint32_t>(std::count(text.begin(), text.end(), uint8_t('\n')));
    ok = word(p.pasmLengthAt, static_cast<uint32_t>(text.size())) && word(p.pasmLastLineAt, lines - 1) &&
         word(p.pasmLineLengthAt, static_cast<uint32_t>(packed.size())) && word(p.pasmLineAt, start) && word(p.pasmTopAt, start) && ok;
    for (size_t k = 0; k < buffer.size(); ++k)
        ok = poke(p.pasmLineBuffer + static_cast<uint32_t>(k), buffer[k]) && ok;
    for (const uint16_t w : p.zeroWords)
        ok = word(w, 0) && ok;
    for (const uint16_t b : p.zeroBytes)
        ok = poke(b, 0) && ok;
    ok = poke(p.pasmRedrawAt, 0xFF) && ok;
    if (p.pasmNameAt && !options.name.empty())
    {
        std::string name = options.name.substr(0, 8);
        name.resize(8, ' ');
        for (size_t k = 0; k < name.size(); ++k)
            ok = poke(p.pasmNameAt + static_cast<uint32_t>(k), static_cast<uint8_t>(name[k])) && ok;
    }
    if (!ok)
    {
        plan.error = "the memory of " + d.title + "'s text is not in RAM";
        return plan;
    }
    plan.page = -1;
    plan.ok = true;
    return plan;
}

InjectPlan PlanInjectLinear(const MachineView& machine, const SyncDescriptor& d, const std::vector<uint8_t>& given, const InjectOptions& options)
{
    if (d.linear.prometheusTableAt)
        return PlanInjectPrometheus(machine, d, given, options);
    if (d.linear.pasmLengthAt)
        return PlanInjectPasm(machine, d, given, options);
    // Laser Genius: memory holds the paragraphs and FF FF, without the disk file's #AF header
    std::vector<uint8_t> file = given;
    if (d.linear.laserGeniusFile)
    {
        if (file.size() >= 9 && file[0] == 0xAF && (file[1] | file[2] << 8) == static_cast<int>(file.size() - 9))
            file.erase(file.begin(), file.begin() + 9);
        if (file.size() < 2 || file[file.size() - 1] != 0xFF || file[file.size() - 2] != 0xFF)
            file.insert(file.end(), {0xFF, 0xFF});
    }
    InjectPlan plan;
    std::string why;
    if (!Idle(machine, d, why) && !(options.force && d.idleChain.empty()))
    {
        plan.error = why;
        return plan;
    }
    const LinearParams& p = d.linear;
    plan.previous = ReadText(machine, d);
    if (plan.previous.ok && !plan.previous.file.empty() && !options.force && !WrittenBefore(plan.previous, options.written, 0))
    {
        plan.error = "the text in " + d.title + " may have changes not saved (it keeps no flag; force overwrites it)";
        return plan;
    }
    const auto start = p.startAt ? machine.Word(p.startAt) : std::optional<uint16_t>(p.fixedStart);
    if (!start)
    {
        plan.error = "the text pointers of " + d.title + " are not in memory";
        return plan;
    }
    // a file that ends with the end marker (ZEUS' FF FF): the end word points at the marker's last byte
    const bool marked = !p.end.empty() && file.size() >= p.end.size() &&
                        std::equal(p.end.begin(), p.end.end(), file.end() - static_cast<std::ptrdiff_t>(p.end.size()),
                                   [](char a, uint8_t b) { return static_cast<uint8_t>(a) == b; });
    const uint32_t end = *start + static_cast<uint32_t>(file.size()) - (marked ? static_cast<uint32_t>(p.end.size()) : 0);
    const auto limitWord = p.limitAt ? machine.Word(p.limitAt) : std::optional<uint16_t>();
    if (p.limitAt && !limitWord)
    {
        plan.error = "the text pointers of " + d.title + " are not in memory";
        return plan;
    }
    // a plain value: gcc 14 at -O3 takes *optional here for maybe-uninitialized (-Werror)
    const bool limited = limitWord.has_value();
    const uint32_t limit = limitWord.value_or(0);
    // the block after the text (Laser Genius' screen lines) moves behind the new one
    std::vector<uint8_t> follow;
    uint32_t oldEnd = 0;
    if (p.followLastAt)
    {
        const auto was = machine.Word(p.endAt);
        const auto last = machine.Word(p.followLastAt);
        if (!was || !last || *last < *was || *last - *was > 0x400 || !machine.Read(*was, static_cast<size_t>(*last - *was + 1), follow))
        {
            plan.error = "the editor's lines after the text of " + d.title + " are not where its pointers say";
            return plan;
        }
        oldEnd = *was;
    }
    if (limited && end + p.end.size() + follow.size() > limit)
    {
        plan.error = "the text takes " + std::to_string(file.size()) + " bytes; " + d.title + " holds " +
                     std::to_string(limit > *start ? limit - *start : 0) + " (its text space)";
        return plan;
    }
    if (end + p.end.size() > 0x10000)
    {
        plan.error = "the text takes " + std::to_string(file.size()) + " bytes; " + d.title + " holds " + std::to_string(0x10000 - *start - p.end.size());
        return plan;
    }
    const auto poke = [&](uint32_t at, uint8_t value) {
        const int window = at >= 0xC000 && p.upperPage >= 0 ? p.upperPage : machine.windows[(at >> 14) & 3];
        if (window < 0 || at < 0x4000)
            return false;
        plan.pokes.push_back({window, static_cast<uint16_t>(at & (kPage - 1)), value});
        return true;
    };
    bool ok = true;
    for (size_t k = 0; k < file.size(); ++k)
        ok = poke(*start + static_cast<uint32_t>(k), file[k]) && ok;
    if (!marked)
        for (size_t k = 0; k < p.end.size(); ++k)
            ok = poke(end + static_cast<uint32_t>(k), static_cast<uint8_t>(p.end[k])) && ok;
    ok = poke(p.endAt, static_cast<uint8_t>(end & 0xFF)) && poke(p.endAt + 1u, static_cast<uint8_t>(end >> 8)) && ok;
    if (p.followLastAt)
    {
        for (size_t k = 0; k < follow.size(); ++k)
            ok = poke(end + static_cast<uint32_t>(k), follow[k]) && ok;
        for (const uint16_t at : p.followPointers)
            if (const auto v = machine.Word(at); v && *v)
            {
                const auto moved = static_cast<uint16_t>(*v - oldEnd + end);
                ok = poke(at, static_cast<uint8_t>(moved & 0xFF)) && poke(at + 1u, static_cast<uint8_t>(moved >> 8)) && ok;
            }
    }
    for (uint32_t at = p.resetFrom; at + 1 < p.resetTo; at += 2)
        ok = poke(at, static_cast<uint8_t>(*start & 0xFF)) && poke(at + 1, static_cast<uint8_t>(*start >> 8)) && ok;
    for (const uint16_t at : p.resetAlso)
        ok = poke(at, static_cast<uint8_t>(*start & 0xFF)) && poke(at + 1u, static_cast<uint8_t>(*start >> 8)) && ok;
    for (const uint16_t at : p.endAlso)
        ok = poke(at, static_cast<uint8_t>(end & 0xFF)) && poke(at + 1u, static_cast<uint8_t>(end >> 8)) && ok;
    for (const uint16_t at : p.zeroWords)
        ok = poke(at, 0) && poke(at + 1u, 0) && ok;
    for (const uint16_t at : p.zeroBytes)
        ok = poke(at, 0) && ok;
    for (uint32_t at = p.blankFrom; at < p.blankTo; ++at)
        ok = poke(at, ' ') && ok;
    if (p.nameAt && !options.name.empty())
    {
        std::string name = options.name.substr(0, p.nameLength);
        name.resize(p.nameLength, ' ');
        for (size_t k = 0; k < name.size(); ++k)
        {
            const uint32_t at = p.nameAt + static_cast<uint32_t>(k);
            const int window = p.namePage >= 0 ? p.namePage : machine.windows[(at >> 14) & 3];
            if (window < 0 || at < 0x4000 || at > 0xFFFF)
                ok = false;
            else
                plan.pokes.push_back({window, static_cast<uint16_t>(at & (kPage - 1)), static_cast<uint8_t>(name[k])});
        }
    }
    if (!ok)
    {
        plan.error = "the memory of " + d.title + "'s text is not in RAM";
        return plan;
    }
    plan.page = -1;
    plan.ok = true;
    return plan;
}
}  // namespace

InjectPlan PlanInject(const MachineView& machine, const SyncDescriptor& d, const std::vector<uint8_t>& file, const InjectOptions& options)
{
    InjectPlan plan;
    if (d.family == LayoutFamily::GapBuffer && (d.gapBuffer.loadedForm || d.gapBuffer.commandLineForm))
        return PlanInjectLoaded(machine, d, file, options);
    if (d.family == LayoutFamily::Linear && d.linear.injectable)
        return PlanInjectLinear(machine, d, file, options);
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
    // The editor's variables are found in the code the CPU sees below #C000
    std::vector<uint8_t> code;
    if ((!p.loadVariables.empty() || !p.changedVariables.empty()) && !machine.Read(0x4000, 0x8000, code))
    {
        plan.error = "the code of " + d.title + " (#4000-#BFFF) is not in memory";
        return plan;
    }
    bool changed = plan.previous.changed;
    for (const LoadVariable& v : p.changedVariables)
    {
        // a flag of the editor's own (STORM: a line written since LOAD / SAVE, the cursor line typed in)
        size_t places = 0;
        const auto address = VariableAddress(code, v, places);
        if (!address)
        {
            plan.error = d.title + "'s editor variables were not found in its code (" + std::to_string(places) + " places)";
            return plan;
        }
        const uint32_t bits = machine.Byte(*address).value_or(0) & v.value;
        changed = changed || (v.from == LoadVariable::From::SetBits ? bits != 0 : bits != v.value);
    }
    if (plan.previous.ok && changed && !options.force && !WrittenBefore(plan.previous, options.written, p.headerSize))
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
    const size_t tail = p.lengthFromPointer ? 1 : 0;   // the end sentinel after the file
    if (file.size() < p.headerSize || p.base + file.size() + tail > kPage)
    {
        plan.error = "the text takes " + std::to_string(file.size()) + " bytes; the page holds " + std::to_string(kPage - p.base - tail);
        return plan;
    }
    plan.page = page;
    plan.bytes.assign(target->bytes.begin(), target->bytes.begin() + kPage);
    std::fill(plan.bytes.begin() + p.base, plan.bytes.end(), uint8_t{0});
    std::copy(file.begin(), file.end(), plan.bytes.begin() + p.base);
    if (p.lengthFromPointer)
    {
        // STORM: the sentinel before the text, the end byte after it, the pointer one past that
        std::copy(p.signature.begin(), p.signature.end(), plan.bytes.begin() + p.signatureAt);
        plan.bytes[p.base + file.size()] = p.endSentinel;
        const uint16_t end = static_cast<uint16_t>(0xC000 + p.base + file.size() + 1);
        plan.bytes[p.pointerAt] = static_cast<uint8_t>(end & 0xFF);
        plan.bytes[p.pointerAt + 1] = static_cast<uint8_t>(end >> 8);
    }
    if (options.markChanged && p.changedField)
        plan.bytes[p.base + p.changedField] |= p.changedMask;
    // The editor's variables LOAD sets: each found once in the code
    for (const LoadVariable& v : p.loadVariables)
    {
        size_t places = 0;
        const auto found = VariableAddress(code, v, places);
        if (!found)
        {
            plan.error = d.title + "'s editor variables were not found in its code (" + std::to_string(places) + " places)";
            return plan;
        }
        const uint16_t address = *found;
        std::vector<uint8_t> bytes(v.size, 0);
        uint32_t value = 0;
        switch (v.from)
        {
            case LoadVariable::From::TextEnd:
            {
                const auto end = std::find(file.begin() + p.textOffset, file.end(), p.endByte);
                value = 0xC000u + p.base + static_cast<uint32_t>(end - file.begin());
                break;
            }
            case LoadVariable::From::Header:
                for (uint8_t k = 0; k < v.size && v.field + k < file.size(); ++k)
                    value |= static_cast<uint32_t>(file[v.field + k]) << (8 * k);
                break;
            case LoadVariable::From::Constant: value = v.value; break;
            case LoadVariable::From::SetBits: value = machine.Byte(address).value_or(0) | v.value; break;
            case LoadVariable::From::PageBytes: break;
            case LoadVariable::From::SecondLine:
            {
                // walked back from the end: the line whose body starts the file ends at its length byte
                long at = static_cast<long>(file.size()) - 1;
                while (at >= 0 && at - (file[static_cast<size_t>(at)] & 0x3F) > 0)
                    at -= (file[static_cast<size_t>(at)] & 0x3F) + 1;
                value = 0xC000u + p.base + static_cast<uint32_t>(at + 1);
                break;
            }
        }
        for (uint8_t k = 0; k < v.size; ++k)
            bytes[k] = v.from == LoadVariable::From::PageBytes ? plan.bytes[(v.field + k) % kPage] : static_cast<uint8_t>(value >> (8 * (k % 4)));
        for (uint8_t k = 0; k < v.size; ++k)
        {
            const uint16_t at = static_cast<uint16_t>(address + k);
            const int window = machine.windows[at >> 14];
            if (window < 0 || at < 0x4000 || at >= 0xC000)
            {
                plan.error = d.title + "'s editor variable #" + Hex(address, 4) + " is not in RAM below #C000";
                return plan;
            }
            plan.pokes.push_back({window, static_cast<uint16_t>(at & (kPage - 1)), bytes[k]});
        }
    }
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
        InjectOptions named = options;
        if (named.name.empty())
            named.name = title;
        result.pages.push_back(PlanInject(machine, d, written.bytes, named));
        result.ok = result.pages[0].ok;
        result.error = result.pages[0].error;
        return result;
    }
    const size_t others = files.size() - 1;
    if (d.projectOnDisk && d.projectPages.empty())
    {
        // one text in memory, the others onto the disk it reads them from, each INCLUDE naming its file there
        const std::vector<std::string> names = ProjectTextNames(files, title);
        std::map<std::string, std::string> includeAs;
        for (size_t k = 1; k < files.size(); ++k)
            includeAs[files[k].name] = "\"" + names[k] + "\"";
        WriteProjectResult written = WriteProjectAs(files, d.codec, d.version, names, includeAs);
        result.diagnostics = std::move(written.diagnostics);
        if (!written.ok)
        {
            result.error = written.error;
            return result;
        }
        result.texts = std::move(written.files);
        InjectOptions named = options;
        if (named.name.empty())
            named.name = title;
        result.pages.push_back(PlanInject(machine, d, result.texts[0].bytes, named));
        result.ok = result.pages[0].ok;
        result.error = result.pages[0].error;
        const char type = d.extension.empty() ? 'C' : d.extension[0];
        for (size_t k = 1; result.ok && k < result.texts.size(); ++k)
            result.diskFiles.push_back(CatalogFile(names[k], type, d.codec, d.version, result.texts[k].bytes));
        return result;
    }
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
            if (previous.changed && !options.force && !WrittenBefore(previous, options.written, p.headerSize))
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
        if (page.page >= 0)   // a whole page (ALASM); a linear text comes as pokes (TASM, MASM, GENS ...)
            changed[page.page] = page.bytes;
    for (const InjectPlan& page : plan.pages)
        for (const InjectPlan::Poke& poke : page.pokes)
        {
            auto it = changed.find(poke.page);
            if (it == changed.end())
                it = changed.emplace(poke.page, snapshot.pages.at(poke.page)).first;
            it->second[poke.offset] = poke.value;
        }
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
        Alasm("4.5", "4.5", 0x9E06, "", true, {0x998F, 0x8B6B}, {0, 3}),
        Alasm("4.46", "4.44", 0x9E6E, "", true, {0x9A07, 0x8B92}, {4}),
        Alasm("4.45", "4.44", 0x9E53, "", true, {0x99E1, 0x8B93}, {4}),
        Alasm("4.44", "4.44", 0x9E7E, "", true, {0x9A08, 0x8BA8}, {4}),
        Alasm("4.43", "4.5", 0x9E1B, "", false, {0x99A4, 0x8B99}, {4}),
        Alasm("4.42", "4.42", 0x9E2F, "", true, {0x99B2, 0x8B8F}, {4}),
        Alasm("4.2", "4.2", 0x9D3B, "4.2\r Written by", true, {0x983F, 0x8B84}, {4, 0}),
        Alasm("3.8c", "3.8", 0x9951, "3.8c\r Written by", true, {0x943A, 0x8149}, {4}),
        Xas("4.18", "4.18", 0xB51A, "XAS by Max Petrov (HPM) 3.091", {0x979F, 0x8D85}),
        Xas("5.05", "5.05", 0xB601, "XAS by Max Petrov (HPM) 5.05 ", {0x97A8, 0x8D9E}),   // 5.05 and 5.05SE alike
        Xas("7.43c", "7.43c", 0xB961, "by Max Petrov & Creator v7.43", {0x97D8, 0x8E3B}),
        Xas("7.447", "7.43", 0x9B00, "by Max Petrov & Creator v7.44", {0x9DDC, 0x8E5B}),
        Xas("9.07m", "9.07m", 0xB8EA, "XAS 9.07 ReCompiled by Mythos", {0x95B5, 0x8D7B}),
        Xas("9.10", "9.10", 0xB912, "XAS by Max Petrov,64sm by STS", {0x9552, 0x8D98}),
        Storm("storm-1.3", "STORM 1.3", "1.3", 0x0B, '\x87'),
        Storm("storm-1.0b", "STORM 1.0beta", "1.0", 0x03, '\x85'),
        Zasm315(),
        ZasmRubtsoff("zasm-3.2x", "ZAsm 3.2x", "3.15", 0x8048,
                     "\xCA\xFC\x7F\x3E\x20\x18\xC4\xF1\x3E\x00\xB7\x37\xC8\xCD\x54\x1F\xD4\x93\x79\x37\xC9\xF5\x3A\x02", 0x8538, {0xC930, 0x7B4E}, 0xF9B6),
        ZasmRubtsoff("zasm-lite-1.07", "ZAsm Lite 1.07", "lite", 0x8000,
                     "\xE5\x2A\x48\x62\xE5\xB7\xED\x42\x4D\x44\xE1\x09\x1B\x7A\xB3\x20\xFA\x22\x48\x62\xE1\xC9\xCD\xC8", 0x859D, {0xDF74, 0x6428}, 0xE692),
        ZasmRubtsoff("zasm-3.3.02", "ZAsm 3.3.02", "3.15", 0x8048,
                     "\xCA\xFC\x7F\x3E\x20\x18\xC4\xF1\x3E\x00\xB7\x37\xC8\xCD\x54\x1F\xD4\x93\x79\x37\xC9\xF5\x3A\xD3", 0x850A, {0xCA21, 0x7B4E}, 0xF9B4),
        ZasmRubtsoff("zasm-3.3.51", "ZAsm 3.3.51", "3.15", 0x8048,
                     "\xCA\xFC\x7F\x3E\x20\x18\xC4\xF1\x3E\x00\xB7\x37\xC8\xCD\x54\x1F\xD4\x93\x79\x37\xC9\xF5\x3A\xC6", 0x84FC, {0xCBC9, 0x7B4E}, 0xF889),
        ZasmRubtsoff("zasm-3.3.final", "ZAsm 3.3.Final", "3.15", 0x8000,
                     "\xD0\xEB\x29\x44\x4D\x29\x29\x09\x06\x00\xD6\x30\x4F\x09\xEB\x23\x18\xEA\x7E\x23\xFE\x0A\x28\xFA", 0x8401, {0xD0BC, 0x7B64}, 0xF8A7),
        ZasmRubtsoff("zasm-3.80.4", "ZAsm 3.80.4", "3.15", 0x8000,
                     "\x41\x20\xE6\xE5\x3C\x2A\xB7\x66\xCD\xC1\x63\x3A\xB6\x66\xB7\x28\x05\xED\x4B\x83\x80\x09\x73\x23", 0x6D02, {0xC685, 0x6425}, 0x6D31),
        ZasmRubtsoff("zasm-3.4", "ZAsm 3.4 (ZXTA34X, Z34_04)", "3.15", 0x8130,
                     "\xC5\xD5\x7E\x23\xFE\xFF\x28\x08\xA7\x28\x05\xCD\x44\x81\x18\xF2\xD1\xC1\xF1\xC9\xFE\x18\xD2\x02", 0x8520, {0xCF6B, 0x7B4E}, 0xF94C),
        ZasmRubtsoff("zasm-x64.1", "ZAsm x64.1", "3.15", 0x8000,
                     "\x28\x15\x30\x04\xFE\x02\x30\x2D\xFE\x0D\xC8\x12\x13\x10\xE5\x3E\x0D\x01\x00\x00\xED\xB1\xC9\x7E", 0x852D, {0xC94A, 0x7B46}, 0xF837),
        ZasmRubtsoff("zasm-4.0x8", "ZAsm 4.0x8", "3.15", 0x8034,
                     "\x76\x6C\x70\x0C\x66\x30\xCC\xF8\x78\xDC\x76\xDC\x7C\x7C\xCC\xCC\xC6\xC6\xCC\xFC\x30\x18\x30\x00", 0x692B, {0xD31F, 0x6212}, 0xF9BE),
        ZasmRubtsoff("zasm-4.x64", "ZAsm 4.x64", "3.15", 0x8000,
                     "\x28\x0E\xCD\xC8\x6B\xCD\x3B\x6E\xDD\xCB\x1A\xD6\xD7\x96\x18\x03\xCD\x6B\x67\xCD\xAB\x70\xCD\x8B", 0x6929, {0xD919, 0x6212}, 0xFAD9),
        ZasmRubtsoff("zasm-4.20", "ZAsm 4.20", "3.15", 0x8000,
                     "\xD4\xD5\xD6\xD7\xD8\xD9\xDA\xDB\xDC\xDD\xDE\xDF\x72\x73\x74\x75\x66\x68\x63\x7E\x7B\x7D\x5C\x79", 0x68FB, {0xC18A, 0xE431, 0x6212}, 0xDE36),
        Zxasm310(),
        Zxasm30(),
        LaserGenius(),
        Prometheus48(),
        Prometheus48("prometheus-48-trdos", "PROMETHEUS 48K TR-DOS (PROMBD.C, at 25000)", 0x514),
        Prometheus128("prometheus-128", "PROMETHEUS 128 L (Proxima 1993, at 24000)", 0, 0,
                      "\xC9\xF5\xD6\x34\x5F\x16\x37\xCD\x90\x8C\xC5\x21\x03\x5E\xAF\x4F\x23\x0C\xBE\x20\xFB\x11\xF0\x5D"),
        Prometheus128("prometheus-128m", "PROMETHEUS 128 M (Proxima 1993, at 24000)", 0x237, 0x23B,
                      "\xC9\xF5\xD6\x34\x5F\x16\x37\xCD\x59\x8A\xC5\x21\x03\x5E\xAF\x4F\x23\x0C\xBE\x20\xFB\x11\xF0\x5D"),
        Prometheus128("prometheus-128s", "PROMETHEUS 128 S (Proxima 1993, at 24000)", 0x5CC, 0x5D0,
                      "\xC9\xF5\xD6\x34\x5F\x16\x37\xCD\xC4\x86\xC5\x21\x03\x5E\xAF\x4F\x23\x0C\xBE\x20\xFB\x11\xF0\x5D"),
        Pasm30(),
        Zeus1983(),
        Zeus31(),
        Zeus11(),
        ZeusGg(),
        ZeusPort("zeus-2.2", "ZEUS 2.2 (Bayanov 1996)",
                 "\x21\x6A\x5C\xA6\x77\xCD\xE3\xE4\x0F\x5A\x45\x55\x53\x20\x76\x32\x2E\x32\x0D\x7F\x31\x39\x38\x33"),
        ZeusPort("zeus-2.2si", "ZEUS 2.2SI (SI Soft 1991)",
                 "\x21\x6A\x5C\xA6\x77\xCD\xE3\xE4\x0F\x5A\x45\x55\x53\x20\x5A\x38\x30\x20\x41\x53\x53\x45\x4D\x42", {0x6EAC, 0xE0D5}),
        ZeusPort("zeus-pro", "Zeus Pro 2.18 (RIP Group)",
                 "\x21\x6A\x5C\xA6\x77\xCD\xE3\xE4\x0F\x20\x20\x20\x20\x20\x20\x20\x20\x20\x5A\x45\x55\x53\x20\x50"),
        ZeusPort("zeus-plus-2.16", "Zeus Plus 2.16 (Stepanenko 1991)",
                 "\x21\x6A\x5C\xA6\x77\xC3\xC2\xDC\x83\xD6\xC9\xD9\xDF\xAC\xF3\xAC\xCF\xDE\xD5\xDF\xD8\xCD\xC0\xAC"),
        Zeus7e(),
        Primus29(),
        Gens4Tape(),
        GensInjectable(Gens("gens-3m21", "GENS3M21 (HiSoft Devpac 3, tape, at 49000)", {0xE001, "HISOFT GENS3M2 ASSEMBLER"}, 0xDE54, 0xBF9E),
                       {0xC25B, 0xC2DB, 0xC1D7}),
        GensInjectable(Gens("gens-4b", "GENS4B 4.1 (MOA B-Disk, at 30000)", {0x9A70, "MOA B-Disk version."}, 0x7928, 0x7566),
                       {0x79DD, 0x7A5D, 0x7959}),
        GensInjectable(Gens("gens-4.3t", "GENS 4.3t (Aysoft, TR-DOS)", {0x8A9F, "BETA DISK VERSION 4.3t"}, 0x689C, 0x61FB),
                       {0x6952, 0x69D2, 0x68CE}),
        ZxAsm2("zasm-2.4", "ZX-ASM 2.4", 0x806C,
               "\x70\x65\x00\x70\x00\x6D\x00\x7E\xFE\x20\x20\x08\x23\x7E\xFE\x20\x28\xFA\xB7\xC9\xFE\x06\x37\xC0", 0x6273, {0x7279, 0x70EA}, 0x631B, 0x631C),
        ZxAsm2("zasm-2.5", "ZX-ASM 2.5", 0x804C,
               "\x6F\x00\x70\x65\x00\x70\x00\x6D\x00\x7E\xFE\x20\x20\x08\x23\x7E\xFE\x20\x28\xFA\xB7\xC9\xFE\x06", 0x6273, {0x7279, 0x70EA}, 0x631B, 0x631C),
        ZxAsm2("zasm-2.6", "ZX-ASM 2.6", 0x8000,
               "\xCD\x2E\x61\x38\x65\xFE\x5F\x28\x61\x23\xFE\x23\x28\x35\xFE\x25\x28\x1B\xFE\x22\x28\x08\xFE\x24", 0x6003, {0x70A3, 0x6F24}, 0x609F, 0x60A5),
        Masm11(),
        Masm10Demo(),
        Masm2("masm-2.0", "MASM 2.0 TURBO", "2.0", {0x80E0, std::string("MASM2.0>\0", 9)}, 0x94DA, 0x92C6, 0x89D1, 0x929F, {0x80EC, 0x807B}, 0x8AD2),
        Masm2("masm-3.0", "MASM 3.0 MACRO", "3.0", {0x80D3, std::string("MASM128>\0", 9)}, 0x9123, 0x8C24, 0x864F, 0x8BFD, {0x80DF, 0x8091}, 0),
        TasmInjectable(TasmGap("4.0", "TASM 4.0 (XL Design)", "4.0", {0x9859, "TASM4.0>"}, 0x8DD0, 0x91AC), {0x9865, 0x807C}, 0x975E, 0x9793),
        TasmInjectable(TasmGap("4.4", "TASM 4.4 (KVA)", "4.0", {0x9859, "TASM4.4>"}, 0x8DD0, 0x91AC), {0x9865, 0x807C}, 0x975E, 0x9793),
        TasmInjectable(TasmGap("3.0", "TASM 3.0 (Rst7)", "3", {0x9721, "TASM2.0 source file: "}, 0x8910, 0x8C82), {0x93D5, 0x807C}, 0x9322, 0x9346),
        TasmInjectable(TasmGap("3.2", "TASM 3.2 (Rst7)", "3", {0x9728, "TASM2.0 file: "}, 0x8910, 0x8C82), {0x93D5, 0x807C}, 0x9322, 0x9346),
        Tasm20(),
        TasmInjectable(TasmGap("3.5", "TASM 3.5 FLASHVERSION", "3", {0x9350, "TASM128 FLASHVERSION"}, 0x8910, 0x8C82), {0x93D5, 0x807C}, 0x9322, 0x9346),
        Tasm412(),
    };
    return descriptors;
}

const SyncDescriptor* FindDescriptor(const std::string& id)
{
    for (const SyncDescriptor& d : Descriptors())
        if (d.id == id)
            return &d;
    // "<id>@<address>": a relocatable one installed elsewhere
    const size_t sign = id.rfind('@');
    if (sign == std::string::npos || sign + 1 == id.size() || id.find_first_not_of("0123456789", sign + 1) != std::string::npos ||
        id.size() - sign > 6)
        return nullptr;
    const long address = std::stol(id.substr(sign + 1));
    for (const SyncDescriptor& d : Descriptors())
        if (d.installedAt && d.id.compare(0, std::string::npos, id, 0, sign) == 0 && address >= 0x4000 && address <= 0xFFFF)
            return Relocated(d, static_cast<int>(address) - d.installedAt);
    return nullptr;
}

const SyncDescriptor* Relocated(const SyncDescriptor& base, int shift)
{
    if (!base.installedAt || base.identify.empty())
        return nullptr;
    if (!shift)
        return &base;
    const auto installed = static_cast<uint16_t>(base.installedAt + shift);
    const std::string id = base.id + "@" + std::to_string(installed);
    static std::mutex mutex;
    static std::deque<SyncDescriptor> made;   // a deque: the pointers handed out stay valid
    const std::lock_guard<std::mutex> lock(mutex);
    for (const SyncDescriptor& d : made)
        if (d.id == id)
            return &d;

    SyncDescriptor d = base;
    d.id = id;
    const std::string at = "at " + std::to_string(base.installedAt);
    if (const size_t found = d.title.find(at); found != std::string::npos)
        d.title.replace(found, at.size(), "at " + std::to_string(installed));
    d.installedAt = installed;
    // the code's addresses move, the paged window's do not (PROMETHEUS 128's text in page 6 at #C01A)
    const auto move = [&](uint16_t& a) {
        if (a && (!base.linear.upperPage || a < 0xC000))
            a = static_cast<uint16_t>(a + shift);
    };
    for (Signature& s : d.identify)
        move(s.address);
    for (const uint8_t w : base.relocatedWords)
    {
        std::string& bytes = d.identify.front().bytes;
        if (w + 1u >= bytes.size())
            continue;
        uint16_t word = static_cast<uint8_t>(bytes[w]) | static_cast<uint8_t>(bytes[w + 1]) << 8;
        move(word);
        bytes[w] = static_cast<char>(word & 0xFF);
        bytes[w + 1] = static_cast<char>(word >> 8);
    }
    LinearParams& p = d.linear;
    for (uint16_t* a : {&p.fixedStart, &p.prometheusTableAt, &p.prometheusEndAt, &p.limitAt})
        move(*a);
    for (uint16_t& a : p.resetWords)
        move(a);
    for (uint16_t& a : d.idleChain)
        move(a);
    made.push_back(std::move(d));
    return &made.back();
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

namespace
{
/// Where a relocatable descriptor's code lies moved (Probe): its first signature anywhere in memory with the absolute
/// words masked, those words all moved by the same shift (0 = not found; a move by 0 identifies in place)
int FindMove(const std::vector<uint8_t>& memory, const SyncDescriptor& d)
{
    const std::string& signature = d.identify.front().bytes;
    std::vector<bool> masked(signature.size());
    for (const uint8_t w : d.relocatedWords)
        for (size_t k = w; k < std::min<size_t>(w + 2u, signature.size()); ++k)
            masked[k] = true;
    const auto word = [](auto&& bytes, size_t at) {
        return static_cast<uint16_t>(static_cast<uint8_t>(bytes[at]) | static_cast<uint8_t>(bytes[at + 1]) << 8);
    };
    for (size_t at = 0; at + signature.size() <= memory.size(); ++at)
    {
        size_t k = 0;
        while (k < signature.size() && (masked[k] || memory[at + k] == static_cast<uint8_t>(signature[k])))
            ++k;
        if (k < signature.size())
            continue;
        const int shift = static_cast<int>(0x4000 + at) - d.identify.front().address;
        if (shift && std::all_of(d.relocatedWords.begin(), d.relocatedWords.end(), [&](uint8_t w) {
                return word(memory, at + w) == static_cast<uint16_t>(word(signature, w) + shift);
            }))
            return shift;
    }
    return 0;
}
}  // namespace

std::vector<ProbeCandidate> Probe(const MachineView& machine)
{
    std::vector<ProbeCandidate> candidates;
    const auto add = [&](const SyncDescriptor& d, const char* how) {
        const SyncText text = ReadText(machine, d);
        candidates.push_back({&d, text.ok ? 100 : 60,
                              text.ok ? std::string(how) + ", the text reads"
                                      : std::string(how) + "; " + (text.diagnostics.empty() ? text.state : text.diagnostics.back().message)});
    };
    for (const SyncDescriptor& d : Descriptors())
        if (!d.identify.empty() &&
            std::all_of(d.identify.begin(), d.identify.end(), [&](const Signature& s) { return Matches(machine, s); }))
            add(d, "identified");
    // nothing in place: a relocatable code installed elsewhere
    std::vector<uint8_t> memory;
    if (candidates.empty() && machine.Read(0x4000, 0xC000, memory))
        for (const SyncDescriptor& d : Descriptors())
            if (d.installedAt && !d.identify.empty())
                if (const int shift = FindMove(memory, d))
                    if (const SyncDescriptor* moved = Relocated(d, shift);
                        moved && std::all_of(moved->identify.begin(), moved->identify.end(), [&](const Signature& s) { return Matches(machine, s); }))
                        add(*moved, ("identified at " + std::to_string(moved->installedAt)).c_str());
    std::stable_sort(candidates.begin(), candidates.end(), [](const ProbeCandidate& a, const ProbeCandidate& b) { return a.score > b.score; });
    return candidates;
}
}  // namespace unrealasm::sync
