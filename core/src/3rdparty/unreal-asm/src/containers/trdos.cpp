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

namespace
{
constexpr size_t kInfo = 8 * kSector;   // the disk info sector (track 0, sector 9)

/// The file's sectors: its own count, else data + tail rounded up
size_t SectorsOf(const TrdosFile& file)
{
    const size_t body = file.data.size() + file.tail.size();
    return file.sectors ? file.sectors : (body + kSector - 1) / kSector;
}

/// The file's bytes padded to its sectors
std::vector<uint8_t> BodyOf(const TrdosFile& file)
{
    std::vector<uint8_t> body = file.data;
    body.insert(body.end(), file.tail.begin(), file.tail.end());
    body.resize(SectorsOf(file) * kSector, 0);
    return body;
}

/// The 14 catalog bytes TR-DOS and SCL share: name, type, start, length, sectors
void PutEntry(uint8_t* entry, const TrdosFile& file, size_t sectors)
{
    for (size_t i = 0; i < 8; ++i)
        entry[i] = static_cast<uint8_t>(i < file.name.size() ? file.name[i] : ' ');
    entry[8] = static_cast<uint8_t>(file.type);
    entry[9] = static_cast<uint8_t>(file.start & 0xFF);
    entry[10] = static_cast<uint8_t>(file.start >> 8);
    entry[11] = static_cast<uint8_t>(file.length & 0xFF);
    entry[12] = static_cast<uint8_t>(file.length >> 8);
    entry[13] = static_cast<uint8_t>(sectors);
}

bool SameFile(const uint8_t* entry, const TrdosFile& file)
{
    uint8_t wanted[14];
    PutEntry(wanted, file, 0);
    return std::equal(wanted, wanted + 9, entry);
}
}  // namespace

bool ReadScl(std::span<const uint8_t> image, std::vector<TrdosFile>& out, std::string& error)
{
    out.clear();
    if (image.size() < 9 || !std::equal(image.begin(), image.begin() + 8, "SINCLAIR"))
    {
        error = "not an SCL archive (no SINCLAIR mark)";
        return false;
    }
    const size_t count = image[8];
    size_t data = 9 + count * 14;
    if (data > image.size())
    {
        error = "the SCL catalog runs past the end of the archive";
        return false;
    }
    for (size_t i = 0; i < count; ++i)
    {
        const std::span<const uint8_t> entry = image.subspan(9 + i * 14, 14);
        TrdosFile file;
        file.name.assign(reinterpret_cast<const char*>(entry.data()), 8);
        file.type = static_cast<char>(entry[8]);
        file.start = static_cast<uint16_t>(entry[9] | (entry[10] << 8));
        file.length = static_cast<uint16_t>(entry[11] | (entry[12] << 8));
        file.sectors = entry[13];
        const size_t size = static_cast<size_t>(file.sectors) * kSector;
        if (data + size > image.size())
        {
            error = "SCL file " + file.TrimmedName() + " runs past the end of the archive";
            return false;
        }
        const size_t length = std::min<size_t>(file.length, size);
        file.data.assign(image.begin() + static_cast<std::ptrdiff_t>(data), image.begin() + static_cast<std::ptrdiff_t>(data + length));
        file.tail.assign(image.begin() + static_cast<std::ptrdiff_t>(data + length), image.begin() + static_cast<std::ptrdiff_t>(data + size));
        data += size;
        out.push_back(std::move(file));
    }
    return true;
}

std::vector<uint8_t> WriteScl(const std::vector<TrdosFile>& files)
{
    std::vector<uint8_t> out = {'S', 'I', 'N', 'C', 'L', 'A', 'I', 'R', static_cast<uint8_t>(files.size())};
    for (const TrdosFile& file : files)
    {
        uint8_t entry[14];
        PutEntry(entry, file, SectorsOf(file));
        out.insert(out.end(), entry, entry + 14);
    }
    for (const TrdosFile& file : files)
    {
        const std::vector<uint8_t> body = BodyOf(file);
        out.insert(out.end(), body.begin(), body.end());
    }
    uint32_t sum = 0;
    for (const uint8_t b : out)
        sum += b;
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(sum >> (8 * i)));
    return out;
}

void PutFile(std::vector<TrdosFile>& files, const TrdosFile& file)
{
    for (TrdosFile& f : files)
        if (f.name == file.name && f.type == file.type)
        {
            f = file;
            return;
        }
    files.push_back(file);
}

std::vector<uint8_t> NewTrd(const std::string& label)
{
    std::vector<uint8_t> image(160 * 16 * kSector, 0);
    uint8_t* info = image.data() + kInfo;
    info[0xE1] = 0;      // the first free sector and track: track 1
    info[0xE2] = 1;
    info[0xE3] = 0x16;   // 80 tracks, two sides
    info[0xE4] = 0;
    const uint16_t free = 159 * 16;
    info[0xE5] = static_cast<uint8_t>(free & 0xFF);
    info[0xE6] = static_cast<uint8_t>(free >> 8);
    info[0xE7] = 0x10;
    std::fill(info + 0xEA, info + 0xF3, ' ');
    for (size_t i = 0; i < 8; ++i)
        info[0xF5 + i] = static_cast<uint8_t>(i < label.size() ? label[i] : ' ');
    return image;
}

bool PutTrdFile(std::vector<uint8_t>& image, const TrdosFile& file, std::string& error)
{
    if (image.size() < 9 * kSector || image[kInfo + 0xE7] != 0x10)
    {
        error = "not a TR-DOS image (no #10 marker in the disk info sector)";
        return false;
    }
    uint8_t* info = image.data() + kInfo;
    const size_t sectors = SectorsOf(file);
    const std::vector<uint8_t> body = BodyOf(file);
    size_t count = info[0xE4];
    for (size_t i = 0; i < count && i < 128; ++i)
    {
        uint8_t* entry = image.data() + i * 16;
        if (entry[0] == 0)
            break;
        if (!SameFile(entry, file))
            continue;
        if (entry[13] == sectors)
        {
            const size_t offset = (static_cast<size_t>(entry[15]) * 16 + entry[14]) * kSector;
            if (offset + body.size() > image.size())
            {
                error = "catalog entry " + file.TrimmedName() + " points outside the image";
                return false;
            }
            PutEntry(entry, file, sectors);
            std::copy(body.begin(), body.end(), image.begin() + static_cast<std::ptrdiff_t>(offset));
            return true;
        }
        entry[0] = 1;   // deleted; its sectors stay used until the disk is compacted, as TR-DOS leaves them
        info[0xF4]++;
    }
    const size_t freeSectors = info[0xE5] | (info[0xE6] << 8);
    if (count >= 128)
    {
        error = "the TR-DOS catalog is full (128 files)";
        return false;
    }
    const size_t offset = (static_cast<size_t>(info[0xE2]) * 16 + info[0xE1]) * kSector;
    if (sectors > freeSectors || offset + body.size() > image.size())
    {
        error = "not enough free sectors on the disk (" + std::to_string(freeSectors) + " free, " + std::to_string(sectors) + " needed)";
        return false;
    }
    uint8_t* entry = image.data() + count * 16;
    PutEntry(entry, file, sectors);
    entry[14] = info[0xE1];
    entry[15] = info[0xE2];
    std::copy(body.begin(), body.end(), image.begin() + static_cast<std::ptrdiff_t>(offset));
    const size_t next = static_cast<size_t>(info[0xE2]) * 16 + info[0xE1] + sectors;
    info[0xE1] = static_cast<uint8_t>(next % 16);
    info[0xE2] = static_cast<uint8_t>(next / 16);
    info[0xE4] = static_cast<uint8_t>(++count);
    const size_t left = freeSectors - sectors;
    info[0xE5] = static_cast<uint8_t>(left & 0xFF);
    info[0xE6] = static_cast<uint8_t>(left >> 8);
    return true;
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
