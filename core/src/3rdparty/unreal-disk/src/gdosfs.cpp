#include "unrealdisk/gdosfs.h"

#include <algorithm>
#include <cstring>

#include "unrealdisk/diskimage.h"

namespace unrealdisk
{
namespace
{
uint16_t Word(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

constexpr uint8_t kMaxType = 11;
constexpr uint32_t kMapOffset = 15;
constexpr uint32_t kMapBits = 195 * 8;
constexpr uint32_t kHeaderOffset = 211;

/// The sector map's bit of a track / sector: track 4 sector 1 is bit 0, side 1 (bit 7 of the track) after the 80
/// cylinders of side 0
uint32_t MapBit(uint8_t track, uint8_t sector)
{
    const uint32_t logical = (track & 0x7F) + ((track & 0x80) ? 80u : 0u);
    return (logical - 4) * 10 + sector - 1;
}
constexpr uint32_t kSnapshot48Length = 49152;
constexpr size_t kTapeHeader = 9;
}  // namespace

const uint8_t* GdosFileSystem::Sector(uint8_t track, uint8_t sector) const
{
    const uint8_t cylinder = track & 0x7F;
    const uint8_t side = track >> 7;
    if (sector < 1 || sector > kSectorsPerTrack || cylinder >= _image.getCylinders() || side >= _image.getSides())
        return nullptr;
    DiskImage::Track* t = _image.getTrackForCylinderAndSide(cylinder, side);
    const DiskImage::Sector* s = t ? t->findSector(sector) : nullptr;
    return s && s->hasData && s->dataSize == kSector ? s->data : nullptr;
}

bool GdosFileSystem::Entry(uint32_t slot, const uint8_t*& entry) const
{
    const uint8_t cylinder = static_cast<uint8_t>(slot / (kSectorsPerTrack * 2));
    const uint8_t sector = static_cast<uint8_t>(slot % (kSectorsPerTrack * 2) / 2 + 1);
    const uint8_t* s = Sector(cylinder, sector);
    entry = s ? s + (slot % 2) * 256 : nullptr;
    return entry != nullptr;
}

bool GdosFileSystem::IsGdos(std::string* why) const
{
    auto fail = [why](const std::string& reason) {
        if (why)
            *why = reason;
        return false;
    };
    if (_image.getSides() != 2 || _image.getCylinders() < kDirectoryCylinders + 1)
        return fail("not a two-sided disk of 80 cylinders");
    for (uint32_t slot = 0; slot < kEntries; slot++)
    {
        const uint8_t* e = nullptr;
        if (!Entry(slot, e))
            return fail("directory sector of entry " + std::to_string(slot) + " is not a 512-byte sector 1-10");
        const uint8_t type = e[0] & 0x3F;
        if (e[0] == 0)
            continue;
        if (type == 0 || type > kMaxType)
            return fail("entry " + std::to_string(slot) + " has type " + std::to_string(e[0]) + " (not G+DOS)");
        const uint8_t cylinder = e[13] & 0x7F;
        const bool side1 = (e[13] & 0x80) != 0;
        if (e[14] < 1 || e[14] > kSectorsPerTrack || cylinder >= _image.getCylinders() ||
            (!side1 && cylinder < kDirectoryCylinders))
            return fail("entry " + std::to_string(slot) + " starts at track " + std::to_string(e[13]) + " sector " +
                        std::to_string(e[14]));
    }
    return true;
}

bool GdosFileSystem::List(std::vector<GdosFile>& out, std::string& error) const
{
    for (uint32_t slot = 0; slot < kEntries; slot++)
    {
        const uint8_t* e = nullptr;
        if (!Entry(slot, e))
        {
            error = "directory sector of entry " + std::to_string(slot) + " does not read";
            return false;
        }
        if ((e[0] & 0x3F) == 0)
            continue;
        GdosFile f;
        f.slot = slot;
        f.type = e[0] & 0x3F;
        f.hidden = (e[0] & 0x40) != 0;
        f.protect = (e[0] & 0x80) != 0;
        f.name.assign(reinterpret_cast<const char*>(e + 1), 10);
        while (!f.name.empty() && (f.name.back() == ' ' || f.name.back() == 0))
            f.name.pop_back();
        f.sectors = static_cast<uint16_t>(e[11] << 8 | e[12]);
        f.track = e[13];
        f.sector = e[14];
        const uint32_t data = static_cast<uint32_t>(f.sectors) * kDataPerSector;
        if (f.HasTapeHeader())
        {
            f.length = std::min<uint32_t>(Word(e + 212), data > kTapeHeader ? data - kTapeHeader : 0);
            f.start = Word(e + 214);
            f.param = Word(e + 216);
            f.autostart = Word(e + 218);
        }
        else if (f.type == 5)
            f.length = std::min(kSnapshot48Length, data);
        else
            f.length = data;
        out.push_back(std::move(f));
    }
    return true;
}

bool GdosFileSystem::Read(const GdosFile& file, std::vector<uint8_t>& data, std::string& error) const
{
    data.clear();
    const size_t skip = file.HasTapeHeader() ? kTapeHeader : 0;
    const size_t want = skip + file.length;
    std::vector<uint8_t> raw;
    raw.reserve(want);
    uint8_t track = file.track, sector = file.sector;
    for (uint32_t k = 0; k < file.sectors && raw.size() < want; k++)
    {
        const uint8_t* s = Sector(track, sector);
        if (!s)
        {
            error = file.name + ": sector " + std::to_string(k + 1) + " (track " + std::to_string(track) + " sector " +
                    std::to_string(sector) + ") does not read";
            break;
        }
        raw.insert(raw.end(), s, s + kDataPerSector);
        track = s[510];
        sector = s[511];
        if (track == 0 && sector == 0 && raw.size() < want)
        {
            error = file.name + ": the chain ends after " + std::to_string(k + 1) + " of " +
                    std::to_string(file.sectors) + " sectors";
            break;
        }
    }
    if (raw.size() > skip)
        data.assign(raw.begin() + static_cast<std::ptrdiff_t>(skip),
                    raw.begin() + static_cast<std::ptrdiff_t>(std::min(want, raw.size())));
    return error.empty() && data.size() == file.length;
}

bool GdosFileSystem::WriteSector(uint8_t track, uint8_t sector, const uint8_t* bytes)
{
    if (!Sector(track, sector))
        return false;
    DiskImage::Track* t = _image.getTrackForCylinderAndSide(track & 0x7F, track >> 7);
    t->writeSectorData(static_cast<uint8_t>(sector - 1), bytes, kSector);
    return true;
}

bool GdosFileSystem::PatchEntry(uint32_t slot, const std::function<void(uint8_t*)>& patch, std::string& error)
{
    const uint8_t cylinder = static_cast<uint8_t>(slot / (kSectorsPerTrack * 2));
    const uint8_t sector = static_cast<uint8_t>(slot % (kSectorsPerTrack * 2) / 2 + 1);
    const uint8_t* s = Sector(cylinder, sector);
    if (!s)
    {
        error = "directory sector of entry " + std::to_string(slot) + " does not read";
        return false;
    }
    uint8_t bytes[kSector];
    std::memcpy(bytes, s, kSector);
    patch(bytes + (slot % 2) * 256);
    if (!WriteSector(cylinder, sector, bytes))
    {
        error = "directory sector of entry " + std::to_string(slot) + " does not write";
        return false;
    }
    return true;
}

bool GdosFileSystem::ValidName(const std::string& name, std::string& error)
{
    bool printable = !name.empty() && name.size() <= 10 && name.front() != ' ';
    for (const char c : name)
        printable = printable && static_cast<uint8_t>(c) >= 0x20 && static_cast<uint8_t>(c) < 0x80;
    if (!printable)
        error = "a G+DOS file name is 1-10 printable ASCII characters, not starting with a blank (" + name + ")";
    return printable;
}

bool GdosFileSystem::Write(const std::string& name, uint8_t tapeType, const std::vector<uint8_t>& data,
                           uint16_t start, uint16_t param, uint16_t autostart, std::string& error)
{
    if (!ValidName(name, error))
        return false;
    if (tapeType > 3)
    {
        error = "a G+DOS file is a program, a number array, a character array or CODE";
        return false;
    }
    if (data.size() > 0xFFFF)
    {
        error = name + ": " + std::to_string(data.size()) + " bytes, a G+DOS file holds at most 65535";
        return false;
    }
    std::vector<GdosFile> files;
    if (!List(files, error))
        return false;
    const GdosFile* older = nullptr;
    for (const GdosFile& f : files)
        if (f.name == name)
            older = &f;
    if (older && older->protect)
    {
        error = name + " is there and protected";
        return false;
    }

    // The used sectors and entries of every other file
    std::vector<bool> used(kMapBits, false);
    std::vector<bool> slotUsed(kEntries, false);
    for (const GdosFile& f : files)
    {
        if (&f == older)
            continue;
        slotUsed[f.slot] = true;
        const uint8_t* e = nullptr;
        if (!Entry(f.slot, e))
        {
            error = "directory sector of entry " + std::to_string(f.slot) + " does not read";
            return false;
        }
        for (uint32_t bit = 0; bit < kMapBits; bit++)
            if (e[kMapOffset + bit / 8] & (1u << (bit % 8)))
                used[bit] = true;
    }
    const uint32_t slot = older ? older->slot : static_cast<uint32_t>(
        std::find(slotUsed.begin(), slotUsed.end(), false) - slotUsed.begin());
    if (slot >= kEntries)
    {
        error = "the G+DOS directory is full (80 files)";
        return false;
    }

    // The stream: the tape header, then the data
    const uint16_t length = static_cast<uint16_t>(data.size());
    const uint8_t header[kTapeHeader] = {tapeType,
                                         static_cast<uint8_t>(length),
                                         static_cast<uint8_t>(length >> 8),
                                         static_cast<uint8_t>(start),
                                         static_cast<uint8_t>(start >> 8),
                                         static_cast<uint8_t>(param),
                                         static_cast<uint8_t>(param >> 8),
                                         static_cast<uint8_t>(autostart),
                                         static_cast<uint8_t>(autostart >> 8)};
    std::vector<uint8_t> stream(header, header + kTapeHeader);
    stream.insert(stream.end(), data.begin(), data.end());
    const uint32_t needed = static_cast<uint32_t>((stream.size() + kDataPerSector - 1) / kDataPerSector);

    std::vector<std::pair<uint8_t, uint8_t>> chain;
    for (uint32_t logical = kDirectoryCylinders; logical < 160 && chain.size() < needed; logical++)
    {
        const uint8_t track = static_cast<uint8_t>(logical < 80 ? logical : (logical - 80) | 0x80);
        for (uint8_t sector = 1; sector <= kSectorsPerTrack && chain.size() < needed; sector++)
            if (!used[MapBit(track, sector)] && Sector(track, sector))
                chain.emplace_back(track, sector);
    }
    if (chain.size() < needed)
    {
        error = name + ": " + std::to_string(needed) + " sectors needed, " + std::to_string(chain.size()) + " free";
        return false;
    }

    for (size_t k = 0; k < chain.size(); k++)
    {
        uint8_t bytes[kSector] = {};
        const size_t from = k * kDataPerSector;
        const size_t count = std::min<size_t>(kDataPerSector, stream.size() - from);
        std::memcpy(bytes, stream.data() + from, count);
        if (k + 1 < chain.size())
        {
            bytes[510] = chain[k + 1].first;
            bytes[511] = chain[k + 1].second;
        }
        if (!WriteSector(chain[k].first, chain[k].second, bytes))
        {
            error = name + ": track " + std::to_string(chain[k].first) + " sector " +
                    std::to_string(chain[k].second) + " does not write";
            return false;
        }
    }
    return PatchEntry(
        slot,
        [&](uint8_t* e) {
            std::memset(e, 0, 256);
            e[0] = static_cast<uint8_t>(tapeType + 1);
            std::memset(e + 1, ' ', 10);
            std::memcpy(e + 1, name.data(), name.size());
            e[11] = static_cast<uint8_t>(needed >> 8);
            e[12] = static_cast<uint8_t>(needed);
            e[13] = chain[0].first;
            e[14] = chain[0].second;
            for (const auto& [track, sector] : chain)
            {
                const uint32_t bit = MapBit(track, sector);
                e[kMapOffset + bit / 8] = static_cast<uint8_t>(e[kMapOffset + bit / 8] | 1u << (bit % 8));
            }
            std::memcpy(e + kHeaderOffset, header, kTapeHeader);
        },
        error);
}

bool GdosFileSystem::Delete(const GdosFile& file, std::string& error)
{
    if (file.protect)
    {
        error = file.name + " is protected";
        return false;
    }
    return PatchEntry(file.slot, [](uint8_t* e) { e[0] = 0; }, error);
}

bool GdosFileSystem::Rename(const GdosFile& file, const std::string& name, std::string& error)
{
    if (!ValidName(name, error))
        return false;
    std::vector<GdosFile> files;
    if (!List(files, error))
        return false;
    for (const GdosFile& f : files)
        if (f.slot != file.slot && f.name == name)
        {
            error = name + " is there already";
            return false;
        }
    return PatchEntry(
        file.slot,
        [&](uint8_t* e) {
            std::memset(e + 1, ' ', 10);
            std::memcpy(e + 1, name.data(), name.size());
        },
        error);
}

bool GdosFileSystem::SetAttributes(const GdosFile& file, bool hidden, bool protect, std::string& error)
{
    return PatchEntry(
        file.slot,
        [&](uint8_t* e) {
            e[0] = static_cast<uint8_t>((e[0] & 0x3F) | (hidden ? 0x40 : 0) | (protect ? 0x80 : 0));
        },
        error);
}

uint32_t GdosFileSystem::FreeSectors() const
{
    std::vector<GdosFile> files;
    std::string error;
    if (!List(files, error))
        return 0;
    const uint32_t total = static_cast<uint32_t>(_image.getCylinders()) * _image.getSides() * kSectorsPerTrack;
    uint32_t used = kDirectoryCylinders * kSectorsPerTrack;
    for (const GdosFile& f : files)
        used += f.sectors;
    return total > used ? total - used : 0;
}

uint64_t GdosFileSystem::Bytes() const
{
    return static_cast<uint64_t>(_image.getCylinders()) * _image.getSides() * kSectorsPerTrack * kSector;
}
}  // namespace unrealdisk
