#include "trdosbootinjector.h"

#include <algorithm>
#include <cstring>

#include "common/filehelper.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/trdoscatalog.h"

namespace
{
constexpr size_t SECTOR = TrdosCatalog::SECTOR_SIZE;
constexpr size_t SECTORS_PER_TRACK = TrdosCatalog::SECTORS_PER_TRACK;
}  // namespace

bool TrdosBootInjector::InjectHobeta(DiskImage& image, const std::vector<uint8_t>& hobeta)
{
    if (hobeta.size() < HOBETA_HEADER_SIZE)
        return false;

    const uint8_t sectors = hobeta[14];
    if (sectors == 0 || hobeta.size() < HOBETA_HEADER_SIZE + static_cast<size_t>(sectors) * SECTOR)
        return false;

    TrdosCatalog catalog;
    if (!catalog.Parse(image))
        return false;

    if (catalog.FindBoot() != nullptr)
        return false;

    if (catalog.UsedSlots() >= TrdosCatalog::MAX_FILES)
        return false;

    if (catalog.FreeSectors() < sectors)
        return false;

    DiskImage::Track* track0 = image.getTrack(0);
    uint8_t infoCopy[SECTOR];
    std::memcpy(infoCopy, track0->getDataForSector(8), SECTOR);

    const size_t firstPos = infoCopy[TrdosCatalog::INFO_FIRST_FREE_SECTOR] +
                            SECTORS_PER_TRACK * infoCopy[TrdosCatalog::INFO_FIRST_FREE_TRACK];

    // Every target track must exist on the image
    for (size_t i = 0; i < sectors; i++)
    {
        const size_t pos = firstPos + i;
        DiskImage::Track* track = image.getTrack(static_cast<uint8_t>(pos / SECTORS_PER_TRACK));
        if (track == nullptr || track->getDataForSector(static_cast<uint8_t>(pos % SECTORS_PER_TRACK)) == nullptr)
            return false;
    }

    // Catalog entry: 13 header bytes, sector count, first sector, first track
    const size_t slot = catalog.UsedSlots();
    uint8_t dirSector[SECTOR];
    std::memcpy(dirSector, track0->getDataForSector(static_cast<uint8_t>(slot / 16)), SECTOR);
    uint8_t* entry = dirSector + (slot % 16) * 16;
    std::memcpy(entry, hobeta.data(), 13);
    entry[13] = sectors;
    entry[14] = infoCopy[TrdosCatalog::INFO_FIRST_FREE_SECTOR];
    entry[15] = infoCopy[TrdosCatalog::INFO_FIRST_FREE_TRACK];
    track0->writeSectorData(static_cast<uint8_t>(slot / 16), dirSector, SECTOR);

    // File data
    for (size_t i = 0; i < sectors; i++)
    {
        const size_t pos = firstPos + i;
        DiskImage::Track* track = image.getTrack(static_cast<uint8_t>(pos / SECTORS_PER_TRACK));
        track->writeSectorData(static_cast<uint8_t>(pos % SECTORS_PER_TRACK),
                               hobeta.data() + HOBETA_HEADER_SIZE + i * SECTOR, SECTOR);
    }

    // Disk info: first free position, file count, free sectors
    const size_t nextPos = firstPos + sectors;
    infoCopy[TrdosCatalog::INFO_FIRST_FREE_SECTOR] = static_cast<uint8_t>(nextPos % SECTORS_PER_TRACK);
    infoCopy[TrdosCatalog::INFO_FIRST_FREE_TRACK] = static_cast<uint8_t>(nextPos / SECTORS_PER_TRACK);
    infoCopy[TrdosCatalog::INFO_FILE_COUNT] = static_cast<uint8_t>(infoCopy[TrdosCatalog::INFO_FILE_COUNT] + 1);
    const uint16_t free = static_cast<uint16_t>(catalog.FreeSectors() - sectors);
    infoCopy[TrdosCatalog::INFO_FREE_SECTORS] = static_cast<uint8_t>(free & 0xFF);
    infoCopy[TrdosCatalog::INFO_FREE_SECTORS + 1] = static_cast<uint8_t>(free >> 8);
    track0->writeSectorData(8, infoCopy, SECTOR);

    // The injected boot is virtual: the image stays "unmodified"
    image.markClean();
    return true;
}

std::vector<uint8_t> TrdosBootInjector::BuildNamedBootHobeta(const std::string& trimmedName)
{
    if (trimmedName.empty() || trimmedName.size() > 8 || trimmedName.find('"') != std::string::npos)
        return {};

    // Line 1: RANDOMIZE USR 15619: REM : RUN "NAME"
    std::vector<uint8_t> line;
    line.push_back(0xF9);                                   // RANDOMIZE
    line.push_back(0xC0);                                   // USR
    for (char c : std::string("15619"))
        line.push_back(static_cast<uint8_t>(c));
    line.insert(line.end(), {0x0E, 0x00, 0x00, 0x03, 0x3D, 0x00});  // hidden number 15619
    line.push_back(':');
    line.push_back(0xEA);                                   // REM
    line.push_back(':');
    line.push_back(0xF7);                                   // RUN
    line.push_back('"');
    for (char c : trimmedName)
        line.push_back(static_cast<uint8_t>(c));
    line.push_back('"');
    line.push_back(0x0D);

    std::vector<uint8_t> program;
    program.push_back(0x00);  // Line number 1 (big endian)
    program.push_back(0x01);
    program.push_back(static_cast<uint8_t>(line.size() & 0xFF));  // Line length (little endian)
    program.push_back(static_cast<uint8_t>(line.size() >> 8));
    program.insert(program.end(), line.begin(), line.end());

    const uint16_t programLength = static_cast<uint16_t>(program.size());

    // Autostart line after the program: 0x80, 0xAA, line low, line high
    program.insert(program.end(), {0x80, 0xAA, 0x01, 0x00});
    program.resize(SECTOR, 0x00);

    std::vector<uint8_t> hobeta(HOBETA_HEADER_SIZE, 0);
    std::memcpy(hobeta.data(), "boot    ", 8);
    hobeta[8] = 'B';
    hobeta[9] = static_cast<uint8_t>(programLength & 0xFF);
    hobeta[10] = static_cast<uint8_t>(programLength >> 8);
    hobeta[11] = hobeta[9];
    hobeta[12] = hobeta[10];
    hobeta[13] = 0;
    hobeta[14] = 1;  // Sectors
    hobeta.insert(hobeta.end(), program.begin(), program.end());
    return hobeta;
}

bool TrdosBootInjector::InjectNamedBoot(DiskImage& image, const std::string& trimmedName)
{
    std::vector<uint8_t> hobeta = BuildNamedBootHobeta(trimmedName);
    return !hobeta.empty() && InjectHobeta(image, hobeta);
}

std::vector<uint8_t> TrdosBootInjector::LoadBundledCommander()
{
    const std::string relative = "boot/boot.$b";
    const std::string candidates[] = {
        FileHelper::PathCombine(FileHelper::GetExecutablePath(), relative),
        FileHelper::PathCombine(FileHelper::GetResourcesPath(), relative),
    };

    for (const std::string& path : candidates)
    {
        if (!FileHelper::FileExists(path))
            continue;

        FILE* file = FileHelper::OpenFile(path, "rb");
        if (file == nullptr)
            continue;

        std::vector<uint8_t> data;
        uint8_t buffer[4096];
        size_t read;
        while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
            data.insert(data.end(), buffer, buffer + read);
        fclose(file);

        if (data.size() >= HOBETA_HEADER_SIZE)
            return data;
    }

    return {};
}
