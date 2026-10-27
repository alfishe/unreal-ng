// ZX-Evo flash ROM - 'romflash' command: the saved flash (EvoFlash persistence). Same core calls every interface
// uses (evoflashrequest.h).

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/atm/evoflashrequest.h>

#include <sstream>

#include "cli-processor.h"

void CLIProcessor::HandleRomFlash(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    const std::string sub = args.empty() ? "status" : args[0];

    if (sub == "help")
    {
        std::stringstream ss;
        ss << "ZX-Evo flash ROM (TS-Conf, ATM3): what programs flash is saved per machine, never to the ROM image:" << NEWLINE;
        ss << "  romflash [status]  - The file for the loaded ROM image, unsaved changes, files of other ROM images" << NEWLINE;
        ss << "  romflash save      - Write the flash file now (also ~1 s after the last program / erase and on exit)" << NEWLINE;
        ss << "  romflash discard   - Delete the flash file; the shipped ROM image comes back at the next reset" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    if (sub == "save" || sub == "discard")
    {
        const EvoFlashResult result = sub == "save" ? EvoFlashRequestSave(context) : EvoFlashRequestDiscard(context);
        std::string text = std::string(EvoFlashAccepted(result) ? "" : "Error: ") + "romflash " + sub + ": " +
                           EvoFlashResultText(result);
        if (sub == "discard" && EvoFlashAccepted(result))
            text += " (the shipped ROM image returns at the next reset)";
        session.SendResponse(text + NEWLINE);
        return;
    }

    if (sub != "status")
    {
        session.SendResponse("Usage: romflash [status|save|discard|help]" + std::string(NEWLINE));
        return;
    }

    EvoFlash::PersistStatus status;
    if (!EvoFlashGetStatus(context, status))
    {
        session.SendResponse(std::string("Error: ") + EvoFlashResultText(EvoFlashResult::NoFlash) + NEWLINE);
        return;
    }
    std::stringstream ss;
    ss << "ZX-Evo flash ROM (" << status.machine << ")" << NEWLINE;
    ss << "  file:            " << (status.path.empty() ? "(none: persistence off or no ROM loaded)" : status.path)
       << (status.fileExists ? "" : " (not written)") << NEWLINE;
    ss << "  base ROM SHA-256: " << status.baseDigest << NEWLINE;
    ss << "  loaded from file: " << (status.loadedFromFile ? "yes" : "no") << NEWLINE;
    ss << "  unsaved:          " << (status.unsaved ? "yes" : "no") << " (" << status.changes
       << " programs / erases this run)" << NEWLINE;
    for (const std::string& other : status.otherImageFiles)
        ss << "  not used (another ROM image): " << other << NEWLINE;
    session.SendResponse(ss.str());
}
