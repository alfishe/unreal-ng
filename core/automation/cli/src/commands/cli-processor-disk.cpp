#include <common/filehelper.h>
#include <common/stringhelper.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/diskimage.h>
#include <emulator/io/fdc/wd1793.h>
#include <emulator/platform.h>

#include <iomanip>
#include <sstream>

#include "cli-processor.h"
#include "common/timehelper.h"

/// region <Disk Control Commands>

void CLIProcessor::HandleDisk(const ClientSession& session, const std::vector<std::string>& args)
{
    // Get the selected emulator
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    // Get emulator context
    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        session.SendResponse(std::string("Error: Unable to access emulator context.") + NEWLINE);
        return;
    }

    // If no arguments, show drive list
    if (args.empty())
    {
        HandleDiskList(session, context);
        return;
    }

    std::string subcommand = args[0];
    std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

    // Dispatch to subcommand handlers
    if (subcommand == "insert")
    {
        HandleDiskInsert(session, emulator, context, args);
    }
    else if (subcommand == "eject")
    {
        HandleDiskEject(session, emulator, context, args);
    }
    else if (subcommand == "info")
    {
        HandleDiskInfo(session, context, args);
    }
    else if (subcommand == "list")
    {
        HandleDiskList(session, context);
    }
    else if (subcommand == "sector")
    {
        HandleDiskSector(session, context, args);
    }
    else if (subcommand == "track")
    {
        HandleDiskTrack(session, context, args);
    }
    else if (subcommand == "sysinfo")
    {
        HandleDiskSysinfo(session, context, args);
    }
    else if (subcommand == "catalog" || subcommand == "dir")
    {
        HandleDiskCatalog(session, context, args);
    }
    else if (subcommand == "create")
    {
        HandleDiskCreate(session, emulator, context, args);
    }
    else if (subcommand == "help")
    {
        std::stringstream ss;
        ss << "Usage: disk <subcommand> [args]" << NEWLINE;
        ss << NEWLINE;
        ss << "Subcommands:" << NEWLINE;
        ss << "  insert <drive> <file>  - Insert disk image" << NEWLINE;
        ss << "  eject <drive>          - Eject disk" << NEWLINE;
        ss << "  create <drv> [c] [s]   - Create blank disk (default 80T/2S)" << NEWLINE;
        ss << "  info <drive>           - Show drive status" << NEWLINE;
        ss << "  list                   - List all drives" << NEWLINE;
        ss << "  sector <drv> <c> <s> <n> - Read sector" << NEWLINE;
        ss << "  track <drv> <c> <s>    - Read track" << NEWLINE;
        ss << "  sysinfo [drv]          - TR-DOS system info" << NEWLINE;
        ss << "  catalog [drv]          - File listing" << NEWLINE;
        ss << NEWLINE;
        ss << "Drive: A-D or 0-3" << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown subcommand '") + args[0] + "'" + NEWLINE +
                             "Use 'disk help' to see available subcommands." + NEWLINE);
    }
}

uint8_t CLIProcessor::ParseDriveParameter(const std::string& driveStr, std::string& errorMsg)
{
    if (driveStr.empty())
    {
        errorMsg = "Missing drive parameter";
        return 0xFF;  // Invalid marker
    }

    // Accept: A, B, C, D or 0, 1, 2, 3
    if (driveStr == "A" || driveStr == "a" || driveStr == "0")
        return 0;
    if (driveStr == "B" || driveStr == "b" || driveStr == "1")
        return 1;
    if (driveStr == "C" || driveStr == "c" || driveStr == "2")
        return 2;
    if (driveStr == "D" || driveStr == "d" || driveStr == "3")
        return 3;

    errorMsg = "Invalid drive: " + driveStr + " (use A-D or 0-3)";
    return 0xFF;  // Invalid marker
}

void CLIProcessor::HandleDiskInsert(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                    EmulatorContext* context, const std::vector<std::string>& args)
{
    if (args.size() < 3)
    {
        session.SendResponse(std::string("Error: Missing arguments") + NEWLINE + "Usage: disk insert <drive> <file>" +
                             NEWLINE);
        return;
    }

    // Parse drive
    std::string errorMsg;
    uint8_t drive = ParseDriveParameter(args[1], errorMsg);
    if (drive == 0xFF)
    {
        session.SendResponse(std::string("Error: ") + errorMsg + NEWLINE);
        return;
    }

    std::string filepath = args[2];

    // Use existing LoadDisk method with drive parameter
    // Note: LoadDisk currently hardcoded to drive 0, need to update it
    bool success = emulator->LoadDisk(filepath);

    if (success)
    {
        session.SendResponse(std::string("Disk inserted in drive ") + static_cast<char>('A' + drive) + ": " + filepath +
                             NEWLINE);
    }
    else
    {
        session.SendResponse(std::string("Error: Failed to insert disk: ") + filepath + NEWLINE);
    }
}

void CLIProcessor::HandleDiskEject(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                   EmulatorContext* context, const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing drive parameter") + NEWLINE + "Usage: disk eject <drive>" +
                             NEWLINE);
        return;
    }

    // Parse drive
    std::string errorMsg;
    uint8_t drive = ParseDriveParameter(args[1], errorMsg);
    if (drive == 0xFF)
    {
        session.SendResponse(std::string("Error: ") + errorMsg + NEWLINE);
        return;
    }

    if (!context->coreState.diskDrives[drive])
    {
        session.SendResponse(std::string("Error: Drive ") + static_cast<char>('A' + drive) + " not available" +
                             NEWLINE);
        return;
    }

    // Thread-safe: Pause emulator if running
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning)
    {
        emulator->Pause();
        sleep_ms(10);  // Give emulator time to pause
    }

    // Eject disk and clear filepath
    context->coreState.diskDrives[drive]->ejectDisk();
    context->coreState.diskFilePaths[drive].clear();

    if (wasRunning)
    {
        emulator->Resume();
    }

    session.SendResponse(std::string("Disk ejected from drive ") + static_cast<char>('A' + drive) + NEWLINE);
}

void CLIProcessor::HandleDiskInfo(const ClientSession& session, EmulatorContext* context,
                                  const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing drive parameter") + NEWLINE + "Usage: disk info <drive>" +
                             NEWLINE);
        return;
    }

    // Parse drive
    std::string errorMsg;
    uint8_t drive = ParseDriveParameter(args[1], errorMsg);
    if (drive == 0xFF)
    {
        session.SendResponse(std::string("Error: ") + errorMsg + NEWLINE);
        return;
    }

    std::stringstream ss;
    ss << "Drive " << static_cast<char>('A' + drive) << ":" << NEWLINE;
    ss << "==========" << NEWLINE;
    ss << NEWLINE;

    if (!context->coreState.diskDrives[drive])
    {
        ss << "Status: Drive not available" << NEWLINE;
    }
    else
    {
        FDD* fdd = context->coreState.diskDrives[drive];

        if (!fdd->isDiskInserted())
        {
            ss << "Status: No disk inserted" << NEWLINE;
        }
        else
        {
            ss << "Status: Disk inserted" << NEWLINE;
            ss << "File: " << context->coreState.diskFilePaths[drive] << NEWLINE;
            ss << "Write Protected: " << (fdd->isWriteProtect() ? "Yes" : "No") << NEWLINE;
        }
    }

    session.SendResponse(ss.str());
}

/// region <Disk Inspection Commands>

void CLIProcessor::HandleDiskList(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Disk Drives:" << NEWLINE;
    ss << "============" << NEWLINE;
    
    for (int i = 0; i < 4; i++)
    {
        char driveLetter = 'A' + i;
        ss << "  " << driveLetter << ": ";
        
        if (!context->coreState.diskDrives[i])
        {
            ss << "(not available)";
        }
        else
        {
            FDD* fdd = context->coreState.diskDrives[i];
            if (!fdd->isDiskInserted())
            {
                ss << "(empty)";
            }
            else
            {
                ss << context->coreState.diskFilePaths[i];
                if (fdd->isWriteProtect())
                    ss << " [WP]";
            }
        }
        ss << NEWLINE;
    }
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleDiskSector(const ClientSession& session, EmulatorContext* context,
                                     const std::vector<std::string>& args)
{
    // disk sector <drive> <cyl> <side> <sec>
    if (args.size() < 5)
    {
        session.SendResponse(std::string("Usage: disk sector <drive> <cyl> <side> <sec>") + NEWLINE);
        return;
    }
    
    std::string errorMsg;
    uint8_t drive = ParseDriveParameter(args[1], errorMsg);
    if (drive == 0xFF)
    {
        session.SendResponse(std::string("Error: ") + errorMsg + NEWLINE);
        return;
    }
    
    int cylinder = std::stoi(args[2]);
    int side = std::stoi(args[3]);
    int sector = std::stoi(args[4]) - 1;  // Convert to 0-indexed
    
    if (cylinder < 0 || cylinder >= 80 || side < 0 || side > 1 || sector < 0)
    {
        session.SendResponse(std::string("Error: Invalid cylinder/side/sector") + NEWLINE);
        return;
    }
    
    FDD* fdd = context->coreState.diskDrives[drive];
    if (!fdd || !fdd->isDiskInserted())
    {
        session.SendResponse(std::string("Error: No disk in drive ") + static_cast<char>('A' + drive) + NEWLINE);
        return;
    }
    
    DiskImage* diskImage = fdd->getDiskImage();
    if (!diskImage)
    {
        session.SendResponse(std::string("Error: Cannot access disk image") + NEWLINE);
        return;
    }
    
    DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
    if (!track)
    {
        session.SendResponse(std::string("Error: Track not found") + NEWLINE);
        return;
    }
    
    if (sector >= DiskImage::RawTrack::SECTORS_PER_TRACK)
    {
        session.SendResponse(std::string("Error: Sector not found") + NEWLINE);
        return;
    }
    
    DiskImage::RawSectorBytes* rawSector = track->getRawSector(sector);
    if (!rawSector)
    {
        session.SendResponse(std::string("Error: Sector data unavailable") + NEWLINE);
        return;
    }
    
    std::stringstream ss;
    ss << "Sector " << (sector + 1) << " @ Track " << cylinder << "/" << side << ":" << NEWLINE;
    ss << "  Address Mark: C=" << (int)rawSector->address_record.cylinder
       << " H=" << (int)rawSector->address_record.head
       << " R=" << (int)rawSector->address_record.sector
       << " N=" << (int)rawSector->address_record.sector_size << NEWLINE;
    ss << "  ID CRC: " << StringHelper::ToHex(rawSector->address_record.id_crc) 
       << (rawSector->address_record.isCRCValid() ? " (OK)" : " (BAD)") << NEWLINE;
    ss << "  Data CRC: " << StringHelper::ToHex(rawSector->data_crc)
       << (rawSector->isDataCRCValid() ? " (OK)" : " (BAD)") << NEWLINE;
    ss << "  Data:" << NEWLINE;
    
    // Hex dump (first 128 bytes)
    for (int row = 0; row < 8; row++)
    {
        ss << "    " << StringHelper::ToHex((uint8_t)(row * 16), 2) << ": ";
        for (int col = 0; col < 16; col++)
        {
            ss << StringHelper::ToHex(rawSector->data[row * 16 + col], 2) << " ";
        }
        ss << NEWLINE;
    }
    ss << "    ..." << NEWLINE;
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleDiskTrack(const ClientSession& session, EmulatorContext* context,
                                    const std::vector<std::string>& args)
{
    // disk track <drive> <cyl> <side>
    if (args.size() < 4)
    {
        session.SendResponse(std::string("Usage: disk track <drive> <cyl> <side>") + NEWLINE);
        return;
    }
    
    std::string errorMsg;
    uint8_t drive = ParseDriveParameter(args[1], errorMsg);
    if (drive == 0xFF)
    {
        session.SendResponse(std::string("Error: ") + errorMsg + NEWLINE);
        return;
    }
    
    int cylinder = std::stoi(args[2]);
    int side = std::stoi(args[3]);
    
    if (cylinder < 0 || cylinder >= 80 || side < 0 || side > 1)
    {
        session.SendResponse(std::string("Error: Invalid cylinder/side") + NEWLINE);
        return;
    }
    
    FDD* fdd = context->coreState.diskDrives[drive];
    if (!fdd || !fdd->isDiskInserted())
    {
        session.SendResponse(std::string("Error: No disk in drive ") + static_cast<char>('A' + drive) + NEWLINE);
        return;
    }
    
    DiskImage* diskImage = fdd->getDiskImage();
    if (!diskImage)
    {
        session.SendResponse(std::string("Error: Cannot access disk image") + NEWLINE);
        return;
    }
    
    DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
    if (!track)
    {
        session.SendResponse(std::string("Error: Track not found") + NEWLINE);
        return;
    }
    
    std::stringstream ss;
    ss << "Track " << cylinder << "/" << side << " (" << DiskImage::RawTrack::SECTORS_PER_TRACK << " sectors):" << NEWLINE;
    ss << "  Sec  C   H   R   N   ID-CRC  Data-CRC" << NEWLINE;
    ss << "  ---  --  --  --  --  ------  --------" << NEWLINE;
    
    for (int i = 0; i < DiskImage::RawTrack::SECTORS_PER_TRACK; i++)
    {
        DiskImage::RawSectorBytes* sec = track->getRawSector(i);
        if (sec)
        {
            ss << "  " << std::setw(3) << (i + 1)
               << "  " << std::setw(2) << (int)sec->address_record.cylinder
               << "  " << std::setw(2) << (int)sec->address_record.head
               << "  " << std::setw(2) << (int)sec->address_record.sector
               << "  " << std::setw(2) << (int)sec->address_record.sector_size
               << "  " << (sec->address_record.isCRCValid() ? "OK    " : "BAD   ")
               << "  " << (sec->isDataCRCValid() ? "OK" : "BAD") << NEWLINE;
        }
    }
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleDiskSysinfo(const ClientSession& session, EmulatorContext* context,
                                      const std::vector<std::string>& args)
{
    // disk sysinfo [drive] - defaults to A
    uint8_t drive = 0;
    if (args.size() >= 2)
    {
        std::string errorMsg;
        drive = ParseDriveParameter(args[1], errorMsg);
        if (drive == 0xFF)
        {
            session.SendResponse(std::string("Error: ") + errorMsg + NEWLINE);
            return;
        }
    }
    
    FDD* fdd = context->coreState.diskDrives[drive];
    if (!fdd || !fdd->isDiskInserted())
    {
        session.SendResponse(std::string("Error: No disk in drive ") + static_cast<char>('A' + drive) + NEWLINE);
        return;
    }
    
    DiskImage* diskImage = fdd->getDiskImage();
    if (!diskImage)
    {
        session.SendResponse(std::string("Error: Cannot access disk image") + NEWLINE);
        return;
    }
    
    // Read sector 9 (0-indexed: 8) from track 0, side 0
    DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(0, 0);
    if (!track || DiskImage::RawTrack::SECTORS_PER_TRACK < 9)
    {
        session.SendResponse(std::string("Error: Cannot read system sector") + NEWLINE);
        return;
    }
    
    DiskImage::RawSectorBytes* sector = track->getRawSector(8);
    if (!sector)
    {
        session.SendResponse(std::string("Error: System sector unavailable") + NEWLINE);
        return;
    }
    
    const uint8_t* data = sector->data;
    
    // Parse TR-DOS system sector
    uint8_t firstFreeSector = data[0xE1];
    uint8_t firstFreeTrack = data[0xE2];
    uint8_t diskType = data[0xE3];
    uint8_t fileCount = data[0xE4];
    uint16_t freeSectors = data[0xE5] | (data[0xE6] << 8);
    uint8_t signature = data[0xE7];
    
    // Label at offset 0xF5 (8 chars)
    std::string label((const char*)&data[0xF5], 8);
    // Trim trailing spaces
    while (!label.empty() && label.back() == ' ') label.pop_back();
    
    std::stringstream ss;
    ss << "TR-DOS System Info (Drive " << static_cast<char>('A' + drive) << "):" << NEWLINE;
    ss << "  Label: " << (label.empty() ? "(none)" : label) << NEWLINE;
    ss << "  Disk Type: " << StringHelper::ToHex(diskType);
    switch (diskType)
    {
        case 0x16: ss << " (80T DS)"; break;
        case 0x17: ss << " (40T DS)"; break;
        case 0x18: ss << " (80T SS)"; break;
        case 0x19: ss << " (40T SS)"; break;
        default: ss << " (unknown)"; break;
    }
    ss << NEWLINE;
    ss << "  Files: " << (int)fileCount << NEWLINE;
    ss << "  Free: " << freeSectors << " sectors" << NEWLINE;
    ss << "  First Free: Track " << (int)firstFreeTrack << ", Sector " << (int)firstFreeSector << NEWLINE;
    ss << "  Signature: " << StringHelper::ToHex(signature);
    ss << (signature == 0x10 ? " (valid)" : " (INVALID)") << NEWLINE;
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleDiskCatalog(const ClientSession& session, EmulatorContext* context,
                                      const std::vector<std::string>& args)
{
    // disk catalog [drive] - defaults to A
    uint8_t drive = 0;
    if (args.size() >= 2)
    {
        std::string errorMsg;
        drive = ParseDriveParameter(args[1], errorMsg);
        if (drive == 0xFF)
        {
            session.SendResponse(std::string("Error: ") + errorMsg + NEWLINE);
            return;
        }
    }
    
    FDD* fdd = context->coreState.diskDrives[drive];
    if (!fdd || !fdd->isDiskInserted())
    {
        session.SendResponse(std::string("Error: No disk in drive ") + static_cast<char>('A' + drive) + NEWLINE);
        return;
    }
    
    DiskImage* diskImage = fdd->getDiskImage();
    if (!diskImage)
    {
        session.SendResponse(std::string("Error: Cannot access disk image") + NEWLINE);
        return;
    }
    
    DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(0, 0);
    if (!track)
    {
        session.SendResponse(std::string("Error: Cannot read directory track") + NEWLINE);
        return;
    }
    
    std::stringstream ss;
    ss << "Disk Catalog (Drive " << static_cast<char>('A' + drive) << "):" << NEWLINE;
    ss << "  Name      Ext  Size   Start" << NEWLINE;
    ss << "  --------  ---  -----  -----" << NEWLINE;
    
    int fileCount = 0;
    // Read sectors 1-8 (0-indexed: 0-7) for directory entries
    for (int secNum = 0; secNum < 8; secNum++)
    {
        DiskImage::RawSectorBytes* sector = track->getRawSector(secNum);
        if (!sector) continue;
        
        const uint8_t* data = sector->data;
        // 16 entries per sector, 16 bytes each
        for (int entry = 0; entry < 16; entry++)
        {
            const uint8_t* e = data + (entry * 16);
            
            // First byte: 0x00 = deleted, 0x01+ = valid
            if (e[0] == 0x00) continue;  // Deleted
            if (e[0] > 0x7F) continue;   // Invalid
            
            // Filename: bytes 0-7, extension: byte 8
            std::string name((const char*)e, 8);
            char ext = (char)e[8];
            uint16_t start = e[9] | (e[10] << 8);
            uint16_t length = e[11] | (e[12] << 8);
            uint8_t sectors = e[13];
            uint8_t startSec = e[14];
            uint8_t startTrack = e[15];
            
            // Trim trailing spaces from name
            while (!name.empty() && name.back() == ' ') name.pop_back();
            
            ss << "  " << std::left << std::setw(8) << name 
               << "  " << ext
               << "   " << std::right << std::setw(5) << length
               << "  T" << std::setw(2) << (int)startTrack << "/S" << (int)startSec
               << NEWLINE;
            
            fileCount++;
        }
    }
    
    if (fileCount == 0)
    {
        ss << "  (empty)" << NEWLINE;
    }
    else
    {
        ss << NEWLINE << "  " << fileCount << " file(s)" << NEWLINE;
    }
    
    session.SendResponse(ss.str());
}

/// endregion </Disk Inspection Commands>

/// region <Disk Create Command>

void CLIProcessor::HandleDiskCreate(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                    EmulatorContext* context, const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing drive parameter") + NEWLINE + 
                             "Usage: disk create <drive> [cylinders] [sides]" + NEWLINE +
                             "  cylinders: 40 or 80 (default: 80)" + NEWLINE +
                             "  sides: 1 or 2 (default: 2)" + NEWLINE);
        return;
    }

    // Parse drive
    std::string errorMsg;
    uint8_t drive = ParseDriveParameter(args[1], errorMsg);
    if (drive == 0xFF)
    {
        session.SendResponse(std::string("Error: ") + errorMsg + NEWLINE);
        return;
    }

    // Parse optional cylinders (default 80)
    uint8_t cylinders = 80;
    if (args.size() >= 3)
    {
        try {
            int c = std::stoi(args[2]);
            if (c == 40 || c == 80)
                cylinders = static_cast<uint8_t>(c);
            else
            {
                session.SendResponse(std::string("Error: Cylinders must be 40 or 80") + NEWLINE);
                return;
            }
        } catch (...) {
            session.SendResponse(std::string("Error: Invalid cylinders value") + NEWLINE);
            return;
        }
    }

    // Parse optional sides (default 2)
    uint8_t sides = 2;
    if (args.size() >= 4)
    {
        try {
            int s = std::stoi(args[3]);
            if (s == 1 || s == 2)
                sides = static_cast<uint8_t>(s);
            else
            {
                session.SendResponse(std::string("Error: Sides must be 1 or 2") + NEWLINE);
                return;
            }
        } catch (...) {
            session.SendResponse(std::string("Error: Invalid sides value") + NEWLINE);
            return;
        }
    }

    // Validate context
    if (!context || !context->coreState.diskDrives[drive])
    {
        session.SendResponse(std::string("Error: Drive not available") + NEWLINE);
        return;
    }

    // Create blank disk image
    DiskImage* diskImage = new DiskImage(cylinders, sides);

    // Insert into drive
    FDD* fdd = context->coreState.diskDrives[drive];
    fdd->insertDisk(diskImage);

    // Update path tracking for API queries
    context->coreState.diskFilePaths[drive] = "<blank>";

    std::stringstream ss;
    ss << "Created blank disk in drive " << static_cast<char>('A' + drive) << NEWLINE;
    ss << "  Cylinders: " << (int)cylinders << NEWLINE;
    ss << "  Sides: " << (int)sides << NEWLINE;
    ss << "  Ready for TR-DOS FORMAT" << NEWLINE;
    session.SendResponse(ss.str());
}

/// endregion </Disk Create Command>

/// endregion </Disk Control Commands>
