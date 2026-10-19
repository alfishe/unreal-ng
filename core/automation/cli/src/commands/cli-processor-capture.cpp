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
#include <emulator/video/next/nextlayerimage.h>
#include <emulator/video/screenshotter.h>

#include <fstream>

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

    // capture screen --layer=ula|layer2|tilemap|sprites|ids [--format=png|rgba] [--alpha=false] [--size=screen|grid|pixel] [file]: one layer of the last Next frame
    // (design-layer-capture.md): a PNG with alpha 0 where the layer does not paint, or the raw RGBA bytes; ids = the winner of each pixel
    for (size_t i = 1; i < args.size(); i++)
    {
        if (args[i].rfind("--layer=", 0) != 0)
            continue;
        NextLayer::Kind kind = NextLayer::Kind::Composed;
        bool rgba = false, alpha = true;
        NextLayer::Scale scale = NextLayer::Scale::Screen;
        std::string message, path;
        if (!NextLayer::ParseKind(args[i].substr(8), kind))
        {
            session.SendResponse("Error: unknown layer '" + args[i].substr(8) + "': use " + NextLayer::KindWords() + NEWLINE);
            return;
        }
        for (size_t j = 1; j < args.size(); j++)
        {
            const std::string& arg = args[j];
            if (j == i)
                continue;
            if (arg.rfind("--format=", 0) == 0 && !NextLayer::ParseImageFormat(arg.substr(9), rgba, message))
            {
                session.SendResponse("Error: " + message + NEWLINE);
                return;
            }
            else if (arg.rfind("--alpha=", 0) == 0 && !NextLayer::ParseAlpha(arg.substr(8), alpha, message))
            {
                session.SendResponse("Error: " + message + NEWLINE);
                return;
            }
            else if (arg.rfind("--size=", 0) == 0 && !NextLayer::ParseScale(arg.substr(7), scale, message))
            {
                session.SendResponse("Error: " + message + NEWLINE);
                return;
            }
            else if (arg.rfind("--", 0) != 0)
                path = arg;
        }
        if (kind == NextLayer::Kind::Composed)
        {
            session.SendResponse(std::string("Error: layer=composed is the ordinary screenshot: capture screen [file]") + NEWLINE);
            return;
        }
        const NextLayer::Image image = NextLayer::Capture(context, kind, alpha, scale);
        if (image.status != NextLayer::Status::Ok)
        {
            session.SendResponse("Error: " + image.message + NEWLINE);
            return;
        }
        const std::vector<uint8_t> bytes = rgba ? image.rgba : NextLayer::EncodePng(image.rgba, image.width, image.height);
        std::stringstream ss;
        ss << "Layer " << NextLayer::KindName(kind) << " of Next frame #" << image.sequence << " (emulator frame " << image.frameCounter << "):" << NEWLINE;
        ss << "  Format: " << (rgba ? "rgba" : "png")
           << (kind == NextLayer::Kind::Ids ? ", opaque (every pixel has a winner)" : (alpha ? ", alpha 0 where the layer does not paint" : ", flattened onto the fallback colour"))
           << NEWLINE;
        ss << "  Size: " << image.width << "x" << image.height << ", " << bytes.size() << " bytes" << NEWLINE;
        if (kind == NextLayer::Kind::Ids)
        {
            ss << "  Legend:";
            for (unsigned id = 0; id < NextLayer::kIdCount; id++)
            {
                const uint32_t c = NextLayer::IdColour(id);
                char colour[8];
                std::snprintf(colour, sizeof colour, "#%02X%02X%02X", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
                ss << " " << id << "=" << NextLayer::IdName(id) << "(" << colour << ")";
            }
            ss << NEWLINE;
        }
        if (!path.empty())
        {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
            {
                session.SendResponse("Error: the file could not be written: " + path + NEWLINE);
                return;
            }
            ss << "  Saved to: " << path << NEWLINE;
        }
        else if (rgba)
            ss << "  (raw RGBA: give a file to save it)" << NEWLINE;
        else
            ss << NEWLINE << "data:image/png;base64," << NextLayer::Base64(bytes) << NEWLINE;
        session.SendResponse(ss.str());
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
        else if (arg.rfind("--source=", 0) == 0)
        {
            word = arg.substr(9);
            if (!Screenshotter::ParseSource(word, options.source))
            {
                session.SendResponse("Error: unknown source '" + word + "': use presented, live or window" + NEWLINE);
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

    const ScreenshotResult shot = Screenshotter::TakeFrom(*context->pScreen, options, emulator->IsEmulationParked());
    if (!shot.ok)
    {
        session.SendResponse(std::string("Error: ") + shot.errorMessage + NEWLINE);
        return;
    }

    std::stringstream ss;
    ss << "Screenshot:" << NEWLINE;
    ss << "  Format: " << Screenshotter::FormatName(shot.format) << NEWLINE;
    ss << "  Area: " << Screenshotter::AreaName(options.area) << NEWLINE;
    ss << "  Source: " << Screenshotter::RequestSourceName(options.source) << NEWLINE;
    if (shot.frame.beamLine >= 0)
    {
        ss << "  Beam: line " << shot.frame.beamLine << ", T " << shot.frame.beamTstate
           << (shot.frame.partial ? " (the frame is half drawn)" : "") << NEWLINE;
    }
    ss << "  Size: " << shot.width << "x" << shot.height << " (at " << shot.crop.x << "," << shot.crop.y
       << " of the " << shot.frame.width << "x" << shot.frame.height << " frame)" << NEWLINE;
    if (shot.fromWindow)
    {
        ss << "  Window: " << shot.window.backend << ", picture " << shot.window.picture.width << "x"
           << shot.window.picture.height << " at " << shot.window.picture.x << "," << shot.window.picture.y << " of a "
           << shot.window.viewport.width << "x" << shot.window.viewport.height << " viewport, pixel ratio "
           << shot.window.devicePixelRatio << NEWLINE;
    }
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
    ss << "  capture screen [--area=full|screen] [--format=png|gif] [--source=presented|live|window] [file]" << NEWLINE;
    ss << "                                  Screenshot: the whole frame (default) or the working picture;" << NEWLINE;
    ss << "                                  PNG (default) or GIF; the presented frame (default) or the live" << NEWLINE;
    ss << "                                  one as drawn now, or the picture as the GUI window draws it" << NEWLINE;
    ss << "                                  (window: unreal-qt only); to a file or printed as a data URI" << NEWLINE;
    ss << "  capture screen --layer=ula|layer2|tilemap|sprites|ids [--format=png|rgba] [--alpha=false] [--size=screen|grid|pixel] [file]" << NEWLINE;
    ss << "                                  ZX Spectrum Next: ONE LAYER of the last completed frame (PNG with alpha 0 where the layer" << NEWLINE;
    ss << "                                  does not paint; ids = the winner of every pixel); `next layer-capture on|off` switches the record" << NEWLINE;
    ss << "  capture romtext                 Capture ROM print output (TODO)" << NEWLINE;
    ss << "  capture framebuffer <file> [rgba|index]  Raw pixels (RGBA, or u16 pens on the Sprinter)" << NEWLINE;
    ss << NEWLINE;
    ss << "Examples:" << NEWLINE;
    ss << "  capture ocr                     Extract text from screen" << NEWLINE;
    ss << "  capture screen                  Whole frame as PNG, printed as a data URI" << NEWLINE;
    ss << "  capture screen --area=screen    The working picture only (no border)" << NEWLINE;
    ss << "  capture screen scratch/shot.png Save the whole frame to a file" << NEWLINE;
    ss << "  capture screen --format=gif     256-color GIF instead of PNG" << NEWLINE;
    ss << "  capture screen --source=live    The frame as drawn now (a paused machine reports where the beam stopped)" << NEWLINE;
    ss << NEWLINE;

    session.SendResponse(ss.str());
}

/// endregion </Capture Commands>
