#include "loaders/snapshot/szx/szxformat.h"

#include <sstream>

#include "3rdparty/miniz/miniz.h"

namespace szx
{
bool MachineFor(uint8_t id, Machine& machine, std::string& error)
{
    machine = Machine{};
    switch (id)
    {
        case Mid16K:
            machine = {MM_SPECTRUM48, 48, "16K snapshot loaded on a 48K"};
            return true;
        case Mid48K: machine = {MM_SPECTRUM48, 48, {}}; return true;
        case MidNtsc48K:
            machine = {MM_SPECTRUM48, 48, "NTSC 48K snapshot loaded on a PAL 48K"};
            return true;
        case Mid128K: machine = {MM_SPECTRUM128, 128, {}}; return true;
        case Mid128Ke:
            machine = {MM_SPECTRUM128, 128, "128Ke snapshot loaded on a 128K"};
            return true;
        case MidPlus2: machine = {MM_PLUS2, 128, {}}; return true;
        case MidPlus2A: machine = {MM_PLUS2A, 128, {}}; return true;
        case MidPlus3: machine = {MM_PLUS3, 128, {}}; return true;
        case MidPlus3E:
            machine = {MM_PLUS3, 128, "+3e snapshot loaded on a +3"};
            return true;
        case MidPentagon128: machine = {MM_PENTAGON, 128, {}}; return true;
        case MidPentagon512: machine = {MM_PENTAGON, 512, {}}; return true;
        case MidPentagon1024: machine = {MM_PENTAGON, 1024, {}}; return true;
        case MidScorpion: machine = {MM_SCORP, 256, {}}; return true;
        case MidTc2048:
        case MidTc2068:
        case MidTs2068:
        case MidSe:
            error = "machine id " + std::to_string(id) + " (Timex / SE) is not emulated";
            return false;
        default:
            error = "unknown machine id " + std::to_string(id);
            return false;
    }
}

std::optional<uint8_t> IdFor(MEM_MODEL model, uint32_t ramKb)
{
    switch (model)
    {
        case MM_SPECTRUM48: return Mid48K;
        case MM_SPECTRUM128: return Mid128K;
        case MM_PLUS2: return MidPlus2;
        case MM_PLUS2A: return MidPlus2A;
        case MM_PLUS3: return MidPlus3;
        case MM_PENTAGON:
            if (ramKb <= 128)
                return MidPentagon128;
            if (ramKb <= 512)
                return MidPentagon512;
            if (ramKb <= 1024)
                return MidPentagon1024;
            return std::nullopt;
        case MM_SCORP:
            if (ramKb <= 256)
                return MidScorpion;
            return std::nullopt;
        default: return std::nullopt;
    }
}

std::vector<uint8_t> PagesOf(uint8_t id)
{
    uint8_t count = 8;
    switch (id)
    {
        case Mid16K: return {5};
        case Mid48K:
        case MidNtsc48K:
        case MidTc2048:
        case MidTc2068:
        case MidTs2068: return {5, 2, 0};
        case MidPentagon512: count = 32; break;
        case MidPentagon1024: count = 64; break;
        case MidScorpion: count = 16; break;
        default: break;
    }
    std::vector<uint8_t> pages(count);
    for (uint8_t page = 0; page < count; page++)
        pages[page] = page;
    return pages;
}

bool HasPort1FFD(uint8_t id)
{
    return id == MidPlus2A || id == MidPlus3 || id == MidPlus3E || id == MidScorpion;
}

bool HasPortEFF7(uint8_t id)
{
    return id == MidPentagon1024;
}

bool HasAy(uint8_t id)
{
    return id != Mid16K && id != Mid48K && id != MidNtsc48K;
}

void Report::Add(std::string block, Outcome outcome, std::string note)
{
    entries.push_back({std::move(block), outcome, std::move(note)});
}

std::string Report::ToText() const
{
    static const char* names[] = {"applied", "approximated", "ignored", "unknown"};
    std::ostringstream out;
    for (const ReportEntry& entry : entries)
    {
        out << entry.block << ": " << names[static_cast<int>(entry.outcome)];
        if (!entry.note.empty())
            out << " (" << entry.note << ")";
        out << "\n";
    }
    for (const std::string& warning : warnings)
        out << "warning: " << warning << "\n";
    return out.str();
}

std::string BlockName(uint32_t id)
{
    std::string name;
    for (int i = 0; i < 4; i++)
    {
        const char c = static_cast<char>(id >> (8 * i));
        if (c >= 0x20 && c < 0x7F)
            name += c;
    }
    return name;
}

bool Inflate(const uint8_t* data, size_t size, size_t expected, std::vector<uint8_t>& out)
{
    out.assign(expected, 0);
    mz_stream stream{};
    if (mz_inflateInit(&stream) != MZ_OK)
        return false;
    stream.next_in = data;
    stream.avail_in = static_cast<unsigned int>(size);
    stream.next_out = out.data();
    stream.avail_out = static_cast<unsigned int>(expected);
    const int status = mz_inflate(&stream, MZ_FINISH);
    // Exactly `expected` bytes and the stream's end: a longer stream stops
    // with MZ_BUF_ERROR (no room left), a shorter one leaves avail_out > 0
    const bool ok = status == MZ_STREAM_END && stream.avail_out == 0;
    mz_inflateEnd(&stream);
    return ok;
}

std::vector<uint8_t> Deflate(const uint8_t* data, size_t size)
{
    mz_ulong length = mz_compressBound(static_cast<mz_ulong>(size));
    std::vector<uint8_t> out(length);
    if (mz_compress2(out.data(), &length, data, static_cast<mz_ulong>(size), MZ_BEST_COMPRESSION) != MZ_OK)
        return {};
    out.resize(length);
    return out;
}
}  // namespace szx
