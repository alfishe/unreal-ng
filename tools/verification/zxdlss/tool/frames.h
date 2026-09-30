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
    int paperX = 48, paperY = 48;     ///< top-left of the 256 x 192 paper in this frame
    std::vector<uint16_t> planeB;
    /// TTD input only: the machine's sound while this frame was replayed -
    /// interleaved stereo int16 at 44.1 kHz, ~903 samples per Pentagon frame
    std::vector<int16_t> audio;
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
/// overscan: Pentagon overscan (384 x 304) cropped to 352 x 304 with the paper
/// horizontally centered (48 px each side) - the UI's Symmetric Horizontal viewport.
std::string readTtd(const std::string& path, const std::string& model, uint64_t from, uint64_t to, const FrameCallback& cb,
                    bool overscan = false);

/// The machine's sound of frames [from, to] of a TTD file, played continuously
/// (interleaved stereo int16, 44.1 kHz); min / max samples per frame reported.
/// resyncs: frames where the run had left the recording (an outside change the
/// session did not journal) and was put back onto it.
std::string readTtdAudio(const std::string& path, const std::string& model, uint64_t from, uint64_t to,
                         std::vector<int16_t>& samples, size_t& minPerFrame, size_t& maxPerFrame,
                         std::vector<uint64_t>* resyncs = nullptr);

/// First and last frame of a TTD file's session.
bool ttdRange(const std::string& path, const std::string& model, uint64_t& first, uint64_t& last, std::string& error);

/// Interleaved stereo int16 samples -> a 16-bit PCM WAV file.
bool writeWav(const std::string& path, const std::vector<int16_t>& samples, uint32_t rate, std::string& error);

/// RGB24 frames piped into ffmpeg -> H.264 mp4 at `fps`, optionally with a
/// WAV muxed in as AAC (the video frames and the sound share one clock).
class VideoWriter
{
public:
    bool open(const std::string& path, int width, int height, int scale, std::string& error, double fps = 50.0,
              const std::string& wav = {});
    void close();
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
