#include "unrealdisk/gdosfs.h"

#include <algorithm>

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
