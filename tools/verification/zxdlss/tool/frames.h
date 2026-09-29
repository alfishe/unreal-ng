#pragma once

/// @file frames.h
/// @brief Frame sources (a core-exported clip, a TTD file) and sinks (an ffmpeg
/// video, an RGB dump for comparing implementations) of zxdlss-render.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace zxdlss
{

/// One input frame: its emulated frame number and plane B (uint16 per pixel).
struct SourceFrame
{
    uint64_t frame = 0;
    int width = 0;
    int height = 0;
    std::vector<uint16_t> planeB;
};

using FrameCallback = std::function<bool(const SourceFrame&)>;   ///< false stops

/// Frames [from, to] of a clip written by POST /ttd/export-clip (format
/// "unreal-ng-clip" v2: clip.json, meta.jsonl, planeb_NNNN.zst).
/// @return empty on success, otherwise the reason
std::string readClip(const std::string& dir, uint64_t from, uint64_t to, const FrameCallback& cb);

/// First and last frame number of a clip.
bool clipRange(const std::string& dir, uint64_t& first, uint64_t& last, std::string& error);

/// Frames [from, to] of a TTD file, replayed through the emulator core
/// (only when built with the core; model: the machine the session was recorded on).
std::string readTtd(const std::string& path, const std::string& model, uint64_t from, uint64_t to, const FrameCallback& cb);

/// First and last frame of a TTD file's session.
bool ttdRange(const std::string& path, const std::string& model, uint64_t& first, uint64_t& last, std::string& error);

/// RGB24 frames piped into ffmpeg -> H.264 mp4 at 50 fps.
class VideoWriter
{
public:
    bool open(const std::string& path, int width, int height, int scale, std::string& error);
    void write(const uint8_t* rgb);
    ~VideoWriter();

private:
    FILE* _pipe = nullptr;
    int _w = 0, _h = 0, _scale = 1;
    std::vector<uint8_t> _scaled;
};

/// RGB24 frames as zstd chunks (rgb_NNNN.zst, 500 frames each) + dump.json:
/// the exact output of an algorithm, for frame-exact comparison with another
/// implementation (tools: compare_dump.py).
class DumpWriter
{
public:
    bool open(const std::string& dir, int width, int height, uint64_t firstFrame, std::string& error);
    void write(const uint8_t* rgb);
    bool close(std::string& error);
    ~DumpWriter();

private:
    std::string _dir;
    int _w = 0, _h = 0;
    uint64_t _first = 0, _frames = 0;
    uint32_t _chunk = 0, _inChunk = 0;
    std::vector<uint8_t> _buf;
    bool _open = false;
    bool flush(std::string& error);
};

}  // namespace zxdlss
