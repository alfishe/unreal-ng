#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

/// Native MP4 muxer for H.264/HEVC video with optional AAC audio
/// Writes ftyp, mdat, moov atoms in that order (moov at end for streaming writes)
class Mp4Muxer
{
public:
    enum class Codec { H264, HEVC };

    Mp4Muxer();
    ~Mp4Muxer();

    bool open(const std::string& filename, Codec codec, uint32_t width, uint32_t height, uint32_t fps);

    /// Enable audio track (call before writing any frames)
    /// @param sampleRate Audio sample rate (e.g., 44100)
    /// @param channels Number of channels (1 or 2)
    /// @param audioSpecificConfig AAC AudioSpecificConfig (2 bytes for AAC-LC)
    void enableAudio(uint32_t sampleRate, uint32_t channels, const std::vector<uint8_t>& audioSpecificConfig);

    /// Write a video frame with explicit PTS (presentation timestamp in timescale units)
    /// DTS is assigned sequentially; ctts stores (PTS - DTS) offset for B-frame reordering
    bool writeVideoFrame(const uint8_t* data, size_t size, bool keyframe, int64_t pts);

    /// Write an audio frame (raw AAC without ADTS header)
    /// @param data AAC frame data
    /// @param size Frame size in bytes
    /// @param pts Presentation timestamp in audio samples
    bool writeAudioFrame(const uint8_t* data, size_t size, int64_t pts);

    /// Legacy alias for writeVideoFrame
    bool writeFrame(const uint8_t* data, size_t size, bool keyframe, int64_t pts)
    { return writeVideoFrame(data, size, keyframe, pts); }

    bool close();

private:
    void writeFtyp();
    void writeMoov();
    void writeVideoTrak(std::vector<uint8_t>& moov, uint32_t trackId, uint32_t duration);
    void writeAudioTrak(std::vector<uint8_t>& moov, uint32_t trackId, uint32_t duration);
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

    // Video track
    struct FrameInfo {
        uint64_t offset;
        uint32_t size;
        bool keyframe;
        int64_t pts;
        int64_t dts;
        int32_t cttsOffset;
    };
    std::vector<FrameInfo> _videoFrames;
    int64_t _nextVideoDts = 0;
    bool _hasCtts = false;

    // Audio track
    struct AudioFrameInfo {
        uint64_t offset;
        uint32_t size;
        int64_t pts;  // In audio samples
    };
    std::vector<AudioFrameInfo> _audioFrames;
    bool _hasAudio = false;
    uint32_t _audioSampleRate = 0;
    uint32_t _audioChannels = 0;
    std::vector<uint8_t> _audioSpecificConfig;

    uint64_t _mdatStart = 0;
    uint64_t _mdatSize = 0;

    // Video parameter sets (SPS/PPS for H.264, VPS/SPS/PPS for HEVC)
    std::vector<uint8_t> _sps;
    std::vector<uint8_t> _pps;
    std::vector<uint8_t> _vps;
    bool _gotParams = false;
};
