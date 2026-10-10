#include "diskfiles.h"

#include <algorithm>
#include <cstring>
#include <functional>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/trdoscatalog.h"

namespace
{
constexpr size_t kSector = TrdosCatalog::SECTOR_SIZE;
constexpr size_t kSectorsPerTrack = TrdosCatalog::SECTORS_PER_TRACK;
constexpr size_t kInfoDeletedCount = 0xF4;   // the disk info sector's count of deleted files

/// Runs `work` where nothing else touches the disk: paused, stopped or between frames
bool Coherent(EmulatorContext* context, const std::function<void()>& work, std::string& error)
{
    if (context && context->pEmulator)
    {
        if (context->pEmulator->RunAtCoherentMoment(work, 2000) == Emulator::CoherentMoment::Busy)
        {
            error = "no coherent moment to reach the disk within 2 s (another client is stepping the machine)";
            return false;
        }
        return true;
    }
    work();
    return true;
}

bool Drive(EmulatorContext* context, uint8_t drive, FDD*& fdd, DiskImage*& image, std::string& error)
{
    const char letter = static_cast<char>('A' + drive);
    if (!context || drive > 3 || !context->coreState.diskDrives[drive])
    {
        error = std::string("no drive ") + letter;
        return false;
    }
    fdd = context->coreState.diskDrives[drive];
    image = fdd->getDiskImage();
    if (!image)
    {
        error = std::string("no disk in drive ") + letter;
        return false;
    }
    if (!TrdosCatalog::IsTrdos(*image))
    {
        error = std::string("the disk in drive ") + letter + " has no TR-DOS file system";
        return false;
    }
    return true;
}

/// The image as a .trd file: every logical track's sectors 1-16 in order (a missing sector reads as zeros)
std::vector<uint8_t> TrdBytes(DiskImage& image)
{
    std::vector<uint8_t> out;
    const size_t tracks = static_cast<size_t>(image.getCylinders()) * image.getSides();
    out.reserve(tracks * kSectorsPerTrack * kSector);
    for (size_t t = 0; t < tracks && t <= 0xFF; ++t)
    {
        DiskImage::Track* track = image.getTrack(static_cast<uint8_t>(t));
        for (size_t s = 0; s < kSectorsPerTrack; ++s)
        {
            const uint8_t* data = track ? track->getDataForSector(static_cast<uint8_t>(s)) : nullptr;
            if (data)
                out.insert(out.end(), data, data + kSector);
            else
                out.insert(out.end(), kSector, 0);
        }
    }
    return out;
}
}  // namespace

bool ParseDiskFileRef(const std::string& text, DiskFileRef& out)
{
    if (text.size() < 8 || text.compare(0, 5, "disk:") != 0 || text[6] != '/')
        return false;
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(text[5])));
    if (letter < 'A' || letter > 'D')
        return false;
    std::string file = text.substr(7);
    const size_t dot = file.find_last_of('.');
    DiskFileRef ref;
    ref.drive = static_cast<uint8_t>(letter - 'A');
    if (dot != std::string::npos && dot + 2 == file.size())
    {
        ref.type = file[dot + 1];
        file.resize(dot);
    }
    while (!file.empty() && file.back() == ' ')
        file.pop_back();
    if (file.empty() || file.size() > 8)
        return false;
    ref.name = file;
    out = ref;
    return true;
}

bool IsDiskFileRef(const std::string& text)
{
    DiskFileRef ref;
    return ParseDiskFileRef(text, ref);
}

bool ReadDiskFiles(EmulatorContext* context, uint8_t drive, std::vector<unrealasm::containers::TrdosFile>& out, std::string& error)
{
    std::vector<uint8_t> trd;
    bool ok = false;
    if (!Coherent(context, [&]() {
            FDD* fdd = nullptr;
            DiskImage* image = nullptr;
            ok = Drive(context, drive, fdd, image, error);
            if (ok)
                trd = TrdBytes(*image);
        }, error) || !ok)
        return false;
    out.clear();
    return unrealasm::containers::ReadTrd(trd, out, error);
}

bool ReadDiskFile(EmulatorContext* context, const DiskFileRef& ref, unrealasm::containers::TrdosFile& out, std::string& error)
{
    std::vector<unrealasm::containers::TrdosFile> files;
    if (!ReadDiskFiles(context, ref.drive, files, error))
        return false;
    for (auto it = files.rbegin(); it != files.rend(); ++it)
        if (it->TrimmedName() == ref.name && (ref.type == 0 || it->type == ref.type))
        {
            out = *it;
            return true;
        }
    error = "no file " + ref.name + (ref.type ? std::string(".") + ref.type : std::string()) + " on the disk in drive " +
            static_cast<char>('A' + ref.drive);
    return false;
}

bool WriteDiskFile(EmulatorContext* context, const DiskFileRef& ref, uint16_t start, const std::vector<uint8_t>& bytes, std::string& error)
{
    if (ref.type == 0)
    {
        error = "a file written to a disk needs its type letter (disk:A/NAME.T)";
        return false;
    }
    if (bytes.size() > 0xFFFF)
    {
        error = "a TR-DOS file holds at most 65535 bytes";
        return false;
    }
    bool ok = false;
    const bool reached = Coherent(context, [&]() {
        FDD* fdd = nullptr;
        DiskImage* image = nullptr;
        if (!Drive(context, ref.drive, fdd, image, error))
            return;
        if (fdd->isWriteProtect())
        {
            error = std::string("the disk in drive ") + static_cast<char>('A' + ref.drive) + " is write-protected";
            return;
        }
        TrdosCatalog catalog;
        if (!catalog.Parse(*image))
        {
            error = "the TR-DOS catalog cannot be read";
            return;
        }
        const size_t sectors = (bytes.size() + kSector - 1) / kSector;
        if (catalog.FreeSectors() < sectors)
        {
            error = "the disk has " + std::to_string(catalog.FreeSectors()) + " free sectors, the file needs " + std::to_string(sectors);
            return;
        }
        if (catalog.UsedSlots() >= TrdosCatalog::MAX_FILES)
        {
            error = "the catalog is full (128 entries)";
            return;
        }
        DiskImage::Track* track0 = image->getTrack(0);
        uint8_t info[kSector];
        std::memcpy(info, track0->getDataForSector(8), kSector);
        const size_t first = info[TrdosCatalog::INFO_FIRST_FREE_SECTOR] + kSectorsPerTrack * info[TrdosCatalog::INFO_FIRST_FREE_TRACK];
        for (size_t i = 0; i < sectors; ++i)
        {
            DiskImage::Track* track = image->getTrack(static_cast<uint8_t>((first + i) / kSectorsPerTrack));
            if (!track || !track->getDataForSector(static_cast<uint8_t>((first + i) % kSectorsPerTrack)))
            {
                error = "the disk's free space runs past its last track";
                return;
            }
        }
        // An older live file of this name and type: deleted as TR-DOS deletes (the first name byte #01)
        std::string padded = ref.name;
        padded.resize(8, ' ');
        for (const TrdosFile& f : catalog.Files())
            if (!f.deleted && f.name == padded && f.type == ref.type)
            {
                uint8_t dir[kSector];
                std::memcpy(dir, track0->getDataForSector(static_cast<uint8_t>(f.slot / 16)), kSector);
                dir[(f.slot % 16) * 16] = 0x01;
                track0->writeSectorData(static_cast<uint8_t>(f.slot / 16), dir, kSector);
                ++info[kInfoDeletedCount];
            }
        // The data, the catalog entry, the disk info
        std::vector<uint8_t> padded256(bytes);
        padded256.resize(sectors * kSector, 0);
        for (size_t i = 0; i < sectors; ++i)
            image->getTrack(static_cast<uint8_t>((first + i) / kSectorsPerTrack))
                ->writeSectorData(static_cast<uint8_t>((first + i) % kSectorsPerTrack), padded256.data() + i * kSector, kSector);
        const size_t slot = catalog.UsedSlots();
        uint8_t dir[kSector];
        std::memcpy(dir, track0->getDataForSector(static_cast<uint8_t>(slot / 16)), kSector);
        uint8_t* entry = dir + (slot % 16) * 16;
        std::memcpy(entry, padded.data(), 8);
        entry[8] = static_cast<uint8_t>(ref.type);
        entry[9] = static_cast<uint8_t>(start & 0xFF);
        entry[10] = static_cast<uint8_t>(start >> 8);
        entry[11] = static_cast<uint8_t>(bytes.size() & 0xFF);
        entry[12] = static_cast<uint8_t>(bytes.size() >> 8);
        entry[13] = static_cast<uint8_t>(sectors);
        entry[14] = info[TrdosCatalog::INFO_FIRST_FREE_SECTOR];
        entry[15] = info[TrdosCatalog::INFO_FIRST_FREE_TRACK];
        track0->writeSectorData(static_cast<uint8_t>(slot / 16), dir, kSector);
        const size_t next = first + sectors;
        info[TrdosCatalog::INFO_FIRST_FREE_SECTOR] = static_cast<uint8_t>(next % kSectorsPerTrack);
        info[TrdosCatalog::INFO_FIRST_FREE_TRACK] = static_cast<uint8_t>(next / kSectorsPerTrack);
        info[TrdosCatalog::INFO_FILE_COUNT] = static_cast<uint8_t>(info[TrdosCatalog::INFO_FILE_COUNT] + 1);
        const uint16_t free = static_cast<uint16_t>(catalog.FreeSectors() - sectors);
        info[TrdosCatalog::INFO_FREE_SECTORS] = static_cast<uint8_t>(free & 0xFF);
        info[TrdosCatalog::INFO_FREE_SECTORS + 1] = static_cast<uint8_t>(free >> 8);
        track0->writeSectorData(8, info, kSector);
        ok = true;
    }, error);
    return reached && ok;
}
