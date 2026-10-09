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
    std::optional<uint8_t> id;
    if (d.pages == PageIdRule::Port7FFD)
    {
        id = machine.Byte(p.pageIdAt);
        if (!id)
            return Inconsistent(text, "the page id at #" + Hex(p.pageIdAt, 4) + " is not in the memory copied");
    }
    for (const int candidate : PagesForId(machine, d.pages, id.value_or(0)))
    {
        const symbols::MemoryPage* page = machine.Page(candidate);
        if (!page || page->bytes.size() < kPage)
            continue;
        const auto bytes = page->bytes;
        if (bytes.size() < size_t(p.base) + p.headerSize || bytes.size() < size_t(p.signatureAt) + p.signature.size() ||
            !std::equal(p.signature.begin(), p.signature.end(), bytes.begin() + p.signatureAt,
                        [](char a, uint8_t b) { return static_cast<uint8_t>(a) == b; }))
            continue;
        size_t length = 0;
        if (p.lengthFromEnd)
        {
            const auto end = std::find(bytes.begin() + p.base + p.textOffset, bytes.end(), p.endByte);
            if (end == bytes.end())
                return Inconsistent(text, "no end of text in page " + std::to_string(candidate));
            length = static_cast<size_t>(end - bytes.begin()) - p.base + 1;
            if (p.wholeSectors)
                length = std::min<size_t>((length + 255) / 256 * 256, bytes.size() - p.base);
        }
        else if (p.lengthFromPointer)
        {
            const uint32_t end = bytes[p.pointerAt] | (bytes[p.pointerAt + 1u] << 8);
            const uint32_t first = kPage * 3 + p.base;   // the text's CPU address at #C000
            if (end <= first || end > kPage * 4 || bytes[end - kPage * 3 - 1] != p.endSentinel)
                return Inconsistent(text, "the end pointer #" + Hex(end, 4) + " does not end the text in page " + std::to_string(candidate));
            length = end - first - 1;
        }
        else
            length = p.headerSize + (bytes[p.base + p.lengthField] | (bytes[p.base + p.lengthField + 1u] << 8));
        if (size_t(p.base) + length > bytes.size())
            return Inconsistent(text, "the text in page " + std::to_string(candidate) + " is longer than the page");
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
        return text;
    }
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

/// MASM's gap buffer: [fixed start, gap start) + [gap end, #10000), and in the editor the cursor line's record that
/// ends at the gap end
SyncText ReadGapBufferFixed(const MachineView& machine, const SyncDescriptor& d)
{
    SyncText text;
    const GapBufferParams& p = d.gapBuffer;
    const auto gapStart = machine.Word(p.gapStart), gapEnd = machine.Word(p.gapEnd);
    std::vector<uint8_t> flag;
    if (!gapStart || !gapEnd || !machine.Read(p.editorFlagAt, p.editorFlagLength, flag))
        return Inconsistent(text, "the text pointers are not in the memory copied");
    if (!(p.fixedStart <= *gapStart && *gapStart <= *gapEnd))
        return Inconsistent(text, "the text pointers are out of order: #" + Hex(*gapStart, 4) + " #" + Hex(*gapEnd, 4));
    text.editor = std::any_of(flag.begin(), flag.end(), [](uint8_t b) { return b != 0; });
    uint32_t upperFrom = *gapEnd;
    if (text.editor)
    {
        // The cursor line's record [n] body [n] ends at the gap end
        const std::optional<uint8_t> n = machine.Byte(static_cast<uint16_t>(*gapEnd - 1));
        if (!n || *gapEnd < uint32_t(*n) + 2 || uint32_t(*gapEnd) - *n - 2 < *gapStart ||
            machine.Byte(static_cast<uint16_t>(*gapEnd - *n - 2)) != n)
            return Inconsistent(text, "no line record before the gap end #" + Hex(*gapEnd, 4));
        upperFrom = *gapEnd - *n - 2u;
    }
    std::vector<uint8_t> lower, upper;
    if (!machine.Read(p.fixedStart, static_cast<size_t>(*gapStart - p.fixedStart), lower) ||
        !machine.Read(static_cast<uint16_t>(upperFrom), static_cast<size_t>(0x10000 - upperFrom), upper))
        return Inconsistent(text, "the text lies in a page that was not copied");
    if (upper.empty() || upper.back() != static_cast<uint8_t>(p.end.empty() ? 0xFF : p.end.back()))
        return Inconsistent(text, "the text does not end with its end marker at #FFFF");
    text.file = std::move(lower);
    text.file.insert(text.file.end(), upper.begin(), upper.end());
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

/// The title is looked for where the code keeps it (each build elsewhere); #BE00 also holds it, but the editor uses
/// that place as a buffer. `typingFlag` false: the build keeps no "line modified" bit at IX+#0C (4.43)
SyncDescriptor Alasm(const std::string& version, const std::string& codecVersion, uint16_t titleAt, std::string title = "",
                     bool typingFlag = true)
{
    SyncDescriptor d;
    d.id = "alasm-" + version;
    d.title = "ALASM " + version;
    d.codec = "alasm";
    d.version = codecVersion;
    d.extension = "H";
    d.identify = {{titleAt, title.empty() ? "ALASM v" + version : title}};
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
    d.typing = {TypingRule::NotInText, 0x80CB, static_cast<uint8_t>(typingFlag ? 0x01 : 0x00)};   // IX+#0C bit 0: the line is modified
    d.labelScanner = "alasm-table";
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

/// STORM 1.2 / 1.3 (asm-synchronizer.md §7.8): the text in RAM page 6 at #C000: the end pointer, the name, an #FF
/// sentinel, the file from #C00B up to the #FF the end pointer follows. The line under the cursor joins the text when
/// the cursor leaves it. Identified by its entry code at #8000, the same in 1.3 and 1.3i (packed on its disk)
SyncDescriptor Storm13()
{
    SyncDescriptor d;
    d.id = "storm-1.3";
    d.title = "STORM 1.3";
    d.codec = "storm";
    d.version = "1.3";
    d.extension = "C";
    d.identify = {{0x8000, std::string("\xFD\x21\x3A\x5C\xED\x56\xFB\x2A\x3D\x5C\x01\xFC\x5F\xB7\xED\x42\x28\x02\xCF\x03\x09\xF9\x76\xF3", 24)}};
    d.pages = PageIdRule::MappedAtC000;
    d.family = LayoutFamily::FileImage;
    d.fileImage.base = 0x0B;
    d.fileImage.headerSize = 0;
    d.fileImage.signatureAt = 0x0A;
    d.fileImage.signature = std::string("\xFF", 1);
    d.fileImage.nameAt = 0x02;
    d.fileImage.nameLength = 8;
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

/// MASM 1.1 (asm-synchronizer.md §7.12): the text from #970B; while it is edited a gap at (#96CC)-(#96CE), the part
/// after it up to #FFFF (the #FF end inside the text). Identified by its prompt in the code below the text
SyncDescriptor Masm11()
{
    SyncDescriptor d;
    d.id = "masm-1.1";
    d.title = "MASM 1.1";
    d.codec = "masm";
    d.version = "1.1";
    d.extension = "a";
    d.identify = {{0x85B1, "MASM128> "}};
    d.pages = PageIdRule::Fixed;
    d.family = LayoutFamily::GapBuffer;
    d.gapBuffer.fixedStart = 0x970B;
    d.gapBuffer.gapStart = 0x96CC;
    d.gapBuffer.gapEnd = 0x96CE;
    d.gapBuffer.editorFlagAt = 0x851A;
    d.gapBuffer.editorFlagLength = 31;
    d.gapBuffer.end = std::string("\xFF", 1);
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
        // Each checked with dumps (testdata/sync): the text, the file it saves, the title where the editor leaves it
        Alasm("5.09", "5.07", 0x97C5),
        Alasm("5.08", "5.07", 0x97CA),
        Alasm("5.07", "5.07", 0x97BC),
        Alasm("5.05", "5.05", 0x97BC),
        Alasm("5.00", "5.0", 0x9E22),
        Alasm("4.5", "4.5", 0x9E06),
        Alasm("4.46", "4.44", 0x9E6E),
        Alasm("4.45", "4.44", 0x9E53),
        Alasm("4.44", "4.44", 0x9E7E),
        Alasm("4.43", "4.5", 0x9E1B, "", false),
        Alasm("4.42", "4.42", 0x9E2F),
        Alasm("3.8c", "3.8", 0x9951, "3.8c\r Written by"),
        Xas("4.18", "4.18", 0xB51A, "XAS by Max Petrov (HPM) 3.091"),
        Xas("5.05", "5.05", 0xB601, "XAS by Max Petrov (HPM) 5.05 "),   // 5.05 and 5.05SE alike
        Xas("7.43c", "7.43c", 0xB961, "by Max Petrov & Creator v7.43"),
        Xas("7.447", "7.43", 0x9B00, "by Max Petrov & Creator v7.44"),
        Xas("9.07m", "9.07m", 0xB8EA, "XAS 9.07 ReCompiled by Mythos"),
        Xas("9.10", "9.10", 0xB912, "XAS by Max Petrov,64sm by STS"),
        Storm13(),
        Zasm315(),
        Masm11(),
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
