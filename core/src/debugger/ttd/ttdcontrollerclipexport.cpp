/// @file ttdcontrollerclipexport.cpp (a copy of ttdclipexport.cpp for the controller)
/// @brief TimeTravelController::ExportClip - a TTD range written to disk as a
/// lossless clip (final picture + plane B + frame meta per frame).
///
/// Reference material for ZX DLSS (docs/inprogress/2026-09-27-zxdlss-gigascreen/
/// p0a-plane-b.md). Walking the range inside the core replaces one WebAPI
/// seek + capture round trip per frame.

#include "stdafx.h"

#include "timetravelcontroller.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ttdcompression.h"
#include "common/filehelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/video/screen.h"

namespace ttd
{

namespace
{
std::string ChunkName(const char* prefix, uint32_t index)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%s_%04u.zst", prefix, index);
    return name;
}

bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& data, uint64_t& bytesWritten)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    bytesWritten += data.size();
    return static_cast<bool>(out);
}
}  // namespace

std::string TimeTravelController::VisitComposedFrames(uint64_t fromFrame, uint64_t toFrame, const TTDFrameVisitor& visit)
{
    if (_state == TTDSessionState::Recording)
        return "session is recording - stop it first";
    if (_timeline.empty() || !_context || !_context->pScreen)
        return "no recorded history";
    const uint64_t first = _timeline.front().time.frame;
    const uint64_t last = _timeline.back().time.frame;
    if (fromFrame > toFrame || fromFrame < first || toFrame > last)
        return "frame range outside the session [" + std::to_string(first) + ", " + std::to_string(last) + "]";

    Screen* screen = _context->pScreen;
    std::string error;
    for (uint64_t frame = fromFrame; frame <= toFrame; ++frame)
    {
        // Positioning by frame number shows the frame's final picture (design
        // 2026-09-28-ttd-positioning-and-display §3). Internal positioning, so
        // no publication per frame; the last one is published below.
        if (!SeekToInternal(TTDTimePoint{frame, 0}, nullptr))
        {
            error = "cannot position at frame " + std::to_string(frame);
            break;
        }
        TTDComposedFrame f;
        f.frame = frame;
        f.p7FFD = _context->emulatorState.p7FFD;
        f.border = _context->emulatorState.pFE & 0b0000'0111;
        f.activeScreen = screen->GetActiveScreen();
        ComposeDisplay(/*frameTarget=*/true, kCurrentFrame);   // positioned at its start: this frame to its end

        uint32_t* fb = nullptr;
        size_t fbSize = 0;
        screen->GetFramebufferData(&fb, &fbSize);
        const FramebufferDescriptor& d = screen->GetFramebufferDescriptor();
        f.rgba = reinterpret_cast<const uint8_t*>(fb);
        f.rgbaBytes = fbSize;
        f.planeB = screen->GetPlaneB(&f.planeBCount);
        f.width = d.width;
        f.height = d.height;
        if (!visit(f))
            break;
    }

    // Leave the machine where the walk ended, shown as usual
    PublishSeekedFrame();
    return error;
}

TimeTravelController::TTDClipExportResult TimeTravelController::ExportClip(const TTDClipExportOptions& options)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    TTDClipExportResult result;
    const auto started = std::chrono::steady_clock::now();

    if (_state == TTDSessionState::Recording)
    {
        result.error = "session is recording - stop it first";
        return result;
    }
    if (_timeline.empty() || !_context || !_context->pScreen)
    {
        result.error = "no recorded history";
        return result;
    }
    const uint64_t first = _timeline.front().time.frame;
    const uint64_t last = _timeline.back().time.frame;
    if (options.fromFrame > options.toFrame || options.fromFrame < first || options.toFrame > last)
    {
        result.error = "frame range outside the session [" + std::to_string(first) + ", " + std::to_string(last) + "]";
        return result;
    }
    if (options.directory.empty() || options.chunkFrames == 0)
    {
        result.error = "directory and chunkFrames are required";
        return result;
    }

    std::error_code ec;
    const std::filesystem::path dir = FileHelper::ToFsPath(options.directory);   // UTF-8: non-ASCII names on Windows
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        result.error = "cannot create " + options.directory + ": " + ec.message();
        return result;
    }
    std::ofstream meta(dir / "meta.jsonl", std::ios::trunc);
    if (!meta)
    {
        result.error = "cannot write meta.jsonl";
        return result;
    }

    std::vector<uint8_t> rgbaChunk;
    std::vector<uint8_t> planeBChunk;
    uint32_t inChunk = 0;
    uint32_t chunkIndex = 0;
    size_t frameBytes = 0;
    size_t planeBBytes = 0;

    auto flush = [&]() -> bool {
        if (inChunk == 0)
            return true;
        if (!WriteFile(dir / ChunkName("rgba", chunkIndex), codec::Compress(rgbaChunk.data(), rgbaChunk.size(), options.zstdLevel),
                       result.bytesWritten))
            return false;
        if (result.planeB &&
            !WriteFile(dir / ChunkName("planeb", chunkIndex),
                       codec::Compress(planeBChunk.data(), planeBChunk.size(), options.zstdLevel), result.bytesWritten))
            return false;
        rgbaChunk.clear();
        planeBChunk.clear();
        inChunk = 0;
        chunkIndex++;
        return true;
    };

    const std::string walkError = VisitComposedFrames(options.fromFrame, options.toFrame, [&](const TTDComposedFrame& f) {
        if (f.frame == options.fromFrame)
        {
            result.width = f.width;
            result.height = f.height;
            result.planeB = f.planeB != nullptr;
            frameBytes = f.rgbaBytes;
            planeBBytes = f.planeBCount * sizeof(uint16_t);
        }
        if (!f.rgba || f.rgbaBytes != frameBytes ||
            (result.planeB && (!f.planeB || f.planeBCount * sizeof(uint16_t) != planeBBytes)))
        {
            result.error = "frame geometry changed at frame " + std::to_string(f.frame) + " (video mode switch)";
            return false;
        }
        rgbaChunk.insert(rgbaChunk.end(), f.rgba, f.rgba + f.rgbaBytes);
        if (result.planeB)
        {
            const auto* pbBytes = reinterpret_cast<const uint8_t*>(f.planeB);
            planeBChunk.insert(planeBChunk.end(), pbBytes, pbBytes + planeBBytes);
        }
        meta << "{\"frame\": " << f.frame << ", \"p7FFD\": " << static_cast<int>(f.p7FFD)
             << ", \"active_screen\": " << static_cast<int>(f.activeScreen) << ", \"border\": " << static_cast<int>(f.border)
             << "}\n";
        result.frames++;
        if (++inChunk == options.chunkFrames && !flush())
        {
            result.error = "cannot write chunk " + std::to_string(chunkIndex);
            return false;
        }
        return true;
    });
    if (!result.error.empty())
        return result;
    if (!walkError.empty())
    {
        result.error = walkError;
        return result;
    }
    if (!flush())
    {
        result.error = "cannot write chunk " + std::to_string(chunkIndex);
        return result;
    }
    meta.close();

    std::ostringstream json;
    json << "{\n"
         << "  \"format\": \"unreal-ng-clip\",\n"
         << "  \"version\": 2,\n"
         << "  \"width\": " << result.width << ",\n"
         << "  \"height\": " << result.height << ",\n"
         << "  \"from\": " << options.fromFrame << ",\n"
         << "  \"to\": " << options.toFrame << ",\n"
         << "  \"frames\": " << result.frames << ",\n"
         << "  \"chunk\": " << options.chunkFrames << ",\n"
         << "  \"picture\": \"final beam-rendered picture of each frame (TTD positioning by frame number)\",\n"
         << "  \"planes\": {\n"
         << "    \"rgba\": {\"files\": \"rgba_NNNN.zst\", \"bytes_per_pixel\": 4, \"order\": \"R,G,B,A\"}";
    if (result.planeB)
    {
        json << ",\n    \"planeb\": {\"files\": \"planeb_NNNN.zst\", \"bytes_per_pixel\": 2, "
             << "\"encoding\": \"u16le: attr[0:7] color[8:11] ink[12] role[13:14] (1 screen, 2 border)\"}";
    }
    json << "\n  },\n";
    if (result.planeB && _context->pScreen)
    {
        // The 16 ZX colors plane B's color indices were drawn in (the live palette):
        // a consumer mixing plane B needs them, not a copy of some default table
        uint32_t palette[16];
        _context->pScreen->GetRGBAPalette16(palette);
        json << "  \"palette16\": [";
        for (int c = 0; c < 16; ++c)
        {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "\"#%02x%02x%02x\"", palette[c] & 0xFF, (palette[c] >> 8) & 0xFF,
                          (palette[c] >> 16) & 0xFF);
            json << (c ? ", " : "") << hex;
        }
        json << "],\n";
    }
    json << "  \"meta\":\"meta.jsonl: frame, p7FFD, active_screen, border at the frame's start\"\n"
         << "}\n";
    const std::string text = json.str();
    if (!WriteFile(dir / "clip.json", std::vector<uint8_t>(text.begin(), text.end()), result.bytesWritten))
    {
        result.error = "cannot write clip.json";
        return result;
    }

    result.ok = true;
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}

}  // namespace ttd
