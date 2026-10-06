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
    hints.extension = std::string("$") + type;
    hints.slack = tail;
    return hints;
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

bool ReadTrd(std::span<const uint8_t> image, std::vector<TrdosFile>& out, std::string& error)
{
    out.clear();
    if (image.size() < 9 * kSector || image[8 * kSector + 0xE7] != 0x10)
    {
        error = "not a TR-DOS image (no #10 marker in the disk info sector)";
        return false;
    }
    for (size_t i = 0; i < 128; ++i)
    {
        const std::span<const uint8_t> entry = image.subspan(i * 16, 16);
        if (entry[0] == 0)
            break;   // end of the catalog
        if (entry[0] == 1)
            continue;   // deleted
        TrdosFile file;
        file.name.assign(reinterpret_cast<const char*>(entry.data()), 8);
        file.type = static_cast<char>(entry[8]);
        file.start = static_cast<uint16_t>(entry[9] | (entry[10] << 8));
        file.length = static_cast<uint16_t>(entry[11] | (entry[12] << 8));
        file.sectors = entry[13];
        const size_t offset = (static_cast<size_t>(entry[15]) * 16 + entry[14]) * kSector;
        const size_t size = static_cast<size_t>(file.sectors) * kSector;
        if (offset + size > image.size())
        {
            error = "catalog entry " + file.TrimmedName() + " points outside the image";
            return false;
        }
        const size_t length = std::min<size_t>(file.length, size);   // a length beyond the sectors: as in ReadHobeta
        file.data.assign(image.begin() + static_cast<std::ptrdiff_t>(offset), image.begin() + static_cast<std::ptrdiff_t>(offset + length));
        file.tail.assign(image.begin() + static_cast<std::ptrdiff_t>(offset + length), image.begin() + static_cast<std::ptrdiff_t>(offset + size));
        out.push_back(std::move(file));
    }
    return true;
}
}  // namespace unrealasm::containers
