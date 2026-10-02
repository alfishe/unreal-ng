#include "stdafx.h"

#include "vdac2capture.h"

#include <cstring>

#include "3rdparty/digestpp/digestpp.hpp"
#include "common/filehelper.h"

namespace
{

constexpr size_t kFlushBytes = 1 << 20;
constexpr uint32_t kFlagStartsAtPowerOn = 0x01;

void PutLe(std::vector<uint8_t>& out, uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; i++)
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

} // namespace

bool Vdac2Capture::Open(const std::string& path, uint32_t externalClockHz, const std::vector<uint8_t>& rom,
                        const ChipState* state, uint64_t clock)
{
    Close(_lastClock);
    _file = FileHelper::OpenFile(path, "wb");
    if (!_file)
        return false;
    _lastClock = state ? clock : 0;
    _stats = Stats{};
    _stats.path = path;
    _stats.startClock = _lastClock;
    _stats.lastClock = _lastClock;

    // Header, 64 bytes: magic, header size, chip model, external clock, ROM
    // SHA-1 (zeros: no ROM) and size, flags, reserved
    std::vector<uint8_t> header;
    header.insert(header.end(), {'E', 'V', 'R', '1'});
    PutLe(header, kHeaderSize, 4);
    PutLe(header, kChipModel, 4);
    PutLe(header, externalClockHz, 4);
    uint8_t sha1[20] = {};
    if (!rom.empty())
        digestpp::sha1().absorb(rom.data(), rom.size()).digest(sha1, sizeof sha1);
    header.insert(header.end(), sha1, sha1 + sizeof sha1);
    PutLe(header, rom.size(), 4);
    PutLe(header, state ? 0 : kFlagStartsAtPowerOn, 4);
    PutLe(header, _lastClock, 8);  // the clock the stream starts at
    header.resize(kHeaderSize, 0);
    _buffer = std::move(header);

    // A running chip: its whole state first (state blob, then each region)
    if (state)
    {
        Record(clock, kState);
        PutLe(_buffer, state->state.size(), 4);
        _buffer.insert(_buffer.end(), state->state.begin(), state->state.end());
        PutLe(_buffer, state->regions.size(), 4);
        for (const ChipState::Region& region : state->regions)
        {
            Put8(static_cast<uint8_t>(region.name.size()));
            _buffer.insert(_buffer.end(), region.name.begin(), region.name.end());
            PutLe(_buffer, region.data.size(), 4);
            _buffer.insert(_buffer.end(), region.data.begin(), region.data.end());
        }
    }
    Flush();
    return true;
}

void Vdac2Capture::Record(uint64_t clock, Kind kind)
{
    // Kind, then the clocks since the previous record as an unsigned LEB128
    Put8(kind);
    uint64_t delta = clock >= _lastClock ? clock - _lastClock : 0;
    _lastClock = clock >= _lastClock ? clock : _lastClock;
    do
    {
        uint8_t byte = static_cast<uint8_t>(delta & 0x7F);
        delta >>= 7;
        if (delta)
            byte |= 0x80;
        Put8(byte);
    } while (delta);
}

void Vdac2Capture::Select(uint64_t clock, bool selected)
{
    if (!_file)
        return;
    Record(clock, kSelect);
    Put8(selected ? 1 : 0);
    _stats.selects++;
    if (_buffer.size() >= kFlushBytes)
        Flush();
}

void Vdac2Capture::Byte(uint64_t clock, uint8_t mosi, uint8_t miso)
{
    if (!_file)
        return;
    Record(clock, kByte);
    Put8(mosi);
    Put8(miso);
    _stats.exchanges++;
    if (_buffer.size() >= kFlushBytes)
        Flush();
}

void Vdac2Capture::Frame(uint64_t clock, uint64_t frames, bool drawn, uint16_t width, uint16_t height, uint64_t pictureHash)
{
    if (!_file)
        return;
    Record(clock, kFrame);
    Put64(frames);
    Put8(drawn ? 1 : 0);
    Put16(width);
    Put16(height);
    Put64(drawn ? pictureHash : 0);
    _stats.frames++;
    if (_buffer.size() >= kFlushBytes)
        Flush();
}

void Vdac2Capture::PowerOn(uint64_t clock)
{
    if (!_file)
        return;
    Record(clock, kPowerOn);
}

void Vdac2Capture::Close(uint64_t clock)
{
    if (!_file)
        return;
    Record(clock, kEnd);
    Flush();
    std::fclose(_file);
    _file = nullptr;
}

uint64_t Vdac2Capture::HashPicture(const uint32_t* pixels, size_t count)
{
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < count; i++)
    {
        const uint32_t pixel = pixels[i];
        for (int b = 0; b < 4; b++)
        {
            hash ^= static_cast<uint8_t>(pixel >> (8 * b));
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

void Vdac2Capture::Put16(uint16_t value)
{
    PutLe(_buffer, value, 2);
}

void Vdac2Capture::Put64(uint64_t value)
{
    PutLe(_buffer, value, 8);
}

void Vdac2Capture::Flush()
{
    if (_file && !_buffer.empty())
    {
        std::fwrite(_buffer.data(), 1, _buffer.size(), _file);
        _stats.bytesWritten += _buffer.size();
    }
    _stats.lastClock = _lastClock;
    _buffer.clear();
}
