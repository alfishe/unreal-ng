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
    size_t next = 0;
    return Write(image, device, [&]() -> std::optional<uint64_t> {
        return next < lbas.size() ? std::optional<uint64_t>(lbas[next++]) : std::nullopt; }, error);
}

bool CommitJournal::Write(const std::filesystem::path& image, IBlockDevice& device, const std::function<std::optional<uint64_t>()>& next,
                          std::string* error)
{
    // Streamed: an entry at a time into the file, the count patched in at the end
    const std::filesystem::path path = PathFor(image);
    auto fail = [&](const std::string& why) {
        if (error)
            *error = why;
        return false;
    };
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return fail("cannot write " + FileHelper::FromFsPath(path));
    std::vector<uint8_t> head;
    head.insert(head.end(), kMagic, kMagic + sizeof kMagic);
    Put64(head, device.SectorCount());
    Put64(head, 0);  // the entry count, once known
    file.write(reinterpret_cast<const char*>(head.data()), static_cast<std::streamsize>(head.size()));

    uint64_t count = 0;
    uint64_t hash = 0xcbf29ce484222325ULL;
    std::vector<uint8_t> entry;
    entry.reserve(8 + kSector);
    uint8_t sector[kSector];
    for (std::optional<uint64_t> lba = next(); lba; lba = next())
    {
        // A sector past the end of a cut-down image reads as zeros: put back as zeros
        if (*lba >= device.SectorCount())
            std::memset(sector, 0, sizeof sector);
        else if (!device.ReadSector(*lba, sector))
            return fail("cannot read sector " + std::to_string(*lba) + " of " + FileHelper::FromFsPath(image));
        entry.clear();
        Put64(entry, *lba);
        entry.insert(entry.end(), sector, sector + kSector);
        hash = Fnv(hash, entry.data(), entry.size());
        file.write(reinterpret_cast<const char*>(entry.data()), static_cast<std::streamsize>(entry.size()));
        count++;
    }
    std::vector<uint8_t> tail;
    tail.insert(tail.end(), kEnd, kEnd + sizeof kEnd);
    Put64(tail, hash);
    file.write(reinterpret_cast<const char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
    std::vector<uint8_t> countBytes;
    Put64(countBytes, count);
    file.seekp(static_cast<std::streamoff>(sizeof kMagic + 8));
    file.write(reinterpret_cast<const char*>(countBytes.data()), 8);
    file.flush();
    if (!file)
        return fail("cannot write " + FileHelper::FromFsPath(path));
    file.close();
    if (!Sync(path))
        return fail("cannot sync " + FileHelper::FromFsPath(path));
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

    // Streamed: the header, then two passes over the entries (checked, then applied), an entry at a time
    const uint64_t size = std::filesystem::file_size(path, ec);
    std::ifstream in(path, std::ios::binary);
    const size_t header = sizeof kMagic + 16;
    uint8_t head[sizeof kMagic + 16] = {};
    const bool started = !ec && size >= header && in.read(reinterpret_cast<char*>(head), header) &&
                         std::memcmp(head, kMagic, sizeof kMagic) == 0;
    const uint64_t count = started ? Get64(head + sizeof kMagic + 8) : 0;
    const uint64_t sectors = started ? Get64(head + sizeof kMagic) : 0;
    const uint64_t entrySize = 8 + kSector;
    const uint64_t complete = header + count * entrySize + sizeof kEnd + 8;
    uint8_t tail[sizeof kEnd + 8] = {};
    const bool ended = started && count <= size / entrySize && size == complete &&
                       in.seekg(static_cast<std::streamoff>(complete - sizeof tail)) &&
                       in.read(reinterpret_cast<char*>(tail), sizeof tail) && std::memcmp(tail, kEnd, sizeof kEnd) == 0;
    if (!ended)
    {
        // Never finished: the commit writes the image only after a complete, synced journal
        in.close();
        std::filesystem::remove(path, ec);
        say(name + ": an unfinished commit journal was found and dropped; the image had not been written");
        return Recovery::Dropped;
    }
    std::vector<uint8_t> entry(static_cast<size_t>(entrySize));
    auto readEntry = [&](uint64_t i) {
        in.clear();
        in.seekg(static_cast<std::streamoff>(header + i * entrySize));
        return static_cast<bool>(in.read(reinterpret_cast<char*>(entry.data()), static_cast<std::streamsize>(entrySize)));
    };
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (uint64_t i = 0; i < count; i++)
    {
        if (!readEntry(i))
            break;
        hash = Fnv(hash, entry.data(), entry.size());
    }
    if (Get64(tail + sizeof kEnd) != hash)
    {
        in.close();
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
        if (!readEntry(i))
        {
            say(name + ": an interrupted commit could not be rolled back (the journal cannot be read); the journal is kept");
            return Recovery::Damaged;
        }
        const uint64_t lba = Get64(entry.data());
        if (lba < device->SectorCount() && !device->WriteSector(lba, entry.data() + 8))
        {
            say(name + ": an interrupted commit could not be rolled back (sector " + std::to_string(lba) + "); the journal is kept");
            return Recovery::Damaged;
        }
    }
    device.reset();
    in.close();
    // A cut-down image the commit extended goes back to its size
    if (format == "raw" && std::filesystem::file_size(image, ec) > sectors * kSector)
        std::filesystem::resize_file(image, sectors * kSector, ec);
    Sync(image);
    std::filesystem::remove(path, ec);
    say(name + ": an interrupted commit was rolled back (" + std::to_string(count) + " sectors restored)");
    return Recovery::RolledBack;
}
