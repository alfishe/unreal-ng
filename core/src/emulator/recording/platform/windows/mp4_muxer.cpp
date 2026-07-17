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
    _timescale = fps * 1000;  // Use millisecond precision
    _frames.clear();
    _gotParams = false;
    _sps.clear();
    _pps.clear();
    _vps.clear();
    _nextDts = 0;
    _hasCtts = false;

    writeFtyp();

    // Write mdat header placeholder (will update size at end)
    _mdatStart = _file.tellp();
    uint8_t mdatHeader[8] = {0, 0, 0, 1, 'm', 'd', 'a', 't'};  // size=1 means 64-bit extended size follows
    _file.write(reinterpret_cast<char*>(mdatHeader), 8);
    // Write 64-bit size placeholder
    uint8_t size64[8] = {0};
    _file.write(reinterpret_cast<char*>(size64), 8);
    _mdatSize = 16;  // Header size

    return true;
}

void Mp4Muxer::writeFtyp()
{
    std::vector<uint8_t> data;
    writeStr(data, "isom");     // major brand
    writeU32(data, 0x200);      // minor version
    writeStr(data, "isom");     // compatible brands
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
    // Scan for NAL units in Annex-B format (00 00 00 01 or 00 00 01)
    size_t i = 0;
    while (i + 4 < size)
    {
        // Find start code
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

        // Find next start code or end
        size_t nalEnd = nalStart;
        while (nalEnd + 3 < size)
        {
            if ((data[nalEnd] == 0 && data[nalEnd+1] == 0 && data[nalEnd+2] == 1) ||
                (data[nalEnd] == 0 && data[nalEnd+1] == 0 && data[nalEnd+2] == 0 && nalEnd + 4 <= size && data[nalEnd+3] == 1))
                break;
            nalEnd++;
        }
        if (nalEnd + 3 >= size) nalEnd = size;

        // Remove trailing zeros
        while (nalEnd > nalStart && data[nalEnd-1] == 0) nalEnd--;

        if (nalEnd > nalStart)
        {
            uint8_t nalType;
            if (_codec == Codec::H264)
            {
                nalType = data[nalStart] & 0x1F;
                if (nalType == 7) // SPS
                    _sps.assign(data + nalStart, data + nalEnd);
                else if (nalType == 8) // PPS
                    _pps.assign(data + nalStart, data + nalEnd);
            }
            else // HEVC
            {
                nalType = (data[nalStart] >> 1) & 0x3F;
                if (nalType == 32) // VPS
                    _vps.assign(data + nalStart, data + nalEnd);
                else if (nalType == 33) // SPS
                    _sps.assign(data + nalStart, data + nalEnd);
                else if (nalType == 34) // PPS
                    _pps.assign(data + nalStart, data + nalEnd);
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
            // Skip parameter set NALUs (already in codec config)
            uint8_t nalType;
            if (_codec == Codec::H264)
                nalType = data[nalStart] & 0x1F;
            else
                nalType = (data[nalStart] >> 1) & 0x3F;

            bool isParamSet = (_codec == Codec::H264 && (nalType == 7 || nalType == 8)) ||
                              (_codec == Codec::HEVC && (nalType == 32 || nalType == 33 || nalType == 34));

            if (!isParamSet)
            {
                // Write 4-byte length prefix
                writeU32(result, static_cast<uint32_t>(nalLen));
                writeBytes(result, data + nalStart, nalLen);
            }
        }

        i = nalEnd;
    }

    return result;
}

bool Mp4Muxer::writeFrame(const uint8_t* data, size_t size, bool keyframe, int64_t pts)
{
    if (!_file.is_open()) return false;

    // Extract parameter sets from first keyframe
    if (!_gotParams && keyframe)
        extractParameterSets(data, size);

    // Convert Annex-B to AVCC/HVCC format (length-prefixed NALUs)
    std::vector<uint8_t> avccData = convertAnnexBToAvcc(data, size);
    if (avccData.empty()) return true;  // Only had parameter sets

    uint64_t offset = _file.tellp();
    _file.write(reinterpret_cast<const char*>(avccData.data()), avccData.size());

    // Assign sequential DTS, compute CTS offset (PTS - DTS)
    int64_t dts = _nextDts;
    _nextDts += 1000;  // Fixed duration per frame (timescale / fps)

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
    _frames.push_back(info);

    _mdatSize += avccData.size();

    return true;
}

bool Mp4Muxer::close()
{
    if (!_file.is_open()) return false;

    // If no frames or no parameter sets, just close (empty/invalid file)
    if (_frames.empty() || !_gotParams)
    {
        _file.close();
        return false;
    }

    // Update mdat size (64-bit extended size at offset mdatStart + 8)
    uint64_t mdatTotalSize = _mdatSize;
    _file.seekp(_mdatStart + 8);
    uint8_t size64[8];
    for (int i = 7; i >= 0; i--) {
        size64[7 - i] = (mdatTotalSize >> (i * 8)) & 0xFF;
    }
    _file.write(reinterpret_cast<char*>(size64), 8);

    // Seek to end and write moov
    _file.seekp(0, std::ios::end);
    writeMoov();

    _file.close();
    return true;
}

void Mp4Muxer::writeMoov()
{
    uint32_t duration = static_cast<uint32_t>(_frames.size()) * 1000;  // Duration in timescale units (1000 per frame at fps)
    uint32_t mediaDuration = static_cast<uint32_t>(_frames.size()) * 1000;

    // Build moov content
    std::vector<uint8_t> moov;

    // mvhd (movie header)
    {
        std::vector<uint8_t> mvhd;
        writeU8(mvhd, 0);  // version
        writeU8(mvhd, 0); writeU8(mvhd, 0); writeU8(mvhd, 0);  // flags
        writeU32(mvhd, 0);  // creation time
        writeU32(mvhd, 0);  // modification time
        writeU32(mvhd, _timescale);  // timescale
        writeU32(mvhd, duration);    // duration
        writeU32(mvhd, 0x00010000);  // rate (1.0)
        writeU16(mvhd, 0x0100);      // volume (1.0)
        writeU16(mvhd, 0);           // reserved
        writeU32(mvhd, 0); writeU32(mvhd, 0);  // reserved
        // Matrix (identity)
        writeU32(mvhd, 0x00010000); writeU32(mvhd, 0); writeU32(mvhd, 0);
        writeU32(mvhd, 0); writeU32(mvhd, 0x00010000); writeU32(mvhd, 0);
        writeU32(mvhd, 0); writeU32(mvhd, 0); writeU32(mvhd, 0x40000000);
        // Pre-defined
        for (int i = 0; i < 6; i++) writeU32(mvhd, 0);
        writeU32(mvhd, 2);  // next track ID

        writeU32(moov, static_cast<uint32_t>(8 + mvhd.size()));
        writeStr(moov, "mvhd");
        writeBytes(moov, mvhd.data(), mvhd.size());
    }

    // trak (track)
    {
        std::vector<uint8_t> trak;

        // tkhd (track header)
        {
            std::vector<uint8_t> tkhd;
            writeU8(tkhd, 0);  // version
            writeU8(tkhd, 0); writeU8(tkhd, 0); writeU8(tkhd, 3);  // flags (track enabled, in movie)
            writeU32(tkhd, 0);  // creation time
            writeU32(tkhd, 0);  // modification time
            writeU32(tkhd, 1);  // track ID
            writeU32(tkhd, 0);  // reserved
            writeU32(tkhd, duration);  // duration
            writeU32(tkhd, 0); writeU32(tkhd, 0);  // reserved
            writeU16(tkhd, 0);   // layer
            writeU16(tkhd, 0);   // alternate group
            writeU16(tkhd, 0);   // volume
            writeU16(tkhd, 0);   // reserved
            // Matrix (identity)
            writeU32(tkhd, 0x00010000); writeU32(tkhd, 0); writeU32(tkhd, 0);
            writeU32(tkhd, 0); writeU32(tkhd, 0x00010000); writeU32(tkhd, 0);
            writeU32(tkhd, 0); writeU32(tkhd, 0); writeU32(tkhd, 0x40000000);
            writeU32(tkhd, _width << 16);   // width (16.16 fixed point)
            writeU32(tkhd, _height << 16);  // height

            writeU32(trak, static_cast<uint32_t>(8 + tkhd.size()));
            writeStr(trak, "tkhd");
            writeBytes(trak, tkhd.data(), tkhd.size());
        }

        // mdia (media)
        {
            std::vector<uint8_t> mdia;

            // mdhd (media header)
            {
                std::vector<uint8_t> mdhd;
                writeU8(mdhd, 0);  // version
                writeU8(mdhd, 0); writeU8(mdhd, 0); writeU8(mdhd, 0);  // flags
                writeU32(mdhd, 0);  // creation time
                writeU32(mdhd, 0);  // modification time
                writeU32(mdhd, _timescale);  // timescale
                writeU32(mdhd, mediaDuration);  // duration
                writeU16(mdhd, 0x55C4);  // language (und)
                writeU16(mdhd, 0);       // quality

                writeU32(mdia, static_cast<uint32_t>(8 + mdhd.size()));
                writeStr(mdia, "mdhd");
                writeBytes(mdia, mdhd.data(), mdhd.size());
            }

            // hdlr (handler)
            {
                std::vector<uint8_t> hdlr;
                writeU8(hdlr, 0);  // version
                writeU8(hdlr, 0); writeU8(hdlr, 0); writeU8(hdlr, 0);  // flags
                writeU32(hdlr, 0);         // pre-defined
                writeStr(hdlr, "vide");    // handler type
                writeU32(hdlr, 0); writeU32(hdlr, 0); writeU32(hdlr, 0);  // reserved
                writeStr(hdlr, "VideoHandler");
                writeU8(hdlr, 0);  // null terminator

                writeU32(mdia, static_cast<uint32_t>(8 + hdlr.size()));
                writeStr(mdia, "hdlr");
                writeBytes(mdia, hdlr.data(), hdlr.size());
            }

            // minf (media information)
            {
                std::vector<uint8_t> minf;

                // vmhd (video media header)
                {
                    std::vector<uint8_t> vmhd;
                    writeU8(vmhd, 0);  // version
                    writeU8(vmhd, 0); writeU8(vmhd, 0); writeU8(vmhd, 1);  // flags
                    writeU16(vmhd, 0);  // graphics mode
                    writeU16(vmhd, 0); writeU16(vmhd, 0); writeU16(vmhd, 0);  // opcolor

                    writeU32(minf, static_cast<uint32_t>(8 + vmhd.size()));
                    writeStr(minf, "vmhd");
                    writeBytes(minf, vmhd.data(), vmhd.size());
                }

                // dinf (data information)
                {
                    std::vector<uint8_t> dinf;
                    // dref
                    std::vector<uint8_t> dref;
                    writeU8(dref, 0);  // version
                    writeU8(dref, 0); writeU8(dref, 0); writeU8(dref, 0);  // flags
                    writeU32(dref, 1);  // entry count
                    // url entry
                    writeU32(dref, 12);  // size
                    writeStr(dref, "url ");
                    writeU8(dref, 0);  // version
                    writeU8(dref, 0); writeU8(dref, 0); writeU8(dref, 1);  // flags (self-contained)

                    writeU32(dinf, static_cast<uint32_t>(8 + dref.size()));
                    writeStr(dinf, "dref");
                    writeBytes(dinf, dref.data(), dref.size());

                    writeU32(minf, static_cast<uint32_t>(8 + dinf.size()));
                    writeStr(minf, "dinf");
                    writeBytes(minf, dinf.data(), dinf.size());
                }

                // stbl (sample table)
                {
                    std::vector<uint8_t> stbl;

                    // stsd (sample description)
                    {
                        std::vector<uint8_t> stsd;
                        writeU8(stsd, 0);  // version
                        writeU8(stsd, 0); writeU8(stsd, 0); writeU8(stsd, 0);  // flags
                        writeU32(stsd, 1);  // entry count

                        // avc1 or hev1 entry
                        std::vector<uint8_t> codecEntry;
                        writeU32(codecEntry, 0); writeU16(codecEntry, 0);  // reserved
                        writeU16(codecEntry, 1);  // data reference index
                        writeU16(codecEntry, 0);  // pre-defined
                        writeU16(codecEntry, 0);  // reserved
                        writeU32(codecEntry, 0); writeU32(codecEntry, 0); writeU32(codecEntry, 0);  // pre-defined
                        writeU16(codecEntry, _width);
                        writeU16(codecEntry, _height);
                        writeU32(codecEntry, 0x00480000);  // horiz resolution (72 dpi)
                        writeU32(codecEntry, 0x00480000);  // vert resolution
                        writeU32(codecEntry, 0);  // reserved
                        writeU16(codecEntry, 1);  // frame count
                        // Compressor name (32 bytes)
                        for (int i = 0; i < 32; i++) writeU8(codecEntry, 0);
                        writeU16(codecEntry, 0x0018);  // depth (24-bit)
                        writeU16(codecEntry, 0xFFFF);  // pre-defined (-1)

                        // avcC or hvcC
                        if (_codec == Codec::H264)
                        {
                            std::vector<uint8_t> avcC;
                            writeU8(avcC, 1);  // version
                            writeU8(avcC, _sps.size() > 1 ? _sps[1] : 0x64);  // profile
                            writeU8(avcC, _sps.size() > 2 ? _sps[2] : 0x00);  // profile compat
                            writeU8(avcC, _sps.size() > 3 ? _sps[3] : 0x1F);  // level
                            writeU8(avcC, 0xFF);  // 6 bits reserved + 2 bits NAL size minus 1 (3 = 4 bytes)
                            writeU8(avcC, 0xE1);  // 3 bits reserved + 5 bits SPS count (1)
                            writeU16(avcC, static_cast<uint16_t>(_sps.size()));
                            writeBytes(avcC, _sps.data(), _sps.size());
                            writeU8(avcC, 1);  // PPS count
                            writeU16(avcC, static_cast<uint16_t>(_pps.size()));
                            writeBytes(avcC, _pps.data(), _pps.size());

                            writeU32(codecEntry, static_cast<uint32_t>(8 + avcC.size()));
                            writeStr(codecEntry, "avcC");
                            writeBytes(codecEntry, avcC.data(), avcC.size());
                        }
                        else // HEVC
                        {
                            // Build HEVCDecoderConfigurationRecord (ISO/IEC 14496-15)
                            std::vector<uint8_t> hvcC;
                            writeU8(hvcC, 1);    // configurationVersion

                            // Profile/tier/level - use defaults (Main profile, level 3.1)
                            // Proper parsing requires bit-level VPS/SPS decoding
                            writeU8(hvcC, 0x01);       // profile_space(2)=0 + tier_flag(1)=0 + profile_idc(5)=1 (Main)
                            writeU32(hvcC, 0x60000000);  // profile_compatibility_flags
                            writeU16(hvcC, 0x0000);      // constraint_indicator_flags (48 bits: 16 + 32)
                            writeU32(hvcC, 0x00000000);
                            writeU8(hvcC, 0x5D);         // level_idc = 93 (Level 3.1)

                            writeU16(hvcC, 0xF000);  // min_spatial_segmentation_idc with reserved F
                            writeU8(hvcC, 0xFC);     // parallelismType with reserved FC
                            writeU8(hvcC, 0xFD);     // chromaFormat (4:2:0) with reserved FC
                            writeU8(hvcC, 0xF8);     // bitDepthLumaMinus8 with reserved F8
                            writeU8(hvcC, 0xF8);     // bitDepthChromaMinus8 with reserved F8
                            writeU16(hvcC, 0);       // avgFrameRate
                            writeU8(hvcC, 0x03);     // constantFrameRate + numTemporalLayers + temporalIdNested + lengthSizeMinusOne(3)
                            writeU8(hvcC, 3);        // numOfArrays

                            // VPS array
                            writeU8(hvcC, 0xA0);     // array_completeness=1 + NAL_unit_type=32 (VPS)
                            writeU16(hvcC, 1);       // numNalus
                            writeU16(hvcC, static_cast<uint16_t>(_vps.size()));
                            writeBytes(hvcC, _vps.data(), _vps.size());

                            // SPS array
                            writeU8(hvcC, 0xA1);     // array_completeness=1 + NAL_unit_type=33 (SPS)
                            writeU16(hvcC, 1);       // numNalus
                            writeU16(hvcC, static_cast<uint16_t>(_sps.size()));
                            writeBytes(hvcC, _sps.data(), _sps.size());

                            // PPS array
                            writeU8(hvcC, 0xA2);     // array_completeness=1 + NAL_unit_type=34 (PPS)
                            writeU16(hvcC, 1);       // numNalus
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

                    // stts (decoding time to sample)
                    {
                        std::vector<uint8_t> stts;
                        writeU8(stts, 0);  // version
                        writeU8(stts, 0); writeU8(stts, 0); writeU8(stts, 0);  // flags
                        writeU32(stts, 1);  // entry count
                        writeU32(stts, static_cast<uint32_t>(_frames.size()));  // sample count
                        writeU32(stts, 1000);  // sample delta (timescale / fps = 1000)

                        writeU32(stbl, static_cast<uint32_t>(8 + stts.size()));
                        writeStr(stbl, "stts");
                        writeBytes(stbl, stts.data(), stts.size());
                    }

                    // stss (sync sample - keyframes)
                    {
                        std::vector<uint32_t> keyframes;
                        for (size_t i = 0; i < _frames.size(); i++)
                            if (_frames[i].keyframe)
                                keyframes.push_back(static_cast<uint32_t>(i + 1));

                        if (!keyframes.empty())
                        {
                            std::vector<uint8_t> stss;
                            writeU8(stss, 0);  // version
                            writeU8(stss, 0); writeU8(stss, 0); writeU8(stss, 0);  // flags
                            writeU32(stss, static_cast<uint32_t>(keyframes.size()));
                            for (uint32_t kf : keyframes)
                                writeU32(stss, kf);

                            writeU32(stbl, static_cast<uint32_t>(8 + stss.size()));
                            writeStr(stbl, "stss");
                            writeBytes(stbl, stss.data(), stss.size());
                        }
                    }

                    // ctts (composition time to sample) - only if B-frames present
                    if (_hasCtts)
                    {
                        std::vector<uint8_t> ctts;
                        writeU8(ctts, 1);  // version 1 (signed offsets for B-frames)
                        writeU8(ctts, 0); writeU8(ctts, 0); writeU8(ctts, 0);  // flags
                        writeU32(ctts, static_cast<uint32_t>(_frames.size()));  // entry count
                        for (const auto& f : _frames)
                        {
                            writeU32(ctts, 1);  // sample count
                            writeU32(ctts, static_cast<uint32_t>(f.cttsOffset));  // sample offset
                        }

                        writeU32(stbl, static_cast<uint32_t>(8 + ctts.size()));
                        writeStr(stbl, "ctts");
                        writeBytes(stbl, ctts.data(), ctts.size());
                    }

                    // stsz (sample sizes)
                    {
                        std::vector<uint8_t> stsz;
                        writeU8(stsz, 0);  // version
                        writeU8(stsz, 0); writeU8(stsz, 0); writeU8(stsz, 0);  // flags
                        writeU32(stsz, 0);  // sample size (0 = variable)
                        writeU32(stsz, static_cast<uint32_t>(_frames.size()));
                        for (const auto& f : _frames)
                            writeU32(stsz, f.size);

                        writeU32(stbl, static_cast<uint32_t>(8 + stsz.size()));
                        writeStr(stbl, "stsz");
                        writeBytes(stbl, stsz.data(), stsz.size());
                    }

                    // stco or co64 (chunk offsets)
                    {
                        bool use64 = false;
                        for (const auto& f : _frames)
                            if (f.offset > 0xFFFFFFFF) { use64 = true; break; }

                        std::vector<uint8_t> stco;
                        writeU8(stco, 0);  // version
                        writeU8(stco, 0); writeU8(stco, 0); writeU8(stco, 0);  // flags
                        writeU32(stco, static_cast<uint32_t>(_frames.size()));
                        for (const auto& f : _frames)
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

                    // stsc (sample to chunk)
                    {
                        std::vector<uint8_t> stsc;
                        writeU8(stsc, 0);  // version
                        writeU8(stsc, 0); writeU8(stsc, 0); writeU8(stsc, 0);  // flags
                        writeU32(stsc, 1);  // entry count
                        writeU32(stsc, 1);  // first chunk
                        writeU32(stsc, 1);  // samples per chunk
                        writeU32(stsc, 1);  // sample description index

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

    // Write moov box
    uint32_t moovSize = static_cast<uint32_t>(8 + moov.size());
    uint8_t header[8];
    header[0] = (moovSize >> 24) & 0xFF; header[1] = (moovSize >> 16) & 0xFF;
    header[2] = (moovSize >> 8) & 0xFF; header[3] = moovSize & 0xFF;
    memcpy(header + 4, "moov", 4);
    _file.write(reinterpret_cast<char*>(header), 8);
    _file.write(reinterpret_cast<char*>(moov.data()), moov.size());
}
