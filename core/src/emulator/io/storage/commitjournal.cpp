#include "stdafx.h"

#include "commitjournal.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "common/filehelper.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/io/storage/iblockdevice.h"

namespace
{
    constexpr char kMagic[8] = {'U', 'N', 'G', 'J', 'R', 'N', 'L', '1'};
    constexpr char kEnd[8] = {'U', 'N', 'G', 'J', 'E', 'N', 'D', '!'};
    constexpr size_t kSector = 512;

    void Put64(std::vector<uint8_t>& out, uint64_t v)
    {
        for (int i = 0; i < 8; i++)
            out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }

    uint64_t Get64(const uint8_t* p)
    {
        uint64_t v = 0;
        for (int i = 0; i < 8; i++)
            v |= static_cast<uint64_t>(p[i]) << (8 * i);
        return v;
    }

    uint64_t Fnv(uint64_t h, const uint8_t* data, size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            h ^= data[i];
            h *= 0x100000001b3ULL;
        }
        return h;
    }
}  // namespace

std::filesystem::path CommitJournal::PathFor(const std::filesystem::path& image)
{
    std::filesystem::path journal = image;
    journal += ".ujournal";
    return journal;
}

bool CommitJournal::Sync(const std::filesystem::path& file)
{
#ifdef _WIN32
    FILE* f = _wfopen(file.c_str(), L"r+b");
#else
    FILE* f = std::fopen(file.c_str(), "r+b");
#endif
    if (!f)
        return false;
    bool ok = std::fflush(f) == 0;
#ifdef _WIN32
    ok = _commit(_fileno(f)) == 0 && ok;
#else
    ok = fsync(fileno(f)) == 0 && ok;
#endif
    std::fclose(f);
    return ok;
}

bool CommitJournal::Write(const std::filesystem::path& image, IBlockDevice& device, const std::vector<uint64_t>& lbas,
                          std::string* error)
{
    std::vector<uint8_t> out;
    out.insert(out.end(), kMagic, kMagic + sizeof kMagic);
    Put64(out, device.SectorCount());
    Put64(out, lbas.size());
    const size_t entries = out.size();
    uint8_t sector[kSector];
    for (uint64_t lba : lbas)
    {
        // A sector past the end of a cut-down image reads as zeros: put back as zeros
        if (lba >= device.SectorCount())
            std::memset(sector, 0, sizeof sector);
        else if (!device.ReadSector(lba, sector))
        {
            if (error)
                *error = "cannot read sector " + std::to_string(lba) + " of " + FileHelper::FromFsPath(image);
            return false;
        }
        Put64(out, lba);
        out.insert(out.end(), sector, sector + kSector);
    }
    const uint64_t hash = Fnv(0xcbf29ce484222325ULL, out.data() + entries, out.size() - entries);
    out.insert(out.end(), kEnd, kEnd + sizeof kEnd);
    Put64(out, hash);

    const std::filesystem::path path = PathFor(image);
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        file.flush();
        if (!file)
        {
            if (error)
                *error = "cannot write " + FileHelper::FromFsPath(path);
            return false;
        }
    }
    if (!Sync(path))
    {
        if (error)
            *error = "cannot sync " + FileHelper::FromFsPath(path);
        return false;
    }
    return true;
}

void CommitJournal::Remove(const std::filesystem::path& image)
{
    std::error_code ec;
    std::filesystem::remove(PathFor(image), ec);
}

CommitJournal::Recovery CommitJournal::Recover(const std::filesystem::path& image, std::string* detail)
{
    const std::filesystem::path path = PathFor(image);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return Recovery::None;
    const std::string name = FileHelper::FromFsPath(image.filename());
    auto say = [detail](const std::string& text) {
        if (detail)
            *detail = text;
    };

    std::vector<uint8_t> data(static_cast<size_t>(std::filesystem::file_size(path, ec)));
    {
        std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    const size_t header = sizeof kMagic + 16;
    const bool started = data.size() >= header && std::memcmp(data.data(), kMagic, sizeof kMagic) == 0;
    const uint64_t count = started ? Get64(data.data() + sizeof kMagic + 8) : 0;
    const size_t entrySize = 8 + kSector;
    const size_t complete = header + static_cast<size_t>(count) * entrySize + sizeof kEnd + 8;
    if (!started || count > (data.size() / entrySize) || data.size() != complete ||
        std::memcmp(data.data() + complete - 16, kEnd, sizeof kEnd) != 0)
    {
        // Never finished: the commit writes the image only after a complete, synced journal
        std::filesystem::remove(path, ec);
        say(name + ": an unfinished commit journal was found and dropped; the image had not been written");
        return Recovery::Dropped;
    }
    if (Get64(data.data() + complete - 8) != Fnv(0xcbf29ce484222325ULL, data.data() + header, static_cast<size_t>(count) * entrySize))
    {
        std::filesystem::path bad = path;
        bad += ".bad";
        std::filesystem::rename(path, bad, ec);
        say(name + ": its commit journal does not check out (kept as " + FileHelper::FromFsPath(bad.filename()) +
            "); the image may hold a half-written commit");
        return Recovery::Damaged;
    }

    std::string error;
    const std::string utf8 = FileHelper::FromFsPath(image);
    const std::string format = HddImageFormats::Probe(utf8, &error);
    auto device = format.empty() || format == "chd" ? nullptr : HddImageFormats::OpenBlock(utf8, format, RawImage::Access::ReadWrite, &error);
    if (!device)
    {
        say(name + ": an interrupted commit could not be rolled back (" + error + "); the journal is kept");
        return Recovery::Damaged;
    }
    for (uint64_t i = 0; i < count; i++)
    {
        const uint8_t* entry = data.data() + header + i * entrySize;
        const uint64_t lba = Get64(entry);
        if (lba < device->SectorCount() && !device->WriteSector(lba, entry + 8))
        {
            say(name + ": an interrupted commit could not be rolled back (sector " + std::to_string(lba) + "); the journal is kept");
            return Recovery::Damaged;
        }
    }
    device.reset();
    // A cut-down image the commit extended goes back to its size
    const uint64_t sectors = Get64(data.data() + sizeof kMagic);
    if (format == "raw" && std::filesystem::file_size(image, ec) > sectors * kSector)
        std::filesystem::resize_file(image, sectors * kSector, ec);
    Sync(image);
    std::filesystem::remove(path, ec);
    say(name + ": an interrupted commit was rolled back (" + std::to_string(count) + " sectors restored)");
    return Recovery::RolledBack;
}
