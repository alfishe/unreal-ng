#include "unrealasm/sync/reader.h"

#include <algorithm>

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

SyncText ReadFileImage(const MachineView& machine, const SyncDescriptor& d)
{
    SyncText text;
    const FileImageParams& p = d.fileImage;
    const std::optional<uint8_t> id = machine.Byte(p.pageIdAt);
    if (!id)
        return Inconsistent(text, "the page id at #" + Hex(p.pageIdAt, 4) + " is not in the memory copied");
    for (const int candidate : PagesForId(machine, d.pages, *id))
    {
        const symbols::MemoryPage* page = machine.Page(candidate);
        if (!page || page->bytes.size() < kPage)
            continue;
        const auto bytes = page->bytes.subspan(p.base);
        if (bytes.size() < p.headerSize ||
            !std::equal(p.signature.begin(), p.signature.end(), bytes.begin() + p.signatureAt,
                        [](char a, uint8_t b) { return static_cast<uint8_t>(a) == b; }))
            continue;
        const size_t length = p.headerSize + (bytes[p.lengthField] | (bytes[p.lengthField + 1u] << 8));
        if (length > bytes.size())
            return Inconsistent(text, "the text in page " + std::to_string(candidate) + " is longer than the page");
        text.page = candidate;
        text.file.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
        if (p.changedField)
        {
            text.changed = text.file[p.changedField] != 0;
            text.file[p.changedField] = 0;   // SAVE writes the file as saved
        }
        size_t nameLength = 8;
        while (nameLength > 0 && text.file[nameLength - 1] == ' ')
            --nameLength;
        text.name.assign(text.file.begin(), text.file.begin() + static_cast<std::ptrdiff_t>(nameLength));
        text.ok = true;
        text.state = "ok";
        return text;
    }
    return Inconsistent(text, "no text header in the page the id #" + Hex(*id, 2) + " names");
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
    const std::string& end = d.gapBuffer.end;
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

SyncText ReadGapBuffer(const MachineView& machine, const SyncDescriptor& d)
{
    SyncText text;
    const GapBufferParams& p = d.gapBuffer;
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

SyncText ReadLinear(const MachineView& machine, const SyncDescriptor& d)
{
    SyncText text;
    const LinearParams& p = d.linear;
    const std::optional<uint16_t> start = p.startAt ? machine.Word(p.startAt) : std::optional<uint16_t>(p.fixedStart);
    const std::optional<uint16_t> end = machine.Word(p.endAt);
    if (!start || !end)
        return Inconsistent(text, "the text pointers are not in the memory copied");
    if (*end < *start)
        return Inconsistent(text, "the text ends (#" + Hex(*end, 4) + ") before it starts (#" + Hex(*start, 4) + ")");
    if (!machine.Read(*start, static_cast<size_t>(*end - *start), text.file))
        return Inconsistent(text, "the text lies in a page that was not copied");
    text.file.insert(text.file.end(), p.end.begin(), p.end.end());
    text.ok = true;
    text.state = "ok";
    return text;
}

// Descriptors -------------------------------------------------------------------------------------------------------

/// The title is looked for where the code keeps it; #BE00 also holds it, but the editor uses that place as a buffer
SyncDescriptor Alasm(const std::string& version, const std::string& codecVersion, uint16_t titleAt)
{
    SyncDescriptor d;
    d.id = "alasm-" + version;
    d.title = "ALASM " + version;
    d.codec = "alasm";
    d.version = codecVersion;
    d.extension = "H";
    d.identify = {{titleAt, "ALASM v" + version}};
    d.pages = PageIdRule::Port7FFD;
    d.family = LayoutFamily::FileImage;
    // IX = #80BF: IX+#0D the page of the current text; the text from #C000 of it is the file (asm-synchronizer.md §7.1)
    d.fileImage.pageIdAt = 0x80CC;
    d.fileImage.base = 0;
    d.fileImage.headerSize = 0x40;
    d.fileImage.lengthField = 0x21;
    d.fileImage.signatureAt = 0x28;
    d.fileImage.signature = std::string("\xF3\x76\xC7\xDD\xFD\xED\xB0\xD9", 8);
    d.fileImage.changedField = 0x27;
    d.typing = {TypingRule::NotInText, 0x80CB, 0x01};   // IX+#0C bit 0: the current line is modified
    d.labelScanner = "alasm-table";
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

const std::vector<SyncDescriptor>& Descriptors()
{
    static const std::vector<SyncDescriptor> descriptors = {
        Alasm("5.09", "5.07", 0x97C5),   // verified with dumps (testdata/sync); 5.0-5.08, 4.43-4.5 need theirs
        Alasm("4.44", "4.44", 0x9E7E),
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
