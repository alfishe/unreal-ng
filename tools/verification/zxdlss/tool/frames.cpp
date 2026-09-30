#include "frames.h"

#include <zstd.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace zxdlss
{
namespace
{

std::string slurp(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// The integer after "key": in a small JSON text (clip.json / meta.jsonl).
bool jsonInt(const std::string& text, const std::string& key, long long& value, size_t from = 0)
{
    const size_t k = text.find("\"" + key + "\"", from);
    if (k == std::string::npos)
        return false;
    const size_t colon = text.find(':', k);
    if (colon == std::string::npos)
        return false;
    value = std::stoll(text.substr(colon + 1));
    return true;
}

std::string chunkName(const char* prefix, uint32_t index)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%s_%04u.zst", prefix, index);
    return name;
}

bool decompress(const std::string& packed, std::vector<uint8_t>& out)
{
    const unsigned long long size = ZSTD_getFrameContentSize(packed.data(), packed.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN)
        return false;
    out.resize(size);
    const size_t got = ZSTD_decompress(out.data(), out.size(), packed.data(), packed.size());
    return !ZSTD_isError(got) && got == size;
}

struct ClipInfo
{
    int width = 0, height = 0, chunk = 0;
    uint64_t first = 0, frames = 0;
};

bool clipInfo(const std::string& dir, ClipInfo& info, std::string& error)
{
    const std::string json = slurp(std::filesystem::path(dir) / "clip.json");
    long long w, h, c, frames, from;
    if (json.find("\"version\": 2") == std::string::npos || !jsonInt(json, "width", w) || !jsonInt(json, "height", h) ||
        !jsonInt(json, "chunk", c) || !jsonInt(json, "frames", frames) || !jsonInt(json, "from", from))
    {
        error = "not an unreal-ng-clip v2: " + dir;
        return false;
    }
    if (json.find("\"planeb\"") == std::string::npos)
    {
        error = "clip has no plane B (export it with the zxdlss feature on)";
        return false;
    }
    info = {static_cast<int>(w), static_cast<int>(h), static_cast<int>(c), static_cast<uint64_t>(from),
            static_cast<uint64_t>(frames)};
    return true;
}

}  // namespace

bool clipRange(const std::string& dir, uint64_t& first, uint64_t& last, std::string& error)
{
    ClipInfo info;
    if (!clipInfo(dir, info, error))
        return false;
    first = info.first;
    last = info.first + info.frames - 1;
    return true;
}

std::string readClip(const std::string& dir, uint64_t from, uint64_t to, const FrameCallback& cb)
{
    ClipInfo info;
    std::string error;
    if (!clipInfo(dir, info, error))
        return error;
    if (from < info.first || to >= info.first + info.frames || from > to)
        return "frame range outside the clip [" + std::to_string(info.first) + ", " +
               std::to_string(info.first + info.frames - 1) + "]";
    const size_t px = static_cast<size_t>(info.width) * info.height;
    std::vector<uint8_t> chunk;
    int loaded = -1;
    SourceFrame f;
    f.width = info.width;
    f.height = info.height;
    f.planeB.resize(px);
    for (uint64_t frame = from; frame <= to; ++frame)
    {
        const uint64_t index = frame - info.first;
        const int c = static_cast<int>(index / info.chunk);
        if (c != loaded)
        {
            if (!decompress(slurp(std::filesystem::path(dir) / chunkName("planeb", c)), chunk))
                return "cannot read " + chunkName("planeb", c);
            loaded = c;
        }
        const size_t offset = (index % info.chunk) * px * sizeof(uint16_t);
        if (offset + px * sizeof(uint16_t) > chunk.size())
            return "chunk " + chunkName("planeb", c) + " is short";
        std::memcpy(f.planeB.data(), chunk.data() + offset, px * sizeof(uint16_t));
        f.frame = frame;
        if (!cb(f))
            break;
    }
    return {};
}

bool writeWav(const std::string& path, const std::vector<int16_t>& samples, uint32_t rate, std::string& error)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        error = "cannot write " + path;
        return false;
    }
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    out.write("RIFF", 4);
    u32(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);                 // PCM
    u16(2);                 // stereo
    u32(rate);
    u32(rate * 4);
    u16(4);
    u16(16);
    out.write("data", 4);
    u32(bytes);
    out.write(reinterpret_cast<const char*>(samples.data()), bytes);
    return static_cast<bool>(out);
}

// ---- video ------------------------------------------------------------------

bool VideoWriter::open(const std::string& path, int width, int height, int scale, std::string& error, double fps,
                       const std::string& wav)
{
    _w = width;
    _h = height;
    _scale = scale < 1 ? 1 : scale;
    std::ostringstream cmd;
    cmd.precision(10);
    cmd << "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s " << _w * _scale << "x" << _h * _scale
        << " -r " << fps << " -i -";
    if (!wav.empty())
        cmd << " -i \"" << wav << "\" -map 0:v -map 1:a -c:a aac -b:a 192k -shortest";
    cmd << " -c:v libx264 -crf 16 -pix_fmt yuv420p \"" << path << "\"";
    _pipe = popen(cmd.str().c_str(), "w");
    if (!_pipe)
    {
        error = "cannot start ffmpeg (is it on PATH?)";
        return false;
    }
    return true;
}

void VideoWriter::write(const uint8_t* rgb)
{
    if (!_pipe)
        return;
    if (_scale == 1)
    {
        fwrite(rgb, 1, static_cast<size_t>(_w) * _h * 3, _pipe);
        return;
    }
    const int W = _w * _scale;
    _scaled.resize(static_cast<size_t>(W) * _h * _scale * 3);
    for (int y = 0; y < _h * _scale; ++y)
        for (int x = 0; x < W; ++x)
            std::memcpy(&_scaled[(static_cast<size_t>(y) * W + x) * 3], &rgb[(static_cast<size_t>(y / _scale) * _w + x / _scale) * 3], 3);
    fwrite(_scaled.data(), 1, _scaled.size(), _pipe);
}

void VideoWriter::close()
{
    if (_pipe)
        pclose(_pipe);
    _pipe = nullptr;
}

VideoWriter::~VideoWriter()
{
    close();
}

// ---- dump -------------------------------------------------------------------

bool DumpWriter::open(const std::string& dir, int width, int height, uint64_t firstFrame, std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        error = "cannot create " + dir;
        return false;
    }
    _dir = dir;
    _w = width;
    _h = height;
    _first = firstFrame;
    _open = true;
    return true;
}

void DumpWriter::write(const uint8_t* rgb)
{
    const size_t n = static_cast<size_t>(_w) * _h * 3;
    _buf.insert(_buf.end(), rgb, rgb + n);
    ++_frames;
    std::string error;
    if (++_inChunk == 500)
        flush(error);
}

bool DumpWriter::flush(std::string& error)
{
    if (_inChunk == 0)
        return true;
    const size_t bound = ZSTD_compressBound(_buf.size());
    std::vector<uint8_t> packed(bound);
    const size_t size = ZSTD_compress(packed.data(), bound, _buf.data(), _buf.size(), 3);
    if (ZSTD_isError(size))
    {
        error = "zstd failed";
        return false;
    }
    std::ofstream out(std::filesystem::path(_dir) / chunkName("rgb", _chunk), std::ios::binary);
    out.write(reinterpret_cast<const char*>(packed.data()), static_cast<std::streamsize>(size));
    _buf.clear();
    _inChunk = 0;
    ++_chunk;
    return static_cast<bool>(out);
}

bool DumpWriter::close(std::string& error)
{
    if (!_open)
        return true;
    _open = false;
    if (!flush(error))
        return false;
    std::ofstream json(std::filesystem::path(_dir) / "dump.json");
    json << "{\n  \"format\": \"zxdlss-rgb-dump\",\n  \"width\": " << _w << ",\n  \"height\": " << _h
         << ",\n  \"from\": " << _first << ",\n  \"frames\": " << _frames << ",\n  \"chunk\": 500,\n"
         << "  \"files\": \"rgb_NNNN.zst (RGB8, frame after frame)\"\n}\n";
    return static_cast<bool>(json);
}

DumpWriter::~DumpWriter()
{
    std::string error;
    close(error);
}

}  // namespace zxdlss
