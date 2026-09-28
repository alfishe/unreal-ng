/// @file ttdclipexport.cpp
/// @brief TimeTravelManager::ExportClip - a TTD range written to disk as a
/// lossless clip (final picture + plane B + frame meta per frame).
///
/// Reference material for ZX DLSS (docs/inprogress/2026-09-27-zxdlss-gigascreen/
/// p0a-plane-b.md). Walking the range inside the core replaces one WebAPI
/// seek + capture round trip per frame.

#include "stdafx.h"

#include "timetravelmanager.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ttdcompression.h"
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

TimeTravelManager::TTDClipExportResult TimeTravelManager::ExportClip(const TTDClipExportOptions& options)
{
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
    const std::filesystem::path dir(options.directory);
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

    Screen* screen = _context->pScreen;
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

    for (uint64_t frame = options.fromFrame; frame <= options.toFrame; ++frame)
    {
        // Positioning by frame number shows the frame's final picture (design
        // 2026-09-28-ttd-positioning-and-display §3). Internal positioning, so
        // no publication per frame; the last one is published below.
        if (!SeekToInternal(TTDTimePoint{frame, 0}, nullptr))
        {
            result.error = "cannot position at frame " + std::to_string(frame);
            return result;
        }
        const uint8_t p7FFD = _context->emulatorState.p7FFD;
        const uint8_t border = _context->emulatorState.pFE & 0b0000'0111;
        const uint8_t activeScreen = screen->GetActiveScreen();
        ComposeDisplay(/*frameTarget=*/true);

        uint32_t* fb = nullptr;
        size_t fbSize = 0;
        screen->GetFramebufferData(&fb, &fbSize);
        size_t planeBCount = 0;
        const uint16_t* planeB = screen->GetPlaneB(&planeBCount);
        if (frame == options.fromFrame)
        {
            const FramebufferDescriptor& d = screen->GetFramebufferDescriptor();
            result.width = d.width;
            result.height = d.height;
            result.planeB = planeB != nullptr;
            frameBytes = fbSize;
            planeBBytes = planeBCount * sizeof(uint16_t);
        }
        if (!fb || fbSize != frameBytes || (result.planeB && (!planeB || planeBCount * sizeof(uint16_t) != planeBBytes)))
        {
            result.error = "frame geometry changed at frame " + std::to_string(frame) + " (video mode switch)";
            return result;
        }

        const auto* fbBytes = reinterpret_cast<const uint8_t*>(fb);
        rgbaChunk.insert(rgbaChunk.end(), fbBytes, fbBytes + fbSize);
        if (result.planeB)
        {
            const auto* pbBytes = reinterpret_cast<const uint8_t*>(planeB);
            planeBChunk.insert(planeBChunk.end(), pbBytes, pbBytes + planeBBytes);
        }
        meta << "{\"frame\": " << frame << ", \"p7FFD\": " << static_cast<int>(p7FFD)
             << ", \"active_screen\": " << static_cast<int>(activeScreen) << ", \"border\": " << static_cast<int>(border)
             << "}\n";

        result.frames++;
        if (++inChunk == options.chunkFrames && !flush())
        {
            result.error = "cannot write chunk " + std::to_string(chunkIndex);
            return result;
        }
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
    json << "\n  },\n"
         << "  \"meta\": \"meta.jsonl: frame, p7FFD, active_screen, border at the frame's start\"\n"
         << "}\n";
    const std::string text = json.str();
    if (!WriteFile(dir / "clip.json", std::vector<uint8_t>(text.begin(), text.end()), result.bytesWritten))
    {
        result.error = "cannot write clip.json";
        return result;
    }

    // Leave the machine where the export ended, shown as usual
    PublishSeekedFrame();

    result.ok = true;
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}

}  // namespace ttd
