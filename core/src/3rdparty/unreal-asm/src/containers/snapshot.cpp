#include "unrealasm/snapshot.h"

#include <algorithm>
#include <cctype>

#include "inflate.h"

namespace unrealasm::containers
{
namespace
{
constexpr size_t kPage = 16384;
constexpr size_t kSnaHeader = 27;
constexpr size_t kSna48 = kSnaHeader + 3 * kPage;   // 49179
constexpr size_t kSna128 = kSna48 + 4 + 5 * kPage;  // 131103
constexpr size_t kSna128Twice = kSna128 + kPage;    // 147487: the paged-in page is 5 or 2, so it comes twice

uint16_t Word(std::span<const uint8_t> bytes, size_t at)
{
    return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

uint32_t Dword(std::span<const uint8_t> bytes, size_t at)
{
    return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

void PutDword(std::vector<uint8_t>& out, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// The 128K windows: ROM, page 5, page 2, the paged page
std::array<int, 4> Windows128(int paged)
{
    return {-1, 5, 2, paged};
}

/// +2A / +3 all-RAM modes (port #1FFD bit 0): the four pages of each
std::array<int, 4> SpecialWindows(uint8_t port1FFD)
{
    static const std::array<std::array<int, 4>, 4> modes = {{{0, 1, 2, 3}, {4, 5, 6, 7}, {4, 5, 6, 3}, {4, 7, 6, 3}}};
    return modes[(port1FFD >> 1) & 3];
}

// SNA ---------------------------------------------------------------------------------------------------------------

/// Where each RAM page lies in an SNA file (a page paged in at #C000 that is 5 or 2 lies there twice)
std::vector<std::pair<int, size_t>> SnaPlaces(size_t size, uint8_t port7FFD)
{
    std::vector<std::pair<int, size_t>> places = {{5, kSnaHeader}, {2, kSnaHeader + kPage}};
    if (size == kSna48)
    {
        places.push_back({0, kSnaHeader + 2 * kPage});
        return places;
    }
    const int paged = port7FFD & 7;
    places.push_back({paged, kSnaHeader + 2 * kPage});
    size_t at = kSna48 + 4;
    for (int page = 0; page < 8; ++page)
    {
        if (page == 5 || page == 2 || page == paged)
            continue;
        places.push_back({page, at});
        at += kPage;
    }
    return places;
}

bool ReadSna(std::span<const uint8_t> file, Snapshot& out, std::string& error)
{
    if (file.size() != kSna48 && file.size() != kSna128 && file.size() != kSna128Twice)
    {
        error = "an SNA is 49179, 131103 or 147487 bytes, this file " + std::to_string(file.size());
        return false;
    }
    const bool is48 = file.size() == kSna48;
    out.format = SnapshotFormat::Sna;
    out.sp = Word(file, 23);
    out.port7FFD = is48 ? 0 : file[kSna48 + 2];
    const int paged = out.port7FFD & 7;
    if (file.size() == kSna128Twice && paged != 5 && paged != 2)
    {
        error = "an SNA of 147487 bytes has page 5 or 2 paged in, this one page " + std::to_string(paged);
        return false;
    }
    if (file.size() == kSna128 && (paged == 5 || paged == 2))
    {
        error = "an SNA of 131103 bytes cannot have page 5 or 2 paged in";
        return false;
    }
    out.machine = is48 ? "48K" : "128K";
    out.ramPages = is48 ? 3 : 8;
    out.windows = is48 ? std::array<int, 4>{-1, 5, 2, 0} : Windows128(paged);
    for (const auto& [page, at] : SnaPlaces(file.size(), out.port7FFD))
        if (!out.pages.count(page))
            out.pages[page].assign(file.begin() + static_cast<std::ptrdiff_t>(at), file.begin() + static_cast<std::ptrdiff_t>(at + kPage));
    if (is48)
    {
        // A 48K SNA keeps PC on the stack
        const uint16_t sp = out.sp;
        const int window = sp >> 14;
        const auto& page = out.pages[out.windows[window]];
        out.pc = window == 0 || (sp & 0x3FFF) == 0x3FFF ? 0 : static_cast<uint16_t>(page[sp & 0x3FFF] | (page[(sp & 0x3FFF) + 1] << 8));
    }
    else
        out.pc = Word(file, kSna48);
    return true;
}

bool WriteSna(const Snapshot& s, const std::map<int, std::vector<uint8_t>>& changed, std::vector<uint8_t>& out)
{
    out = s.bytes;
    for (const auto& [page, at] : SnaPlaces(out.size(), s.port7FFD))
        if (const auto it = changed.find(page); it != changed.end())
            std::copy(it->second.begin(), it->second.end(), out.begin() + static_cast<std::ptrdiff_t>(at));
    return true;
}

// Z80 ---------------------------------------------------------------------------------------------------------------

/// The Z80 format's run-length coding: ED ED n b = n times b; a run of 5 or more, or of 2 or more EDs, is coded; the
/// byte after a single ED is never the start of a run
std::vector<uint8_t> Z80Compress(std::span<const uint8_t> data)
{
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < data.size())
    {
        const uint8_t b = data[i];
        size_t run = 1;
        while (i + run < data.size() && data[i + run] == b && run < 255)
            ++run;
        if (run >= 5 || (b == 0xED && run >= 2))
        {
            out.insert(out.end(), {0xED, 0xED, static_cast<uint8_t>(run), b});
            i += run;
            continue;
        }
        out.push_back(b);
        ++i;
        if (b == 0xED && i < data.size())
            out.push_back(data[i++]);
    }
    return out;
}

/// Decodes `data` up to `limit` bytes; v1 data ends at the 00 ED ED 00 mark (`v1`)
std::vector<uint8_t> Z80Expand(std::span<const uint8_t> data, size_t limit, bool v1)
{
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < data.size() && out.size() < limit)
    {
        if (v1 && i + 3 < data.size() && data[i] == 0 && data[i + 1] == 0xED && data[i + 2] == 0xED && data[i + 3] == 0)
            break;
        if (i + 3 < data.size() && data[i] == 0xED && data[i + 1] == 0xED)
        {
            out.insert(out.end(), data[i + 2], data[i + 3]);
            i += 4;
        }
        else
            out.push_back(data[i++]);
    }
    return out;
}

struct Z80Layout
{
    bool v1 = false;
    size_t blocksAt = 0;           ///< v2 / v3: where the page blocks start
    bool is128 = false;
    bool scorpion = false;
    std::string machine;
};

bool Z80Header(std::span<const uint8_t> file, Z80Layout& layout, std::string& error)
{
    if (file.size() < 30)
    {
        error = "shorter than a Z80 header";
        return false;
    }
    if (Word(file, 6) != 0)
    {
        layout.v1 = true;
        layout.machine = "48K";
        return true;
    }
    if (file.size() < 34)
    {
        error = "the Z80 header ends before its extension";
        return false;
    }
    const uint16_t extra = Word(file, 30);
    if (extra != 23 && extra != 54 && extra != 55)
    {
        error = "a Z80 header extension of " + std::to_string(extra) + " bytes (23, 54 or 55 known)";
        return false;
    }
    layout.blocksAt = 32 + extra;
    if (file.size() < layout.blocksAt)
    {
        error = "the Z80 header ends early";
        return false;
    }
    const uint8_t hardware = file[34];
    const bool v2 = extra == 23;
    layout.is128 = v2 ? hardware >= 3 && hardware <= 6 : (hardware >= 4 && hardware <= 10) || hardware == 12 || hardware == 13;
    layout.scorpion = !v2 && hardware == 10;
    static const char* const names[] = {"48K", "48K", "SamRam", "48K", "128K", "128K", "128K", "+3", "+3", "Pentagon 128", "Scorpion 256",
                                        "Didaktik", "+2", "+2A", "TC2048", "TC2068"};
    if (v2 && hardware >= 3 && hardware <= 4)
        layout.machine = "128K";
    else if (hardware < 16)
        layout.machine = names[hardware];
    else
        layout.machine = "hardware " + std::to_string(hardware);
    if (!layout.is128 && hardware > (v2 ? 2 : 3))
    {
        error = "a Z80 snapshot of " + layout.machine + " (only the 48K and 128K families are read)";
        return false;
    }
    return true;
}

/// The RAM page a v2 / v3 block number names (48K: 8 #4000, 4 #8000, 5 #C000)
int Z80Page(int block, const Z80Layout& layout)
{
    if (layout.is128)
        return block >= 3 && block < (layout.scorpion ? 19 : 11) ? block - 3 : -1;
    return block == 8 ? 5 : block == 4 ? 2 : block == 5 ? 0 : -1;
}

bool ReadZ80(std::span<const uint8_t> file, Snapshot& out, std::string& error)
{
    Z80Layout layout;
    if (!Z80Header(file, layout, error))
        return false;
    out.format = SnapshotFormat::Z80;
    out.machine = layout.machine;
    out.sp = Word(file, 8);
    if (layout.v1)
    {
        const uint8_t flags = file[12] == 0xFF ? 1 : file[12];
        const std::span<const uint8_t> data = file.subspan(30);
        const std::vector<uint8_t> memory = (flags & 0x20) ? Z80Expand(data, 3 * kPage, true) : std::vector<uint8_t>(data.begin(), data.end());
        if (memory.size() < 3 * kPage)
        {
            error = "a Z80 v1 snapshot holds " + std::to_string(memory.size()) + " bytes of RAM, not 49152";
            return false;
        }
        out.pc = Word(file, 6);
        out.ramPages = 3;
        out.windows = {-1, 5, 2, 0};
        out.pages[5].assign(memory.begin(), memory.begin() + kPage);
        out.pages[2].assign(memory.begin() + kPage, memory.begin() + 2 * kPage);
        out.pages[0].assign(memory.begin() + 2 * kPage, memory.begin() + 3 * kPage);
        return true;
    }
    out.pc = Word(file, 32);
    out.port7FFD = layout.is128 ? file[35] : 0;
    out.ramPages = layout.is128 ? (layout.scorpion ? 16 : 8) : 3;
    int paged = out.port7FFD & 7;
    if (layout.scorpion && layout.blocksAt == 32 + 55 && (file[86] & 0x10))
        paged += 8;
    out.windows = layout.is128 ? Windows128(paged) : std::array<int, 4>{-1, 5, 2, 0};
    size_t at = layout.blocksAt;
    while (at + 3 <= file.size())
    {
        const uint16_t length = Word(file, at);
        const int block = file[at + 2];
        at += 3;
        const size_t size = length == 0xFFFF ? kPage : length;
        if (at + size > file.size())
        {
            error = "Z80 block " + std::to_string(block) + " runs past the end of the file";
            return false;
        }
        const std::span<const uint8_t> data = file.subspan(at, size);
        std::vector<uint8_t> page = length == 0xFFFF ? std::vector<uint8_t>(data.begin(), data.end()) : Z80Expand(data, kPage, false);
        if (page.size() != kPage)
        {
            error = "Z80 block " + std::to_string(block) + " expands to " + std::to_string(page.size()) + " bytes, not 16384";
            return false;
        }
        if (const int p = Z80Page(block, layout); p >= 0)
            out.pages[p] = std::move(page);
        at += size;
    }
    return true;
}

bool WriteZ80(const Snapshot& s, const std::map<int, std::vector<uint8_t>>& changed, std::vector<uint8_t>& out, std::string& error)
{
    Z80Layout layout;
    if (!Z80Header(s.bytes, layout, error))
        return false;
    const std::span<const uint8_t> file = s.bytes;
    if (layout.v1)
    {
        // The whole 48K anew, compressed, with the end mark
        std::vector<uint8_t> memory;
        for (const int page : {5, 2, 0})
        {
            const auto it = changed.find(page);
            const std::vector<uint8_t>& bytes = it != changed.end() ? it->second : s.pages.at(page);
            memory.insert(memory.end(), bytes.begin(), bytes.end());
        }
        out.assign(file.begin(), file.begin() + 30);
        out[12] = static_cast<uint8_t>((file[12] == 0xFF ? 1 : file[12]) | 0x20);
        const std::vector<uint8_t> packed = Z80Compress(memory);
        out.insert(out.end(), packed.begin(), packed.end());
        out.insert(out.end(), {0x00, 0xED, 0xED, 0x00});
        return true;
    }
    out.assign(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(layout.blocksAt));
    size_t at = layout.blocksAt;
    while (at + 3 <= file.size())
    {
        const uint16_t length = Word(file, at);
        const int block = file[at + 2];
        const size_t size = length == 0xFFFF ? kPage : length;
        const int page = Z80Page(block, layout);
        const auto it = page >= 0 ? changed.find(page) : changed.end();
        if (it == changed.end())
            out.insert(out.end(), file.begin() + static_cast<std::ptrdiff_t>(at), file.begin() + static_cast<std::ptrdiff_t>(at + 3 + size));
        else
        {
            const std::vector<uint8_t> packed = Z80Compress(it->second);
            // v3 stores a block that does not shrink as it is (length #FFFF); v2 has only the compressed form
            const bool raw = packed.size() >= kPage && layout.blocksAt != 32 + 23;
            const std::vector<uint8_t>& body = raw ? it->second : packed;
            const uint16_t written = raw ? 0xFFFF : static_cast<uint16_t>(packed.size());
            out.insert(out.end(), {static_cast<uint8_t>(written & 0xFF), static_cast<uint8_t>(written >> 8), static_cast<uint8_t>(block)});
            out.insert(out.end(), body.begin(), body.end());
        }
        at += 3 + size;
    }
    return true;
}

// SZX ---------------------------------------------------------------------------------------------------------------

struct SzxMachine
{
    const char* name;
    int pages;
};

/// The machine ids of SZX 1.5
SzxMachine SzxMachineOf(uint8_t id)
{
    switch (id)
    {
        case 0: return {"16K", 1};
        case 1: return {"48K", 3};
        case 2: return {"128K", 8};
        case 3: return {"+2", 8};
        case 4: return {"+2A", 8};
        case 5: return {"+3", 8};
        case 6: return {"+3e", 8};
        case 7: return {"Pentagon 128", 8};
        case 8: return {"TC2048", 3};
        case 9: return {"TC2068", 3};
        case 10: return {"Scorpion 256", 16};
        case 11: return {"SE", 8};
        case 12: return {"TS2068", 3};
        case 13: return {"Pentagon 512", 32};
        case 14: return {"Pentagon 1024", 64};
        case 15: return {"NTSC 48K", 3};
        case 16: return {"128Ke", 8};
        default: return {"unknown", 0};
    }
}

bool ReadSzx(std::span<const uint8_t> file, Snapshot& out, std::string& error)
{
    if (file.size() < 8 || !std::equal(file.begin(), file.begin() + 4, "ZXST"))
    {
        error = "not an SZX file (no ZXST mark)";
        return false;
    }
    const uint8_t id = file[6];
    const SzxMachine machine = SzxMachineOf(id);
    if (machine.pages == 0)
    {
        error = "an SZX of machine id " + std::to_string(id);
        return false;
    }
    out.format = SnapshotFormat::Szx;
    out.machine = machine.name;
    out.ramPages = machine.pages;
    bool registers = false, paging = false;
    uint8_t portExtra = 0;
    size_t at = 8;
    while (at + 8 <= file.size())
    {
        const std::string block(file.begin() + static_cast<std::ptrdiff_t>(at), file.begin() + static_cast<std::ptrdiff_t>(at + 4));
        const uint32_t size = Dword(file, at + 4);
        at += 8;
        if (at + size > file.size())
        {
            error = "SZX block " + block + " runs past the end of the file";
            return false;
        }
        const std::span<const uint8_t> body = file.subspan(at, size);
        if (block == "Z80R" && size >= 24)
        {
            out.sp = Word(body, 20);
            out.pc = Word(body, 22);
            registers = true;
        }
        else if (block == "SPCR" && size >= 3)
        {
            out.port7FFD = body[1];
            portExtra = body[2];
            paging = true;
        }
        else if (block == "RAMP" && size >= 3)
        {
            const bool compressed = Word(body, 0) & 1;
            const int page = body[2];
            std::vector<uint8_t> bytes;
            if (compressed)
            {
                if (!InflateZlib(body.subspan(3), bytes, error))
                {
                    error = "SZX RAMP page " + std::to_string(page) + ": " + error;
                    return false;
                }
            }
            else
                bytes.assign(body.begin() + 3, body.end());
            if (bytes.size() != kPage)
            {
                error = "SZX RAMP page " + std::to_string(page) + " holds " + std::to_string(bytes.size()) + " bytes, not 16384";
                return false;
            }
            out.pages[page] = std::move(bytes);
        }
        at += size;
    }
    if (!registers)
    {
        error = "the SZX has no Z80R block";
        return false;
    }
    if (machine.pages <= 3)
        out.windows = {-1, 5, machine.pages == 3 ? 2 : -1, machine.pages == 3 ? 0 : -1};
    else if (!paging)
        out.windows = Windows128(0);
    else if ((id == 4 || id == 5 || id == 6) && (portExtra & 1))
        out.windows = SpecialWindows(portExtra);
    else
    {
        int paged = out.port7FFD & 7;
        if (id == 13 || id == 14)
            paged |= (out.port7FFD >> 3) & 0x18;   // Pentagon 512: bits 6, 7 the pages' bits 3, 4
        if (id == 14 && !(portExtra & 4))
            paged |= out.port7FFD & 0x20;          // Pentagon 1024: bit 5 the page's bit 5 unless #EFF7 bit 2 locks it
        if (id == 10 && (portExtra & 0x10))
            paged += 8;                            // Scorpion: #1FFD bit 4 the upper 128K
        out.windows = Windows128(paged);
    }
    return true;
}

bool WriteSzx(const Snapshot& s, const std::map<int, std::vector<uint8_t>>& changed, std::vector<uint8_t>& out)
{
    const std::span<const uint8_t> file = s.bytes;
    out.assign(file.begin(), file.begin() + 8);
    size_t at = 8;
    while (at + 8 <= file.size())
    {
        const std::string block(file.begin() + static_cast<std::ptrdiff_t>(at), file.begin() + static_cast<std::ptrdiff_t>(at + 4));
        const uint32_t size = Dword(file, at + 4);
        const auto it = block == "RAMP" && size >= 3 ? changed.find(file[at + 10]) : changed.end();
        if (it == changed.end())
            out.insert(out.end(), file.begin() + static_cast<std::ptrdiff_t>(at), file.begin() + static_cast<std::ptrdiff_t>(at + 8 + size));
        else
        {
            out.insert(out.end(), {'R', 'A', 'M', 'P'});
            PutDword(out, static_cast<uint32_t>(3 + kPage));
            out.insert(out.end(), {0x00, 0x00, static_cast<uint8_t>(it->first)});   // flags 0: not compressed
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
        at += 8 + size;
    }
    return true;
}
}  // namespace

std::string SnapshotFormatName(SnapshotFormat format)
{
    return format == SnapshotFormat::Sna ? "sna" : format == SnapshotFormat::Z80 ? "z80" : "szx";
}

bool IsSnapshotExtension(const std::string& extension)
{
    const std::string e = Lower(extension);
    return e == "sna" || e == "z80" || e == "szx";
}

bool ReadSnapshot(std::span<const uint8_t> file, const std::string& extension, Snapshot& out, std::string& error)
{
    out = {};
    std::string e = Lower(extension);
    if (e.empty())
        e = file.size() >= 4 && std::equal(file.begin(), file.begin() + 4, "ZXST") ? "szx"
            : file.size() == kSna48 || file.size() == kSna128 || file.size() == kSna128Twice ? "sna"
                                                                                               : "z80";
    bool ok = false;
    if (e == "sna")
        ok = ReadSna(file, out, error);
    else if (e == "z80")
        ok = ReadZ80(file, out, error);
    else if (e == "szx")
        ok = ReadSzx(file, out, error);
    else
        error = "not a snapshot extension: " + extension;
    if (ok)
        out.bytes.assign(file.begin(), file.end());
    return ok;
}

bool WriteSnapshot(const Snapshot& snapshot, const std::map<int, std::vector<uint8_t>>& changed, std::vector<uint8_t>& out,
                   std::string& error)
{
    for (const auto& [page, bytes] : changed)
    {
        if (!snapshot.pages.count(page))
        {
            error = "the snapshot holds no RAM page " + std::to_string(page);
            return false;
        }
        if (bytes.size() != kPage)
        {
            error = "page " + std::to_string(page) + " is " + std::to_string(bytes.size()) + " bytes, not 16384";
            return false;
        }
    }
    switch (snapshot.format)
    {
        case SnapshotFormat::Sna: return WriteSna(snapshot, changed, out);
        case SnapshotFormat::Z80: return WriteZ80(snapshot, changed, out, error);
        case SnapshotFormat::Szx: return WriteSzx(snapshot, changed, out);
    }
    return false;
}
}  // namespace unrealasm::containers
