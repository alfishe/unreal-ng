// libsam2695 tools - Standard MIDI File reader.
#include "smfreader.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace sam2695tools
{

namespace
{

struct RawEvent
{
    uint64_t tick = 0;
    uint32_t order = 0;      // file order, keeps simultaneous events stable
    bool tempo = false;
    uint32_t tempoUs = 0;
    std::vector<uint8_t> bytes;
};

uint32_t Be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

bool VarLen(const std::vector<uint8_t>& d, size_t& pos, size_t end, uint32_t& value)
{
    value = 0;
    for (int i = 0; i < 4; i++)
    {
        if (pos >= end)
            return false;
        const uint8_t b = d[pos++];
        value = (value << 7) | (b & 0x7F);
        if (!(b & 0x80))
            return true;
    }
    return false;
}

int DataBytes(uint8_t status)
{
    switch (status & 0xF0)
    {
        case 0xC0:
        case 0xD0:
            return 1;
        default:
            return 2;
    }
}

} // namespace

bool ReadSmf(const std::string& path, SmfFile& out, std::string& error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        error = "cannot open " + path;
        return false;
    }
    const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // RIFF RMID wrapper: skip to the embedded MThd
    size_t pos = 0;
    if (d.size() >= 20 && std::equal(d.begin(), d.begin() + 4, "RIFF") && std::equal(d.begin() + 8, d.begin() + 12, "RMID"))
    {
        for (size_t i = 12; i + 4 <= d.size(); i++)
            if (std::equal(d.begin() + static_cast<long>(i), d.begin() + static_cast<long>(i) + 4, "MThd"))
            {
                pos = i;
                break;
            }
    }
    if (d.size() < pos + 14 || !std::equal(d.begin() + static_cast<long>(pos), d.begin() + static_cast<long>(pos) + 4, "MThd"))
    {
        error = "no MThd header";
        return false;
    }
    const uint32_t hdrLen = Be32(&d[pos + 4]);
    out.format = Be16(&d[pos + 8]);
    out.tracks = Be16(&d[pos + 10]);
    const uint16_t division = Be16(&d[pos + 12]);
    pos += 8 + hdrLen;

    std::vector<RawEvent> raw;
    uint32_t order = 0;
    uint64_t formatTwoOffset = 0;
    for (uint16_t t = 0; t < out.tracks && pos + 8 <= d.size(); t++)
    {
        const uint32_t len = Be32(&d[pos + 4]);
        const bool isTrack = std::equal(d.begin() + static_cast<long>(pos), d.begin() + static_cast<long>(pos) + 4, "MTrk");
        size_t p = pos + 8;
        const size_t end = std::min<size_t>(d.size(), p + len);
        pos = p + len;
        if (!isTrack)
            continue;
        uint64_t tick = out.format == 2 ? formatTwoOffset : 0;
        uint8_t running = 0;
        while (p < end)
        {
            uint32_t delta = 0;
            if (!VarLen(d, p, end, delta))
                break;
            tick += delta;
            if (p >= end)
                break;
            uint8_t status = d[p];
            RawEvent e;
            e.tick = tick;
            e.order = order++;
            if (status == 0xFF)
            {
                if (p + 2 > end)
                    break;
                const uint8_t type = d[p + 1];
                p += 2;
                uint32_t mlen = 0;
                if (!VarLen(d, p, end, mlen) || p + mlen > end)
                    break;
                if (type == 0x51 && mlen == 3)
                {
                    e.tempo = true;
                    e.tempoUs = (uint32_t(d[p]) << 16) | (uint32_t(d[p + 1]) << 8) | d[p + 2];
                    raw.push_back(e);
                }
                p += mlen;
                if (type == 0x2F)
                    break;
                continue;
            }
            if (status == 0xF0 || status == 0xF7)
            {
                p++;
                uint32_t slen = 0;
                if (!VarLen(d, p, end, slen) || p + slen > end)
                    break;
                if (status == 0xF0)
                    e.bytes.push_back(0xF0);
                e.bytes.insert(e.bytes.end(), d.begin() + static_cast<long>(p), d.begin() + static_cast<long>(p + slen));
                p += slen;
                running = 0;
                if (!e.bytes.empty())
                    raw.push_back(e);
                continue;
            }
            if (status & 0x80)
            {
                running = status;
                p++;
            }
            else if (running == 0)
            {
                p++; // stray data byte
                continue;
            }
            status = running;
            const int n = DataBytes(status);
            if (p + n > end)
                break;
            e.bytes.push_back(status);
            for (int i = 0; i < n; i++)
                e.bytes.push_back(d[p + i]);
            p += n;
            raw.push_back(e);
        }
        if (out.format == 2)
            formatTwoOffset = tick;
    }
    std::stable_sort(raw.begin(), raw.end(), [](const RawEvent& a, const RawEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    // ticks to seconds
    double secondsPerTick;
    const bool smpte = (division & 0x8000) != 0;
    if (smpte)
    {
        const int fps = -static_cast<int8_t>(division >> 8);
        const int perFrame = division & 0xFF;
        secondsPerTick = 1.0 / ((fps == 29 ? 29.97 : fps) * perFrame);
    }
    else
        secondsPerTick = 0.5 / (division == 0 ? 96 : division); // 120 BPM until the first tempo event
    const double ppqn = smpte ? 0.0 : (division == 0 ? 96 : division);
    double seconds = 0.0;
    uint64_t lastTick = 0;
    for (const RawEvent& e : raw)
    {
        seconds += static_cast<double>(e.tick - lastTick) * secondsPerTick;
        lastTick = e.tick;
        if (e.tempo)
        {
            if (!smpte && e.tempoUs > 0)
                secondsPerTick = e.tempoUs / 1e6 / ppqn;
            continue;
        }
        out.events.push_back(SmfEvent{seconds, e.bytes});
    }
    out.durationSeconds = seconds;
    return true;
}

} // namespace sam2695tools
