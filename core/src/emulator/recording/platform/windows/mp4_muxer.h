#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

/// Minimal native MP4 muxer for H.264/HEVC Annex-B bitstreams
/// Writes ftyp, mdat, moov atoms in that order (moov at end for streaming writes)
class Mp4Muxer
{
public:
    enum class Codec { H264, HEVC };

    Mp4Muxer();
    ~Mp4Muxer();

    bool open(const std::string& filename, Codec codec, uint32_t width, uint32_t height, uint32_t fps);
    /// Write a frame with explicit PTS (presentation timestamp in timescale units)
    /// DTS is assigned sequentially; ctts stores (PTS - DTS) offset for B-frame reordering
    bool writeFrame(const uint8_t* data, size_t size, bool keyframe, int64_t pts);
    bool close();

private:
    void writeFtyp();
    void writeMoov();
    void writeBox(const char* type, const std::vector<uint8_t>& data);
    void writeU8(std::vector<uint8_t>& buf, uint8_t val);
    void writeU16(std::vector<uint8_t>& buf, uint16_t val);
    void writeU32(std::vector<uint8_t>& buf, uint32_t val);
    void writeU64(std::vector<uint8_t>& buf, uint64_t val);
    void writeBytes(std::vector<uint8_t>& buf, const void* data, size_t len);
    void writeStr(std::vector<uint8_t>& buf, const char* str);

    // Extract parameter sets from Annex-B bitstream
    bool extractParameterSets(const uint8_t* data, size_t size);
    std::vector<uint8_t> convertAnnexBToAvcc(const uint8_t* data, size_t size);

    std::ofstream _file;
    Codec _codec = Codec::H264;
    uint32_t _width = 0;
    uint32_t _height = 0;
    uint32_t _fps = 50;
    uint32_t _timescale = 0;

    // Track frame info for stts, stsz, stss, stco, ctts
    struct FrameInfo {
        uint64_t offset;
        uint32_t size;
        bool keyframe;
        int64_t pts;      // Presentation timestamp
        int64_t dts;      // Decode timestamp (sequential)
        int32_t cttsOffset; // Composition time offset (PTS - DTS)
    };
    std::vector<FrameInfo> _frames;
    uint64_t _mdatStart = 0;
    uint64_t _mdatSize = 0;
    int64_t _nextDts = 0;  // Next DTS to assign
    bool _hasCtts = false; // True if any ctts offset is non-zero

    // Parameter sets (SPS/PPS for H.264, VPS/SPS/PPS for HEVC)
    std::vector<uint8_t> _sps;
    std::vector<uint8_t> _pps;
    std::vector<uint8_t> _vps;  // HEVC only
    bool _gotParams = false;
};
