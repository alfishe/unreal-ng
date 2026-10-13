#include "unrealasm/containers.h"

#include <algorithm>

namespace unrealasm::containers
{
namespace
{
constexpr size_t kHobetaHeader = 17;
constexpr size_t kSector = 256;
}  // namespace

CatalogHints TrdosFile::Hints() const
{
    CatalogHints hints;
    hints.type = type;
    hints.start = start;
    hints.length = length;
    hints.name = TrimmedName();
    hints.extension = extension.empty() ? std::string("$") + type : extension;
    if (!extension.empty())
        hints.type = 0;
    hints.slack = tail;
    return hints;
}

std::string TrdosFile::FullName() const
{
    if (extension.empty() && type == 0)
        return TrimmedName();
    return TrimmedName() + "." + (extension.empty() ? std::string(1, type) : extension);
}

std::string TrdosFile::TrimmedName() const
{
    size_t end = name.size();
    while (end && name[end - 1] == ' ')
        --end;
    return name.substr(0, end);
}

uint16_t HobetaChecksum(std::span<const uint8_t> header15)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < 15 && i < header15.size(); ++i)
        sum += header15[i] * 257u + static_cast<uint32_t>(i);
    return static_cast<uint16_t>(sum & 0xFFFF);
}

bool ReadHobeta(std::span<const uint8_t> bytes, TrdosFile& out, std::string& error)
{
    if (bytes.size() < kHobetaHeader)
    {
        error = "shorter than a hobeta header";
        return false;
    }
    const uint16_t stored = static_cast<uint16_t>(bytes[15] | (bytes[16] << 8));
    if (stored != HobetaChecksum(bytes.first(15)))
    {
        error = "hobeta header checksum mismatch";
        return false;
    }
    out = {};
    out.name.assign(reinterpret_cast<const char*>(bytes.data()), 8);
    out.type = static_cast<char>(bytes[8]);
    out.start = static_cast<uint16_t>(bytes[9] | (bytes[10] << 8));
    out.length = static_cast<uint16_t>(bytes[11] | (bytes[12] << 8));
    out.sectors = bytes[14];
    // A length field larger than the body is kept as it is (XAS does not use the field: 8018 on a 15-sector file)
    const std::span<const uint8_t> body = bytes.subspan(kHobetaHeader);
    const size_t length = std::min<size_t>(out.length, body.size());
    out.data.assign(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(length));
    out.tail.assign(body.begin() + static_cast<std::ptrdiff_t>(length), body.end());
    return true;
}

std::vector<uint8_t> WriteHobeta(const TrdosFile& file)
{
    std::vector<uint8_t> out(kHobetaHeader, 0);
    for (size_t i = 0; i < 8; ++i)
        out[i] = static_cast<uint8_t>(i < file.name.size() ? file.name[i] : ' ');
    out[8] = static_cast<uint8_t>(file.type);
    out[9] = static_cast<uint8_t>(file.start & 0xFF);
    out[10] = static_cast<uint8_t>(file.start >> 8);
    out[11] = static_cast<uint8_t>(file.length & 0xFF);
    out[12] = static_cast<uint8_t>(file.length >> 8);
    const size_t bodySize = file.data.size() + file.tail.size();
    const size_t sectors = (bodySize + kSector - 1) / kSector;
    out[14] = file.sectors ? file.sectors : static_cast<uint8_t>(sectors);
    const uint16_t checksum = HobetaChecksum(std::span<const uint8_t>(out).first(15));
    out[15] = static_cast<uint8_t>(checksum & 0xFF);
    out[16] = static_cast<uint8_t>(checksum >> 8);
    out.insert(out.end(), file.data.begin(), file.data.end());
    out.insert(out.end(), file.tail.begin(), file.tail.end());
    out.resize(kHobetaHeader + sectors * kSector, 0);
    return out;
}

namespace
{
constexpr size_t kPlus3dosHeader = 128;
}

bool ReadPlus3dos(std::span<const uint8_t> bytes, Plus3dosFile& out, std::string& error)
{
    static const char magic[] = "PLUS3DOS";
    if (bytes.size() < kPlus3dosHeader || !std::equal(magic, magic + 8, bytes.begin()) || bytes[8] != 0x1A)
    {
        error = "not a +3DOS file (no PLUS3DOS header)";
        return false;
    }
    uint8_t sum = 0;
    for (size_t i = 0; i < 127; ++i)
        sum = static_cast<uint8_t>(sum + bytes[i]);
    if (sum != bytes[127])
    {
        error = "+3DOS header checksum mismatch";
        return false;
    }
    out = {};
    const uint32_t total = bytes[11] | (bytes[12] << 8) | (bytes[13] << 16) | (static_cast<uint32_t>(bytes[14]) << 24);
    out.type = bytes[15];
    out.length = static_cast<uint16_t>(bytes[16] | (bytes[17] << 8));
    out.start = static_cast<uint16_t>(bytes[18] | (bytes[19] << 8));
    const size_t end = std::min<size_t>(total > kPlus3dosHeader ? total : bytes.size(), bytes.size());
    out.data.assign(bytes.begin() + kPlus3dosHeader, bytes.begin() + static_cast<std::ptrdiff_t>(std::max(end, kPlus3dosHeader)));
    out.tail.assign(bytes.begin() + static_cast<std::ptrdiff_t>(std::max(end, kPlus3dosHeader)), bytes.end());
    return true;
}

std::vector<uint8_t> WritePlus3dos(const Plus3dosFile& file)
{
    std::vector<uint8_t> out(kPlus3dosHeader, 0);
    const char magic[] = "PLUS3DOS";
    std::copy(magic, magic + 8, out.begin());
    out[8] = 0x1A;
    out[9] = 1;
    out[10] = 0;
    const uint32_t total = static_cast<uint32_t>(kPlus3dosHeader + file.data.size());
    for (int i = 0; i < 4; ++i)
        out[11 + i] = static_cast<uint8_t>(total >> (8 * i));
    out[15] = file.type;
    out[16] = static_cast<uint8_t>(file.data.size());
    out[17] = static_cast<uint8_t>(file.data.size() >> 8);
    out[18] = static_cast<uint8_t>(file.start);
    out[19] = static_cast<uint8_t>(file.start >> 8);
    uint8_t sum = 0;
    for (size_t i = 0; i < 127; ++i)
        sum = static_cast<uint8_t>(sum + out[i]);
    out[127] = sum;
    out.insert(out.end(), file.data.begin(), file.data.end());
    return out;
}
}  // namespace unrealasm::containers
