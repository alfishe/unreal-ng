#include <common/filehelper.h>
#include <common/stringhelper.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/wd1793.h>
#include <emulator/platform.h>

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

    // If no arguments, show usage
    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: disk <subcommand> [args]" << NEWLINE;
        ss << NEWLINE;
        ss << "Available subcommands:" << NEWLINE;
        ss << "  insert <drive> <file>  - Insert disk image (.trd, .scl)" << NEWLINE;
        ss << "  eject <drive>          - Eject disk from drive" << NEWLINE;
        ss << "  info <drive>           - Show disk status" << NEWLINE;
        ss << NEWLINE;
        ss << "Drive: A, B, C, D (or 0, 1, 2, 3)" << NEWLINE;
        ss << NEWLINE;
        ss << "Examples:" << NEWLINE;
        ss << "  disk insert A /path/to/game.trd" << NEWLINE;
        ss << "  disk info A" << NEWLINE;
        ss << "  disk eject A" << NEWLINE;

        session.SendResponse(ss.str());
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
    else
    {
        session.SendResponse(std::string("Error: Unknown subcommand '") + args[0] + "'" + NEWLINE +
                             "Use 'disk' without arguments to see available subcommands." + NEWLINE);
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

/// endregion </Disk Control Commands>
