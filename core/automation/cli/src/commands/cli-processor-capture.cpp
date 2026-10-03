/// @author AI Assistant  
/// @date 22.01.2026
/// @brief CLI Capture commands handler (OCR, screen capture, ROM text capture)

#include <common/filehelper.h>
#include <emulator/video/framebufferexport.h>
#include "cli-processor.h"

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <debugger/analyzers/rom-print/screenocr.h>
#include <emulator/emulatorcontext.h>
#include <emulator/video/screenshotter.h>

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
    else if (subcommand == "framebuffer")
    {
        // capture framebuffer <file> [rgba|index]: raw pixels (core FramebufferExport, as GET /capture/framebuffer)
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Usage: capture framebuffer <file> [rgba|index]") + NEWLINE);
            return;
        }
        FramebufferExport::Frame frame;
        std::string error;
        if (!FramebufferExport::Capture(emulator->GetContext(), args.size() > 2 ? args[2] : "rgba", frame, error))
        {
            session.SendResponse("Error: " + error + NEWLINE);
            return;
        }
        if (!FileHelper::SaveBufferToFile(args[1], frame.bytes.data(), frame.bytes.size()))
        {
            session.SendResponse("Error: cannot write '" + args[1] + "'" + NEWLINE);
            return;
        }
        session.SendResponse("Saved " + std::to_string(frame.width) + " x " + std::to_string(frame.height) + " " +
                             frame.format + " (" + frame.encoding + "), " + std::to_string(frame.bytes.size()) +
                             " bytes to " + args[1] + NEWLINE);
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
    // The session's own emulator (not "the selected one, or the first")
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pScreen)
    {
        session.SendResponse(std::string("Error: the emulator has no screen") + NEWLINE);
        return;
    }

    // capture screen [--area=full|screen] [--format=png|gif] [file]
    // Legacy spellings kept: -png / -gif / png / gif, and the bare words full / screen
    ScreenshotOptions options;  // the whole frame, PNG
    for (size_t i = 1; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        std::string word;
        if (arg.rfind("--area=", 0) == 0)
        {
            word = arg.substr(7);
            if (!Screenshotter::ParseArea(word, options.area))
            {
                session.SendResponse("Error: unknown area '" + word + "': use full or screen" + NEWLINE);
                return;
            }
        }
        else if (arg.rfind("--format=", 0) == 0)
        {
            word = arg.substr(9);
            if (!Screenshotter::ParseFormat(word, options.format))
            {
                session.SendResponse("Error: unknown format '" + word + "': use png or gif" + NEWLINE);
                return;
            }
        }
        else if (arg == "-png" || arg == "png")
        {
            options.format = ScreenshotFormat::Png;
        }
        else if (arg == "-gif" || arg == "gif")
        {
            options.format = ScreenshotFormat::Gif;
        }
        else if (arg == "full" || arg == "screen")
        {
            Screenshotter::ParseArea(arg, options.area);
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            session.SendResponse("Error: unknown option '" + arg + "' (see: capture)" + NEWLINE);
            return;
        }
        else
        {
            options.saveTo = arg;  // a path: write the image there instead of printing it
        }
    }

    const ScreenshotResult shot = Screenshotter::TakeFrom(*context->pScreen, options);
    if (!shot.ok)
    {
        session.SendResponse(std::string("Error: ") + shot.errorMessage + NEWLINE);
        return;
    }

    std::stringstream ss;
    ss << "Screenshot:" << NEWLINE;
    ss << "  Format: " << Screenshotter::FormatName(shot.format) << NEWLINE;
    ss << "  Area: " << Screenshotter::AreaName(options.area) << NEWLINE;
    ss << "  Size: " << shot.width << "x" << shot.height << " (at " << shot.crop.x << "," << shot.crop.y
       << " of the " << shot.frame.width << "x" << shot.frame.height << " frame)" << NEWLINE;
    ss << "  Screen window: " << shot.frame.screenWindow.width << "x" << shot.frame.screenWindow.height << " at "
       << shot.frame.screenWindow.x << "," << shot.frame.screenWindow.y << NEWLINE;
    ss << "  Data size: " << shot.encodedSize << " bytes" << NEWLINE;
    if (!shot.savedFile.empty())
    {
        ss << "  Saved to: " << shot.savedFile << NEWLINE;
    }
    else
    {
        const std::string base64 = Screenshotter::Base64Encode(shot.bytes);
        ss << "  Base64 length: " << base64.size() << " chars" << NEWLINE;
        ss << NEWLINE;
        ss << "data:" << (shot.format == ScreenshotFormat::Png ? "image/png" : "image/gif") << ";base64," << base64
           << NEWLINE;
    }

    session.SendResponse(ss.str());
}

void CLIProcessor::ShowCaptureHelp(const ClientSession& session)
{
    std::stringstream ss;
    ss << "Capture Commands" << NEWLINE;
    ss << "================" << NEWLINE;
    ss << NEWLINE;
    ss << "  capture ocr                     OCR text from screen (ROM font)" << NEWLINE;
    ss << "  capture screen [--area=full|screen] [--format=png|gif] [file]" << NEWLINE;
    ss << "                                  Screenshot of the presented frame: the whole frame (default)" << NEWLINE;
    ss << "                                  or the working picture; PNG (default) or GIF; to a file or" << NEWLINE;
    ss << "                                  printed as a data URI" << NEWLINE;
    ss << "  capture romtext                 Capture ROM print output (TODO)" << NEWLINE;
    ss << "  capture framebuffer <file> [rgba|index]  Raw pixels (RGBA, or u16 pens on the Sprinter)" << NEWLINE;
    ss << NEWLINE;
    ss << "Examples:" << NEWLINE;
    ss << "  capture ocr                     Extract text from screen" << NEWLINE;
    ss << "  capture screen                  Whole frame as PNG, printed as a data URI" << NEWLINE;
    ss << "  capture screen --area=screen    The working picture only (no border)" << NEWLINE;
    ss << "  capture screen scratch/shot.png Save the whole frame to a file" << NEWLINE;
    ss << "  capture screen --format=gif     256-color GIF instead of PNG" << NEWLINE;
    ss << NEWLINE;

    session.SendResponse(ss.str());
}

/// endregion </Capture Commands>
