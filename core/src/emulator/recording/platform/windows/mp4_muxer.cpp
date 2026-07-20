#include "mp4_muxer.h"
#include <cstring>
#include <algorithm>

Mp4Muxer::Mp4Muxer() = default;
Mp4Muxer::~Mp4Muxer() { close(); }

void Mp4Muxer::writeU8(std::vector<uint8_t>& buf, uint8_t val) { buf.push_back(val); }
void Mp4Muxer::writeU16(std::vector<uint8_t>& buf, uint16_t val) { buf.push_back(val >> 8); buf.push_back(val & 0xFF); }
void Mp4Muxer::writeU32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back((val >> 24) & 0xFF); buf.push_back((val >> 16) & 0xFF);
    buf.push_back((val >> 8) & 0xFF); buf.push_back(val & 0xFF);
}
void Mp4Muxer::writeU64(std::vector<uint8_t>& buf, uint64_t val) {
    writeU32(buf, static_cast<uint32_t>(val >> 32));
    writeU32(buf, static_cast<uint32_t>(val & 0xFFFFFFFF));
}
void Mp4Muxer::writeBytes(std::vector<uint8_t>& buf, const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    buf.insert(buf.end(), p, p + len);
}
void Mp4Muxer::writeStr(std::vector<uint8_t>& buf, const char* str) {
    writeBytes(buf, str, strlen(str));
}

void Mp4Muxer::writeBox(const char* type, const std::vector<uint8_t>& data) {
    uint32_t size = static_cast<uint32_t>(8 + data.size());
    uint8_t header[8];
    header[0] = (size >> 24) & 0xFF; header[1] = (size >> 16) & 0xFF;
    header[2] = (size >> 8) & 0xFF; header[3] = size & 0xFF;
    memcpy(header + 4, type, 4);
    _file.write(reinterpret_cast<char*>(header), 8);
    if (!data.empty())
        _file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

bool Mp4Muxer::open(const std::string& filename, Codec codec, uint32_t width, uint32_t height, uint32_t fps)
{
    _file.open(filename, std::ios::binary);
    if (!_file.is_open()) return false;

    _codec = codec;
    _width = width;
    _height = height;
    _fps = fps;
    _timescale = fps * 1000;
    _videoFrames.clear();
    _audioFrames.clear();
    _gotParams = false;
    _sps.clear();
    _pps.clear();
    _vps.clear();
    _nextVideoDts = 0;
    _hasCtts = false;
    _hasAudio = false;

    writeFtyp();

    _mdatStart = _file.tellp();
    uint8_t mdatHeader[8] = {0, 0, 0, 1, 'm', 'd', 'a', 't'};
    _file.write(reinterpret_cast<char*>(mdatHeader), 8);
    uint8_t size64[8] = {0};
    _file.write(reinterpret_cast<char*>(size64), 8);
    _mdatSize = 16;

    return true;
}

void Mp4Muxer::enableAudio(uint32_t sampleRate, uint32_t channels, const std::vector<uint8_t>& audioSpecificConfig)
{
    _hasAudio = true;
    _audioSampleRate = sampleRate;
    _audioChannels = channels;
    _audioSpecificConfig = audioSpecificConfig;
}

void Mp4Muxer::writeFtyp()
{
    std::vector<uint8_t> data;
    writeStr(data, "isom");
    writeU32(data, 0x200);
    writeStr(data, "isom");
    writeStr(data, "iso2");
    if (_codec == Codec::H264)
        writeStr(data, "avc1");
    else
        writeStr(data, "hev1");
    writeStr(data, "mp41");
    writeBox("ftyp", data);
}

bool Mp4Muxer::extractParameterSets(const uint8_t* data, size_t size)
{
    size_t i = 0;
    while (i + 4 < size)
    {
        size_t startCodeLen = 0;
        if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1)
            startCodeLen = 4;
        else if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 1)
            startCodeLen = 3;
        else {
            i++;
            continue;
        }

        size_t nalStart = i + startCodeLen;
        size_t nalEnd = nalStart;
        while (nalEnd + 3 < size)
        {
            if ((data[nalEnd] == 0 && data[nalEnd+1] == 0 && data[nalEnd+2] == 1) ||
                (data[nalEnd] == 0 && data[nalEnd+1] == 0 && data[nalEnd+2] == 0 && nalEnd + 4 <= size && data[nalEnd+3] == 1))
                break;
            nalEnd++;
        }
        if (nalEnd + 3 >= size) nalEnd = size;
        while (nalEnd > nalStart && data[nalEnd-1] == 0) nalEnd--;

        if (nalEnd > nalStart)
        {
            uint8_t nalType;
            if (_codec == Codec::H264)
            {
                nalType = data[nalStart] & 0x1F;
                if (nalType == 7) _sps.assign(data + nalStart, data + nalEnd);
                else if (nalType == 8) _pps.assign(data + nalStart, data + nalEnd);
            }
            else
            {
                nalType = (data[nalStart] >> 1) & 0x3F;
                if (nalType == 32) _vps.assign(data + nalStart, data + nalEnd);
                else if (nalType == 33) _sps.assign(data + nalStart, data + nalEnd);
                else if (nalType == 34) _pps.assign(data + nalStart, data + nalEnd);
            }
        }
        i = nalEnd;
    }

    if (_codec == Codec::H264)
        _gotParams = !_sps.empty() && !_pps.empty();
    else
        _gotParams = !_vps.empty() && !_sps.empty() && !_pps.empty();

    return _gotParams;
}

std::vector<uint8_t> Mp4Muxer::convertAnnexBToAvcc(const uint8_t* data, size_t size)
{
    std::vector<uint8_t> result;
    size_t i = 0;

    while (i + 4 < size)
    {
        size_t startCodeLen = 0;
        if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1)
            startCodeLen = 4;
        else if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 1)
            startCodeLen = 3;
        else {
            i++;
            continue;
        }

        size_t nalStart = i + startCodeLen;
        size_t nalEnd = nalStart;

        while (nalEnd + 3 < size)
        {
            if ((data[nalEnd] == 0 && data[nalEnd+1] == 0 && data[nalEnd+2] == 1) ||
                (data[nalEnd] == 0 && data[nalEnd+1] == 0 && data[nalEnd+2] == 0 && nalEnd + 4 <= size && data[nalEnd+3] == 1))
                break;
            nalEnd++;
        }
        if (nalEnd + 3 >= size) nalEnd = size;
        while (nalEnd > nalStart && data[nalEnd-1] == 0) nalEnd--;

        size_t nalLen = nalEnd - nalStart;
        if (nalLen > 0)
        {
            uint8_t nalType;
            if (_codec == Codec::H264)
                nalType = data[nalStart] & 0x1F;
            else
                nalType = (data[nalStart] >> 1) & 0x3F;

            bool isParamSet = (_codec == Codec::H264 && (nalType == 7 || nalType == 8)) ||
                              (_codec == Codec::HEVC && (nalType == 32 || nalType == 33 || nalType == 34));

            if (!isParamSet)
            {
                writeU32(result, static_cast<uint32_t>(nalLen));
                writeBytes(result, data + nalStart, nalLen);
            }
        }
        i = nalEnd;
    }

    return result;
}

bool Mp4Muxer::writeVideoFrame(const uint8_t* data, size_t size, bool keyframe, int64_t pts)
{
    if (!_file.is_open()) return false;

    if (!_gotParams && keyframe)
        extractParameterSets(data, size);

    std::vector<uint8_t> avccData = convertAnnexBToAvcc(data, size);
    if (avccData.empty()) return true;

    uint64_t offset = _file.tellp();
    _file.write(reinterpret_cast<const char*>(avccData.data()), avccData.size());

    int64_t dts = _nextVideoDts;
    _nextVideoDts += 1000;

    int32_t cttsOffset = static_cast<int32_t>(pts - dts);
    if (cttsOffset != 0)
        _hasCtts = true;

    FrameInfo info;
    info.offset = offset;
    info.size = static_cast<uint32_t>(avccData.size());
    info.keyframe = keyframe;
    info.pts = pts;
    info.dts = dts;
    info.cttsOffset = cttsOffset;
    _videoFrames.push_back(info);

    _mdatSize += avccData.size();
    return true;
}

bool Mp4Muxer::writeAudioFrame(const uint8_t* data, size_t size, int64_t pts)
{
    if (!_file.is_open() || !_hasAudio) return false;

    uint64_t offset = _file.tellp();
    _file.write(reinterpret_cast<const char*>(data), size);

    AudioFrameInfo info;
    info.offset = offset;
    info.size = static_cast<uint32_t>(size);
    info.pts = pts;
    _audioFrames.push_back(info);

    _mdatSize += size;
    return true;
}

bool Mp4Muxer::close()
{
    if (!_file.is_open()) return false;

    if (_videoFrames.empty() || !_gotParams)
    {
        _file.close();
        return false;
    }

    uint64_t mdatTotalSize = _mdatSize;
    _file.seekp(_mdatStart + 8);
    uint8_t size64[8];
    for (int i = 7; i >= 0; i--)
        size64[7 - i] = (mdatTotalSize >> (i * 8)) & 0xFF;
    _file.write(reinterpret_cast<char*>(size64), 8);

    _file.seekp(0, std::ios::end);
    writeMoov();

    _file.close();
    return true;
}

void Mp4Muxer::writeMoov()
{
    uint32_t videoDuration = static_cast<uint32_t>(_videoFrames.size()) * 1000;
    uint32_t movieDuration = videoDuration;

    if (_hasAudio && !_audioFrames.empty())
    {
        uint64_t audioDurationSamples = _audioFrames.size() * 1024;
        uint32_t audioDurationMovie = static_cast<uint32_t>(audioDurationSamples * _timescale / _audioSampleRate);
        if (audioDurationMovie > movieDuration)
            movieDuration = audioDurationMovie;
    }

    uint32_t nextTrackId = _hasAudio ? 3 : 2;

    std::vector<uint8_t> moov;

    // mvhd
    {
        std::vector<uint8_t> mvhd;
        writeU8(mvhd, 0);
        writeU8(mvhd, 0); writeU8(mvhd, 0); writeU8(mvhd, 0);
        writeU32(mvhd, 0);
        writeU32(mvhd, 0);
        writeU32(mvhd, _timescale);
        writeU32(mvhd, movieDuration);
        writeU32(mvhd, 0x00010000);
        writeU16(mvhd, 0x0100);
        writeU16(mvhd, 0);
        writeU32(mvhd, 0); writeU32(mvhd, 0);
        writeU32(mvhd, 0x00010000); writeU32(mvhd, 0); writeU32(mvhd, 0);
        writeU32(mvhd, 0); writeU32(mvhd, 0x00010000); writeU32(mvhd, 0);
        writeU32(mvhd, 0); writeU32(mvhd, 0); writeU32(mvhd, 0x40000000);
        for (int i = 0; i < 6; i++) writeU32(mvhd, 0);
        writeU32(mvhd, nextTrackId);

        writeU32(moov, static_cast<uint32_t>(8 + mvhd.size()));
        writeStr(moov, "mvhd");
        writeBytes(moov, mvhd.data(), mvhd.size());
    }

    writeVideoTrak(moov, 1, videoDuration);

    if (_hasAudio && !_audioFrames.empty())
    {
        uint32_t audioDuration = static_cast<uint32_t>(_audioFrames.size() * 1024 * _timescale / _audioSampleRate);
        writeAudioTrak(moov, 2, audioDuration);
    }

    uint32_t moovSize = static_cast<uint32_t>(8 + moov.size());
    uint8_t header[8];
    header[0] = (moovSize >> 24) & 0xFF; header[1] = (moovSize >> 16) & 0xFF;
    header[2] = (moovSize >> 8) & 0xFF; header[3] = moovSize & 0xFF;
    memcpy(header + 4, "moov", 4);
    _file.write(reinterpret_cast<char*>(header), 8);
    _file.write(reinterpret_cast<char*>(moov.data()), moov.size());
}

void Mp4Muxer::writeVideoTrak(std::vector<uint8_t>& moov, uint32_t trackId, uint32_t duration)
{
    std::vector<uint8_t> trak;

    // tkhd
    {
        std::vector<uint8_t> tkhd;
        writeU8(tkhd, 0);
        writeU8(tkhd, 0); writeU8(tkhd, 0); writeU8(tkhd, 3);
        writeU32(tkhd, 0);
        writeU32(tkhd, 0);
        writeU32(tkhd, trackId);
        writeU32(tkhd, 0);
        writeU32(tkhd, duration);
        writeU32(tkhd, 0); writeU32(tkhd, 0);
        writeU16(tkhd, 0);
        writeU16(tkhd, 0);
        writeU16(tkhd, 0);
        writeU16(tkhd, 0);
        writeU32(tkhd, 0x00010000); writeU32(tkhd, 0); writeU32(tkhd, 0);
        writeU32(tkhd, 0); writeU32(tkhd, 0x00010000); writeU32(tkhd, 0);
        writeU32(tkhd, 0); writeU32(tkhd, 0); writeU32(tkhd, 0x40000000);
        writeU32(tkhd, _width << 16);
        writeU32(tkhd, _height << 16);

        writeU32(trak, static_cast<uint32_t>(8 + tkhd.size()));
        writeStr(trak, "tkhd");
        writeBytes(trak, tkhd.data(), tkhd.size());
    }

    // mdia
    {
        std::vector<uint8_t> mdia;

        // mdhd
        {
            std::vector<uint8_t> mdhd;
            writeU8(mdhd, 0);
            writeU8(mdhd, 0); writeU8(mdhd, 0); writeU8(mdhd, 0);
            writeU32(mdhd, 0);
            writeU32(mdhd, 0);
            writeU32(mdhd, _timescale);
            writeU32(mdhd, duration);
            writeU16(mdhd, 0x55C4);
            writeU16(mdhd, 0);

            writeU32(mdia, static_cast<uint32_t>(8 + mdhd.size()));
            writeStr(mdia, "mdhd");
            writeBytes(mdia, mdhd.data(), mdhd.size());
        }

        // hdlr
        {
            std::vector<uint8_t> hdlr;
            writeU8(hdlr, 0);
            writeU8(hdlr, 0); writeU8(hdlr, 0); writeU8(hdlr, 0);
            writeU32(hdlr, 0);
            writeStr(hdlr, "vide");
            writeU32(hdlr, 0); writeU32(hdlr, 0); writeU32(hdlr, 0);
            writeStr(hdlr, "VideoHandler");
            writeU8(hdlr, 0);

            writeU32(mdia, static_cast<uint32_t>(8 + hdlr.size()));
            writeStr(mdia, "hdlr");
            writeBytes(mdia, hdlr.data(), hdlr.size());
        }

        // minf
        {
            std::vector<uint8_t> minf;

            // vmhd
            {
                std::vector<uint8_t> vmhd;
                writeU8(vmhd, 0);
                writeU8(vmhd, 0); writeU8(vmhd, 0); writeU8(vmhd, 1);
                writeU16(vmhd, 0);
                writeU16(vmhd, 0); writeU16(vmhd, 0); writeU16(vmhd, 0);

                writeU32(minf, static_cast<uint32_t>(8 + vmhd.size()));
                writeStr(minf, "vmhd");
                writeBytes(minf, vmhd.data(), vmhd.size());
            }

            // dinf
            {
                std::vector<uint8_t> dinf;
                std::vector<uint8_t> dref;
                writeU8(dref, 0);
                writeU8(dref, 0); writeU8(dref, 0); writeU8(dref, 0);
                writeU32(dref, 1);
                writeU32(dref, 12);
                writeStr(dref, "url ");
                writeU8(dref, 0);
                writeU8(dref, 0); writeU8(dref, 0); writeU8(dref, 1);

                writeU32(dinf, static_cast<uint32_t>(8 + dref.size()));
                writeStr(dinf, "dref");
                writeBytes(dinf, dref.data(), dref.size());

                writeU32(minf, static_cast<uint32_t>(8 + dinf.size()));
                writeStr(minf, "dinf");
                writeBytes(minf, dinf.data(), dinf.size());
            }

            // stbl
            {
                std::vector<uint8_t> stbl;

                // stsd
                {
                    std::vector<uint8_t> stsd;
                    writeU8(stsd, 0);
                    writeU8(stsd, 0); writeU8(stsd, 0); writeU8(stsd, 0);
                    writeU32(stsd, 1);

                    std::vector<uint8_t> codecEntry;
                    writeU32(codecEntry, 0); writeU16(codecEntry, 0);
                    writeU16(codecEntry, 1);
                    writeU16(codecEntry, 0);
                    writeU16(codecEntry, 0);
                    writeU32(codecEntry, 0); writeU32(codecEntry, 0); writeU32(codecEntry, 0);
                    writeU16(codecEntry, _width);
                    writeU16(codecEntry, _height);
                    writeU32(codecEntry, 0x00480000);
                    writeU32(codecEntry, 0x00480000);
                    writeU32(codecEntry, 0);
                    writeU16(codecEntry, 1);
                    for (int i = 0; i < 32; i++) writeU8(codecEntry, 0);
                    writeU16(codecEntry, 0x0018);
                    writeU16(codecEntry, 0xFFFF);

                    if (_codec == Codec::H264)
                    {
                        std::vector<uint8_t> avcC;
                        writeU8(avcC, 1);
                        writeU8(avcC, _sps.size() > 1 ? _sps[1] : 0x64);
                        writeU8(avcC, _sps.size() > 2 ? _sps[2] : 0x00);
                        writeU8(avcC, _sps.size() > 3 ? _sps[3] : 0x1F);
                        writeU8(avcC, 0xFF);
                        writeU8(avcC, 0xE1);
                        writeU16(avcC, static_cast<uint16_t>(_sps.size()));
                        writeBytes(avcC, _sps.data(), _sps.size());
                        writeU8(avcC, 1);
                        writeU16(avcC, static_cast<uint16_t>(_pps.size()));
                        writeBytes(avcC, _pps.data(), _pps.size());

                        writeU32(codecEntry, static_cast<uint32_t>(8 + avcC.size()));
                        writeStr(codecEntry, "avcC");
                        writeBytes(codecEntry, avcC.data(), avcC.size());
                    }
                    else
                    {
                        std::vector<uint8_t> hvcC;
                        writeU8(hvcC, 1);
                        writeU8(hvcC, 0x01);
                        writeU32(hvcC, 0x60000000);
                        writeU16(hvcC, 0x0000);
                        writeU32(hvcC, 0x00000000);
                        writeU8(hvcC, 0x5D);
                        writeU16(hvcC, 0xF000);
                        writeU8(hvcC, 0xFC);
                        writeU8(hvcC, 0xFD);
                        writeU8(hvcC, 0xF8);
                        writeU8(hvcC, 0xF8);
                        writeU16(hvcC, 0);
                        writeU8(hvcC, 0x03);
                        writeU8(hvcC, 3);

                        writeU8(hvcC, 0xA0);
                        writeU16(hvcC, 1);
                        writeU16(hvcC, static_cast<uint16_t>(_vps.size()));
                        writeBytes(hvcC, _vps.data(), _vps.size());

                        writeU8(hvcC, 0xA1);
                        writeU16(hvcC, 1);
                        writeU16(hvcC, static_cast<uint16_t>(_sps.size()));
                        writeBytes(hvcC, _sps.data(), _sps.size());

                        writeU8(hvcC, 0xA2);
                        writeU16(hvcC, 1);
                        writeU16(hvcC, static_cast<uint16_t>(_pps.size()));
                        writeBytes(hvcC, _pps.data(), _pps.size());

                        writeU32(codecEntry, static_cast<uint32_t>(8 + hvcC.size()));
                        writeStr(codecEntry, "hvcC");
                        writeBytes(codecEntry, hvcC.data(), hvcC.size());
                    }

                    uint32_t entrySize = static_cast<uint32_t>(8 + codecEntry.size());
                    writeU32(stsd, entrySize);
                    writeStr(stsd, _codec == Codec::H264 ? "avc1" : "hev1");
                    writeBytes(stsd, codecEntry.data(), codecEntry.size());

                    writeU32(stbl, static_cast<uint32_t>(8 + stsd.size()));
                    writeStr(stbl, "stsd");
                    writeBytes(stbl, stsd.data(), stsd.size());
                }

                // stts
                {
                    std::vector<uint8_t> stts;
                    writeU8(stts, 0);
                    writeU8(stts, 0); writeU8(stts, 0); writeU8(stts, 0);
                    writeU32(stts, 1);
                    writeU32(stts, static_cast<uint32_t>(_videoFrames.size()));
                    writeU32(stts, 1000);

                    writeU32(stbl, static_cast<uint32_t>(8 + stts.size()));
                    writeStr(stbl, "stts");
                    writeBytes(stbl, stts.data(), stts.size());
                }

                // stss
                {
                    std::vector<uint32_t> keyframes;
                    for (size_t i = 0; i < _videoFrames.size(); i++)
                        if (_videoFrames[i].keyframe)
                            keyframes.push_back(static_cast<uint32_t>(i + 1));

                    if (!keyframes.empty())
                    {
                        std::vector<uint8_t> stss;
                        writeU8(stss, 0);
                        writeU8(stss, 0); writeU8(stss, 0); writeU8(stss, 0);
                        writeU32(stss, static_cast<uint32_t>(keyframes.size()));
                        for (uint32_t kf : keyframes)
                            writeU32(stss, kf);

                        writeU32(stbl, static_cast<uint32_t>(8 + stss.size()));
                        writeStr(stbl, "stss");
                        writeBytes(stbl, stss.data(), stss.size());
                    }
                }

                // ctts
                if (_hasCtts)
                {
                    std::vector<uint8_t> ctts;
                    writeU8(ctts, 1);
                    writeU8(ctts, 0); writeU8(ctts, 0); writeU8(ctts, 0);
                    writeU32(ctts, static_cast<uint32_t>(_videoFrames.size()));
                    for (const auto& f : _videoFrames)
                    {
                        writeU32(ctts, 1);
                        writeU32(ctts, static_cast<uint32_t>(f.cttsOffset));
                    }

                    writeU32(stbl, static_cast<uint32_t>(8 + ctts.size()));
                    writeStr(stbl, "ctts");
                    writeBytes(stbl, ctts.data(), ctts.size());
                }

                // stsz
                {
                    std::vector<uint8_t> stsz;
                    writeU8(stsz, 0);
                    writeU8(stsz, 0); writeU8(stsz, 0); writeU8(stsz, 0);
                    writeU32(stsz, 0);
                    writeU32(stsz, static_cast<uint32_t>(_videoFrames.size()));
                    for (const auto& f : _videoFrames)
                        writeU32(stsz, f.size);

                    writeU32(stbl, static_cast<uint32_t>(8 + stsz.size()));
                    writeStr(stbl, "stsz");
                    writeBytes(stbl, stsz.data(), stsz.size());
                }

                // stco/co64
                {
                    bool use64 = false;
                    for (const auto& f : _videoFrames)
                        if (f.offset > 0xFFFFFFFF) { use64 = true; break; }

                    std::vector<uint8_t> stco;
                    writeU8(stco, 0);
                    writeU8(stco, 0); writeU8(stco, 0); writeU8(stco, 0);
                    writeU32(stco, static_cast<uint32_t>(_videoFrames.size()));
                    for (const auto& f : _videoFrames)
                    {
                        if (use64)
                            writeU64(stco, f.offset);
                        else
                            writeU32(stco, static_cast<uint32_t>(f.offset));
                    }

                    writeU32(stbl, static_cast<uint32_t>(8 + stco.size()));
                    writeStr(stbl, use64 ? "co64" : "stco");
                    writeBytes(stbl, stco.data(), stco.size());
                }

                // stsc
                {
                    std::vector<uint8_t> stsc;
                    writeU8(stsc, 0);
                    writeU8(stsc, 0); writeU8(stsc, 0); writeU8(stsc, 0);
                    writeU32(stsc, 1);
                    writeU32(stsc, 1);
                    writeU32(stsc, 1);
                    writeU32(stsc, 1);

                    writeU32(stbl, static_cast<uint32_t>(8 + stsc.size()));
                    writeStr(stbl, "stsc");
                    writeBytes(stbl, stsc.data(), stsc.size());
                }

                writeU32(minf, static_cast<uint32_t>(8 + stbl.size()));
                writeStr(minf, "stbl");
                writeBytes(minf, stbl.data(), stbl.size());
            }

            writeU32(mdia, static_cast<uint32_t>(8 + minf.size()));
            writeStr(mdia, "minf");
            writeBytes(mdia, minf.data(), minf.size());
        }

        writeU32(trak, static_cast<uint32_t>(8 + mdia.size()));
        writeStr(trak, "mdia");
        writeBytes(trak, mdia.data(), mdia.size());
    }

    writeU32(moov, static_cast<uint32_t>(8 + trak.size()));
    writeStr(moov, "trak");
    writeBytes(moov, trak.data(), trak.size());
}

void Mp4Muxer::writeAudioTrak(std::vector<uint8_t>& moov, uint32_t trackId, uint32_t duration)
{
    std::vector<uint8_t> trak;

    // tkhd
    {
        std::vector<uint8_t> tkhd;
        writeU8(tkhd, 0);
        writeU8(tkhd, 0); writeU8(tkhd, 0); writeU8(tkhd, 3);
        writeU32(tkhd, 0);
        writeU32(tkhd, 0);
        writeU32(tkhd, trackId);
        writeU32(tkhd, 0);
        writeU32(tkhd, duration);
        writeU32(tkhd, 0); writeU32(tkhd, 0);
        writeU16(tkhd, 0);
        writeU16(tkhd, 1);  // alternate group
        writeU16(tkhd, 0x0100);  // volume (1.0)
        writeU16(tkhd, 0);
        writeU32(tkhd, 0x00010000); writeU32(tkhd, 0); writeU32(tkhd, 0);
        writeU32(tkhd, 0); writeU32(tkhd, 0x00010000); writeU32(tkhd, 0);
        writeU32(tkhd, 0); writeU32(tkhd, 0); writeU32(tkhd, 0x40000000);
        writeU32(tkhd, 0);  // width
        writeU32(tkhd, 0);  // height

        writeU32(trak, static_cast<uint32_t>(8 + tkhd.size()));
        writeStr(trak, "tkhd");
        writeBytes(trak, tkhd.data(), tkhd.size());
    }

    // mdia
    {
        std::vector<uint8_t> mdia;

        // mdhd
        {
            std::vector<uint8_t> mdhd;
            writeU8(mdhd, 0);
            writeU8(mdhd, 0); writeU8(mdhd, 0); writeU8(mdhd, 0);
            writeU32(mdhd, 0);
            writeU32(mdhd, 0);
            writeU32(mdhd, _audioSampleRate);  // Audio timescale = sample rate
            writeU32(mdhd, static_cast<uint32_t>(_audioFrames.size() * 1024));  // Duration in samples
            writeU16(mdhd, 0x55C4);
            writeU16(mdhd, 0);

            writeU32(mdia, static_cast<uint32_t>(8 + mdhd.size()));
            writeStr(mdia, "mdhd");
            writeBytes(mdia, mdhd.data(), mdhd.size());
        }

        // hdlr
        {
            std::vector<uint8_t> hdlr;
            writeU8(hdlr, 0);
            writeU8(hdlr, 0); writeU8(hdlr, 0); writeU8(hdlr, 0);
            writeU32(hdlr, 0);
            writeStr(hdlr, "soun");
            writeU32(hdlr, 0); writeU32(hdlr, 0); writeU32(hdlr, 0);
            writeStr(hdlr, "SoundHandler");
            writeU8(hdlr, 0);

            writeU32(mdia, static_cast<uint32_t>(8 + hdlr.size()));
            writeStr(mdia, "hdlr");
            writeBytes(mdia, hdlr.data(), hdlr.size());
        }

        // minf
        {
            std::vector<uint8_t> minf;

            // smhd
            {
                std::vector<uint8_t> smhd;
                writeU8(smhd, 0);
                writeU8(smhd, 0); writeU8(smhd, 0); writeU8(smhd, 0);
                writeU16(smhd, 0);  // balance
                writeU16(smhd, 0);  // reserved

                writeU32(minf, static_cast<uint32_t>(8 + smhd.size()));
                writeStr(minf, "smhd");
                writeBytes(minf, smhd.data(), smhd.size());
            }

            // dinf
            {
                std::vector<uint8_t> dinf;
                std::vector<uint8_t> dref;
                writeU8(dref, 0);
                writeU8(dref, 0); writeU8(dref, 0); writeU8(dref, 0);
                writeU32(dref, 1);
                writeU32(dref, 12);
                writeStr(dref, "url ");
                writeU8(dref, 0);
                writeU8(dref, 0); writeU8(dref, 0); writeU8(dref, 1);

                writeU32(dinf, static_cast<uint32_t>(8 + dref.size()));
                writeStr(dinf, "dref");
                writeBytes(dinf, dref.data(), dref.size());

                writeU32(minf, static_cast<uint32_t>(8 + dinf.size()));
                writeStr(minf, "dinf");
                writeBytes(minf, dinf.data(), dinf.size());
            }

            // stbl
            {
                std::vector<uint8_t> stbl;

                // stsd with mp4a
                {
                    std::vector<uint8_t> stsd;
                    writeU8(stsd, 0);
                    writeU8(stsd, 0); writeU8(stsd, 0); writeU8(stsd, 0);
                    writeU32(stsd, 1);

                    std::vector<uint8_t> mp4a;
                    writeU32(mp4a, 0); writeU16(mp4a, 0);  // reserved
                    writeU16(mp4a, 1);  // data reference index
                    writeU16(mp4a, 0);  // version
                    writeU16(mp4a, 0);  // revision
                    writeU32(mp4a, 0);  // vendor
                    writeU16(mp4a, _audioChannels);
                    writeU16(mp4a, 16);  // bits per sample
                    writeU16(mp4a, 0);   // compression ID
                    writeU16(mp4a, 0);   // packet size
                    writeU32(mp4a, _audioSampleRate << 16);  // sample rate (16.16 fixed)

                    // esds box
                    std::vector<uint8_t> esds;
                    writeU8(esds, 0);  // version
                    writeU8(esds, 0); writeU8(esds, 0); writeU8(esds, 0);  // flags

                    // ES_Descriptor
                    writeU8(esds, 0x03);  // ES_DescrTag
                    size_t esDescSize = 23 + _audioSpecificConfig.size();
                    writeU8(esds, static_cast<uint8_t>(esDescSize));
                    writeU16(esds, 0);  // ES_ID
                    writeU8(esds, 0);   // flags

                    // DecoderConfigDescriptor
                    writeU8(esds, 0x04);  // DecoderConfigDescrTag
                    writeU8(esds, static_cast<uint8_t>(15 + _audioSpecificConfig.size()));
                    writeU8(esds, 0x40);  // objectTypeIndication (AAC)
                    writeU8(esds, 0x15);  // streamType (audio) + upstream + reserved
                    writeU8(esds, 0); writeU8(esds, 0); writeU8(esds, 0);  // bufferSizeDB (24 bits)
                    writeU32(esds, 128000);  // maxBitrate
                    writeU32(esds, 128000);  // avgBitrate

                    // DecoderSpecificInfo (AudioSpecificConfig)
                    writeU8(esds, 0x05);  // DecSpecificInfoTag
                    writeU8(esds, static_cast<uint8_t>(_audioSpecificConfig.size()));
                    writeBytes(esds, _audioSpecificConfig.data(), _audioSpecificConfig.size());

                    // SLConfigDescriptor
                    writeU8(esds, 0x06);  // SLConfigDescrTag
                    writeU8(esds, 1);     // length
                    writeU8(esds, 0x02);  // predefined

                    writeU32(mp4a, static_cast<uint32_t>(8 + esds.size()));
                    writeStr(mp4a, "esds");
                    writeBytes(mp4a, esds.data(), esds.size());

                    uint32_t mp4aSize = static_cast<uint32_t>(8 + mp4a.size());
                    writeU32(stsd, mp4aSize);
                    writeStr(stsd, "mp4a");
                    writeBytes(stsd, mp4a.data(), mp4a.size());

                    writeU32(stbl, static_cast<uint32_t>(8 + stsd.size()));
                    writeStr(stbl, "stsd");
                    writeBytes(stbl, stsd.data(), stsd.size());
                }

                // stts (1024 samples per AAC frame)
                {
                    std::vector<uint8_t> stts;
                    writeU8(stts, 0);
                    writeU8(stts, 0); writeU8(stts, 0); writeU8(stts, 0);
                    writeU32(stts, 1);
                    writeU32(stts, static_cast<uint32_t>(_audioFrames.size()));
                    writeU32(stts, 1024);

                    writeU32(stbl, static_cast<uint32_t>(8 + stts.size()));
                    writeStr(stbl, "stts");
                    writeBytes(stbl, stts.data(), stts.size());
                }

                // stsz
                {
                    std::vector<uint8_t> stsz;
                    writeU8(stsz, 0);
                    writeU8(stsz, 0); writeU8(stsz, 0); writeU8(stsz, 0);
                    writeU32(stsz, 0);
                    writeU32(stsz, static_cast<uint32_t>(_audioFrames.size()));
                    for (const auto& f : _audioFrames)
                        writeU32(stsz, f.size);

                    writeU32(stbl, static_cast<uint32_t>(8 + stsz.size()));
                    writeStr(stbl, "stsz");
                    writeBytes(stbl, stsz.data(), stsz.size());
                }

                // stco/co64
                {
                    bool use64 = false;
                    for (const auto& f : _audioFrames)
                        if (f.offset > 0xFFFFFFFF) { use64 = true; break; }

                    std::vector<uint8_t> stco;
                    writeU8(stco, 0);
                    writeU8(stco, 0); writeU8(stco, 0); writeU8(stco, 0);
                    writeU32(stco, static_cast<uint32_t>(_audioFrames.size()));
                    for (const auto& f : _audioFrames)
                    {
                        if (use64)
                            writeU64(stco, f.offset);
                        else
                            writeU32(stco, static_cast<uint32_t>(f.offset));
                    }

                    writeU32(stbl, static_cast<uint32_t>(8 + stco.size()));
                    writeStr(stbl, use64 ? "co64" : "stco");
                    writeBytes(stbl, stco.data(), stco.size());
                }

                // stsc
                {
                    std::vector<uint8_t> stsc;
                    writeU8(stsc, 0);
                    writeU8(stsc, 0); writeU8(stsc, 0); writeU8(stsc, 0);
                    writeU32(stsc, 1);
                    writeU32(stsc, 1);
                    writeU32(stsc, 1);
                    writeU32(stsc, 1);

                    writeU32(stbl, static_cast<uint32_t>(8 + stsc.size()));
                    writeStr(stbl, "stsc");
                    writeBytes(stbl, stsc.data(), stsc.size());
                }

                writeU32(minf, static_cast<uint32_t>(8 + stbl.size()));
                writeStr(minf, "stbl");
                writeBytes(minf, stbl.data(), stbl.size());
            }

            writeU32(mdia, static_cast<uint32_t>(8 + minf.size()));
            writeStr(mdia, "minf");
            writeBytes(mdia, minf.data(), minf.size());
        }

        writeU32(trak, static_cast<uint32_t>(8 + mdia.size()));
        writeStr(trak, "mdia");
        writeBytes(trak, mdia.data(), mdia.size());
    }

    writeU32(moov, static_cast<uint32_t>(8 + trak.size()));
    writeStr(moov, "trak");
    writeBytes(moov, trak.data(), trak.size());
}
