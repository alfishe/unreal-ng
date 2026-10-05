#include "sectorwrite.h"

#include <algorithm>
#include <cctype>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"

namespace SectorWrite
{
namespace
{
/// The sector's data field, or null with the reason (checked before and again at the coherent moment)
DiskImage::Sector* Find(EmulatorContext* context, uint8_t drive, int cylinder, int side, int sector, FDD*& fdd,
                        DiskImage::Track*& track, std::string& error)
{
    fdd = drive < 4 ? context->coreState.diskDrives[drive] : nullptr;
    const std::string name(1, static_cast<char>('A' + drive));
    if (!fdd || !fdd->isDiskInserted() || !fdd->getDiskImage())
    {
        error = "no disk in drive " + name;
        return nullptr;
    }
    if (fdd->isWriteProtect())
    {
        error = "the disk in drive " + name + " is write-protected";
        return nullptr;
    }
    if (cylinder < 0 || side < 0 || side > 1 || sector < 1 || sector > 255)
    {
        error = "bad cylinder / side / sector (cylinder >= 0, side 0 or 1, sector ID 1-255)";
        return nullptr;
    }
    track = fdd->getDiskImage()->getTrackForCylinderAndSide(static_cast<uint8_t>(cylinder), static_cast<uint8_t>(side));
    if (!track)
    {
        error = "no track at cylinder " + std::to_string(cylinder) + ", side " + std::to_string(side);
        return nullptr;
    }
    DiskImage::Sector* found = track->findSector(static_cast<uint8_t>(sector));
    if (!found)
    {
        error = "no sector with ID " + std::to_string(sector) + " on that track";
        return nullptr;
    }
    if (!found->hasData || !found->data)
    {
        error = "sector " + std::to_string(sector) + " has no data field (ID only)";
        return nullptr;
    }
    return found;
}
}  // namespace

Result Write(Emulator* emulator, uint8_t drive, int cylinder, int side, int sector, uint32_t offset,
             const std::vector<uint8_t>& bytes, const char* source)
{
    Result result;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context)
    {
        result.error = "emulator not available";
        return result;
    }
    if (bytes.empty())
    {
        result.error = "no bytes to write";
        return result;
    }

    const Emulator::CoherentMoment where = emulator->RunAtCoherentMoment(
        [&]() {
            FDD* fdd = nullptr;
            DiskImage::Track* track = nullptr;
            DiskImage::Sector* found = Find(context, drive, cylinder, side, sector, fdd, track, result.error);
            if (!found)
                return;
            result.sectorSize = found->dataSize;
            if (offset >= found->dataSize || bytes.size() > found->dataSize - offset)
            {
                result.error = "offset " + std::to_string(offset) + " + " + std::to_string(bytes.size()) +
                               " bytes is past the " + std::to_string(found->dataSize) + "-byte data field";
                return;
            }
            std::vector<uint8_t> data(found->data, found->data + found->dataSize);
            std::copy(bytes.begin(), bytes.end(), data.begin() + offset);
            emulator->EditMemoryFromTool(source, [&]() {
                track->writeSectorData(static_cast<uint8_t>(sector - 1), data.data(), data.size());
            });
            result.ok = true;
        },
        500);
    if (where == Emulator::CoherentMoment::Busy)
    {
        result.busy = true;
        result.error = "no coherent moment within 500 ms (the emulator is stepping or changing state); try again";
        return result;
    }
    if (result.ok)
        result.moment = Emulator::CoherentMomentName(where);
    return result;
}

bool ParseDrive(const std::string& text, uint8_t& drive, std::string& error)
{
    if (text.size() == 1)
    {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        if (c >= 'A' && c <= 'D')
        {
            drive = static_cast<uint8_t>(c - 'A');
            return true;
        }
        if (c >= '0' && c <= '3')
        {
            drive = static_cast<uint8_t>(c - '0');
            return true;
        }
    }
    error = "bad drive '" + text + "' (A-D or 0-3)";
    return false;
}

bool ParseHex(const std::string& text, std::vector<uint8_t>& bytes, std::string& error)
{
    bytes.clear();
    int high = -1;
    for (char c : text)
    {
        if (c == ' ' || c == ',' || c == '\t')
            continue;
        if (!std::isxdigit(static_cast<unsigned char>(c)))
        {
            error = std::string("not a hex digit: '") + c + "'";
            return false;
        }
        const int digit = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::toupper(c) - 'A' + 10);
        if (high < 0)
            high = digit;
        else
        {
            bytes.push_back(static_cast<uint8_t>((high << 4) | digit));
            high = -1;
        }
    }
    if (high >= 0)
    {
        error = "odd number of hex digits";
        return false;
    }
    if (bytes.empty())
    {
        error = "no hex bytes";
        return false;
    }
    return true;
}
}  // namespace SectorWrite
