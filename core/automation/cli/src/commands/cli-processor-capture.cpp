/// @author AI Assistant  
/// @date 22.01.2026
/// @brief CLI Capture commands handler (OCR, screen capture, ROM text capture)

#include "cli-processor.h"

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <debugger/analyzers/rom-print/screenocr.h>
#include <emulator/video/screencapture.h>

#include <sstream>

/// region <Capture Commands>

void CLIProcessor::HandleCapture(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        return;
    }

    if (args.empty())
    {
        ShowCaptureHelp(session);
        return;
    }

    std::string subcommand = args[0];

    if (subcommand == "ocr")
    {
        HandleCaptureOCR(session, emulator);
    }
    else if (subcommand == "romtext")
    {
        // TODO: Implement ROM text capture (ROMPrintDetector)
        session.SendResponse(std::string("Error: 'capture romtext' not yet implemented.") + NEWLINE);
    }
    else if (subcommand == "screen")
    {
        HandleCaptureScreen(session, emulator, args);
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown subcommand '") + subcommand + "'" + NEWLINE +
                             "Use 'capture' without arguments to see available subcommands." + NEWLINE);
    }
}

void CLIProcessor::HandleCaptureOCR(const ClientSession& session, std::shared_ptr<Emulator> emulator)
{
    // Get emulator ID from EmulatorManager
    auto manager = EmulatorManager::GetInstance();
    std::string emulatorId = manager->GetSelectedEmulatorId();
    
    if (emulatorId.empty())
    {
        // Try to get the first (only) emulator
        auto ids = manager->GetEmulatorIds();
        if (!ids.empty())
        {
            emulatorId = ids[0];
        }
    }
    
    // Run OCR on screen
    std::string screenText = ScreenOCR::ocrScreen(emulatorId);
    
    if (screenText.empty())
    {
        session.SendResponse(std::string("Error: Unable to read screen.") + NEWLINE);
        return;
    }
    
    std::stringstream ss;
    ss << "Screen OCR (32x24):" << NEWLINE;
    ss << "================================" << NEWLINE;
    ss << screenText;
    ss << "================================" << NEWLINE;
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleCaptureScreen(const ClientSession& session, 
                                         std::shared_ptr<Emulator> emulator,
                                         const std::vector<std::string>& args)
{
    // Get emulator ID
    auto manager = EmulatorManager::GetInstance();
    std::string emulatorId = manager->GetSelectedEmulatorId();
    
    if (emulatorId.empty())
    {
        auto ids = manager->GetEmulatorIds();
        if (!ids.empty())
        {
            emulatorId = ids[0];
        }
    }
    
    // Parse format option (default: gif)
    std::string format = "gif";
    for (size_t i = 1; i < args.size(); i++)
    {
        if (args[i] == "--format=png" || args[i] == "-png" || args[i] == "png")
        {
            format = "png";
        }
        else if (args[i] == "--format=gif" || args[i] == "-gif" || args[i] == "gif")
        {
            format = "gif";
        }
        // Future: handle page selection (5, 7, shadow)
    }
    
    // Capture screen
    auto result = ScreenCapture::captureScreen(emulatorId, format);
    
    if (!result.success)
    {
        session.SendResponse(std::string("Error: ") + result.errorMessage + NEWLINE);
        return;
    }
    
    std::stringstream ss;
    ss << "Screen Capture:" << NEWLINE;
    ss << "  Format: " << result.format << NEWLINE;
    ss << "  Size: " << result.width << "x" << result.height << NEWLINE;
    ss << "  Data size: " << result.originalSize << " bytes" << NEWLINE;
    ss << "  Base64 length: " << result.base64Data.size() << " chars" << NEWLINE;
    ss << NEWLINE;
    ss << "data:" << (result.format == "png" ? "image/png" : "image/gif") 
       << ";base64," << result.base64Data << NEWLINE;
    
    session.SendResponse(ss.str());
}

void CLIProcessor::ShowCaptureHelp(const ClientSession& session)
{
    std::stringstream ss;
    ss << "Capture Commands" << NEWLINE;
    ss << "================" << NEWLINE;
    ss << NEWLINE;
    ss << "  capture ocr                     OCR text from screen (ROM font)" << NEWLINE;
    ss << "  capture screen [--format=gif|png]  Capture screen bitmap" << NEWLINE;
    ss << "  capture romtext                 Capture ROM print output (TODO)" << NEWLINE;
    ss << NEWLINE;
    ss << "Examples:" << NEWLINE;
    ss << "  capture ocr                     Extract text from screen" << NEWLINE;
    ss << "  capture screen                  Capture as GIF (default)" << NEWLINE;
    ss << "  capture screen --format=png    Capture as PNG" << NEWLINE;
    ss << NEWLINE;

    session.SendResponse(ss.str());
}

/// endregion </Capture Commands>
