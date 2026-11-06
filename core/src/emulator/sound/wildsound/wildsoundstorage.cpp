#include "wildsoundstorage.h"

#include <algorithm>
#include <cstring>

namespace wildsound
{

namespace
{

uint16_t Le16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t Le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void Put32(uint8_t*& p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
    p += 4;
}

uint32_t Get32(const uint8_t*& p)
{
    const uint32_t v = Le32(p);
    p += 4;
    return v;
}

}  // namespace

WildSoundStorage::WildSoundStorage() : _flash(kFlashSize, 0xFF)
{
}

void WildSoundStorage::Clear()
{
    _used = 0;
    _descriptors = 0;
    _trackOffset = _trackSize = _trackReceived = 0;
    _trackAllocated = false;
    _trackEnded = false;
    _track = WildSoundTrack{};
    _tableOffset = _tableSize = _tableReceived = 0;
    _tableAllocated = false;
    _instrumentsParsed = false;
    _instrumentsBase = 0;
    _instruments.clear();
    _samplePos = 0;
    _error.clear();
}

bool WildSoundStorage::Allocate(uint32_t size, uint32_t& offset)
{
    if (_descriptors >= kDescriptors)
    {
        _error = "out of descriptors (64: the track, the table, instrument 0 and 61 instruments)";
        return false;
    }
    if (size > kFlashSize - _used)
    {
        _error = "out of storage (" + std::to_string(size) + " bytes asked, " + std::to_string(kFlashSize - _used) +
                 " free)";
        return false;
    }
    offset = _used;
    _used += size;
    ++_descriptors;
    return true;
}

bool WildSoundStorage::BeginTrack(uint32_t size)
{
    _track = WildSoundTrack{};
    _trackReceived = 0;
    _trackEnded = false;
    _trackAllocated = Allocate(size, _trackOffset);
    _trackSize = _trackAllocated ? size : 0;
    return _trackAllocated;
}

void WildSoundStorage::AppendTrack(const uint8_t* data, size_t count)
{
    if (!_trackAllocated)
        return;
    const size_t room = _trackSize - _trackReceived;
    const size_t n = std::min(count, room);
    std::memcpy(_flash.data() + _trackOffset + _trackReceived, data, n);
    if (_writeHook)
        _writeHook(_trackOffset + _trackReceived, n);
    _trackReceived += static_cast<uint32_t>(n);
}

bool WildSoundStorage::EndTrack()
{
    _trackEnded = _trackAllocated;
    return _trackAllocated && ParseTrack();
}

bool WildSoundStorage::ParseTrack()
{
    _track = WildSoundTrack{};
    if (_trackReceived < kHeaderSize + kPatternRecordSize)
    {
        _error = "track pack too short (" + std::to_string(_trackReceived) + " bytes)";
        return false;
    }
    const uint8_t* p = _flash.data() + _trackOffset;
    WildSoundTrack t;
    for (int i = 0; i < 20 && p[4 + i]; ++i)
        t.name.push_back(static_cast<char>(p[4 + i]));
    t.channels = Le16(p + 24);
    t.speed = Le16(p + 26);
    t.bpm = Le16(p + 28);
    t.linearFrequencies = (Le16(p + 30) & 1) != 0;
    t.songLength = Le16(p + 32);
    t.restart = Le16(p + 34);
    std::memcpy(t.order.data(), p + 36, 256);

    // Pattern count from record 0's offset, as the card computes it (reference-firmware.md §B.3)
    const uint32_t first = Le32(p + kHeaderSize);
    if (first < kHeaderSize + kPatternRecordSize || (first - kHeaderSize) % kPatternRecordSize != 0 ||
        first > _trackReceived)
    {
        _error = "pattern record 0 offset " + std::to_string(first) + " is not the header size";
        return false;
    }
    const uint32_t count = (first - kHeaderSize) / kPatternRecordSize;
    if (t.channels < 1 || t.channels > kMaxChannels)
    {
        _error = "channels " + std::to_string(t.channels) + " outside 1..32";
        return false;
    }
    if (t.songLength < 1 || t.songLength > 256)
    {
        _error = "song length " + std::to_string(t.songLength) + " outside 1..256";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t* r = p + kHeaderSize + i * kPatternRecordSize;
        WildSoundPattern pat;
        pat.offset = Le32(r);
        pat.size = Le32(r + 4);
        pat.rows = Le16(r + 8);
        if (pat.size && (pat.offset < first || pat.offset > _trackReceived || pat.size > _trackReceived - pat.offset))
        {
            _error = "pattern " + std::to_string(i) + " lies outside the pack";
            return false;
        }
        if (pat.size && (pat.rows < 1 || pat.rows > 256))
        {
            _error = "pattern " + std::to_string(i) + " has " + std::to_string(pat.rows) + " rows";
            return false;
        }
        t.patterns.push_back(pat);
    }
    for (uint16_t i = 0; i < t.songLength; ++i)
    {
        if (t.order[i] >= count)
        {
            _error = "order " + std::to_string(i) + " names pattern " + std::to_string(t.order[i]) + " of " +
                     std::to_string(count);
            return false;
        }
    }
    t.valid = true;
    _track = std::move(t);
    return true;
}

const uint8_t* WildSoundStorage::TrackData() const
{
    return _trackAllocated ? _flash.data() + _trackOffset : nullptr;
}

bool WildSoundStorage::BeginInstruments(uint32_t size)
{
    _instruments.clear();
    _instrumentsParsed = false;
    _tableReceived = 0;
    _samplePos = 0;
    // The table, plus the card's blank record 0
    _tableAllocated = Allocate(size + kRecordSize, _tableOffset);
    _tableSize = _tableAllocated ? size : 0;
    return _tableAllocated;
}

void WildSoundStorage::AppendInstruments(const uint8_t* data, size_t count)
{
    if (!_tableAllocated)
        return;
    const size_t room = _tableSize - _tableReceived;
    const size_t n = std::min(count, room);
    std::memcpy(_flash.data() + _tableOffset + kRecordSize + _tableReceived, data, n);
    if (_writeHook)
        _writeHook(_tableOffset + kRecordSize + _tableReceived, n);
    _tableReceived += static_cast<uint32_t>(n);
}

bool WildSoundStorage::EndInstruments()
{
    if (!_tableAllocated)
        return false;
    // The blank instrument 0 costs a descriptor and an empty block on the card
    uint32_t blank = 0;
    if (!Allocate(kInstrumentOverhead + 16, blank))
        return false;
    _instrumentsBase = _used;
    return ParseInstruments();
}

bool WildSoundStorage::ParseInstruments()
{
    _instruments.clear();
    _instrumentsParsed = false;
    const uint32_t records = _tableReceived / kRecordSize;
    const uint8_t* p = _flash.data() + _tableOffset + kRecordSize;
    // Instrument blocks follow each other from _instrumentsBase (set when the table closed)
    _used = _instrumentsBase;
    for (uint32_t i = 0; i < records; ++i)
    {
        const uint8_t* r = p + i * kRecordSize;
        WildSoundInstrument ins;
        const uint32_t ofs = Le32(r);
        ins.streamOffset = ofs >= _tableSize ? ofs - _tableSize : 0;
        ins.length = Le32(r + 4);
        ins.loopStart = Le32(r + 8);
        ins.loopEnd = Le32(r + 12);
        ins.volume = std::min<uint8_t>(r[16], 64);
        ins.fineTune = static_cast<int8_t>(r[17]);
        ins.type = r[18];
        ins.panning = r[19];
        ins.relativeNote = static_cast<int8_t>(r[20]);
        if (ins.loopEnd > ins.length)
            ins.loopEnd = ins.length;
        if (ins.loopStart > ins.loopEnd)
            ins.loopStart = ins.loopEnd;
        uint32_t block = 0;
        if (!Allocate(ins.length + kInstrumentOverhead, block))
        {
            _error += " at instrument " + std::to_string(i + 1);
            return false;
        }
        ins.dataOffset = block + kSampleDataOffset;
        _instruments.push_back(ins);
    }
    _instrumentsParsed = true;
    return true;
}

void WildSoundStorage::PutSampleByte(uint8_t value)
{
    // The instrument whose stream range holds this byte: the highest one starting at or before it
    const uint32_t p = _samplePos++;
    for (size_t i = _instruments.size(); i-- > 0;)
    {
        WildSoundInstrument& ins = _instruments[i];
        if (ins.streamOffset <= p && ins.length)
        {
            const uint32_t at = p - ins.streamOffset;
            if (at < ins.length)
            {
                _flash[ins.dataOffset + at] = value;
                if (_writeHook)
                    _writeHook(ins.dataOffset + at, 1);
                ins.received = std::max(ins.received, at + 1);
            }
            return;
        }
    }
}

const uint8_t* WildSoundStorage::SampleData(const WildSoundInstrument& ins) const
{
    return ins.dataOffset ? _flash.data() + ins.dataOffset : nullptr;
}

bool WildSoundStorage::InstrumentsComplete() const
{
    return _instrumentsParsed &&
           std::all_of(_instruments.begin(), _instruments.end(),
                       [](const WildSoundInstrument& i) { return i.received == i.length; });
}

void WildSoundStorage::SaveState(uint8_t* dst) const
{
    std::memset(dst, 0, kStateSize);
    uint8_t* p = dst;
    Put32(p, _used);
    Put32(p, _descriptors);
    Put32(p, _trackOffset);
    Put32(p, _trackSize);
    Put32(p, _trackReceived);
    Put32(p, _tableOffset);
    Put32(p, _tableSize);
    Put32(p, _tableReceived);
    Put32(p, _instrumentsBase);
    Put32(p, _samplePos);
    *p++ = static_cast<uint8_t>((_trackAllocated ? 1 : 0) | (_trackEnded ? 2 : 0) | (_tableAllocated ? 4 : 0) |
                                (_instrumentsParsed ? 8 : 0));
}

void WildSoundStorage::LoadState(const uint8_t* src)
{
    const uint8_t* p = src;
    const uint32_t used = Get32(p);
    const uint32_t descriptors = Get32(p);
    _trackOffset = Get32(p);
    _trackSize = Get32(p);
    _trackReceived = Get32(p);
    _tableOffset = Get32(p);
    _tableSize = Get32(p);
    _tableReceived = Get32(p);
    _instrumentsBase = Get32(p);
    _samplePos = Get32(p);
    const uint8_t flags = *p++;
    _trackAllocated = (flags & 1) != 0;
    _trackEnded = (flags & 2) != 0;
    _tableAllocated = (flags & 4) != 0;
    _instrumentsParsed = (flags & 8) != 0;
    _used = used;
    _descriptors = descriptors;
    Reparse();
}

void WildSoundStorage::Reparse()
{
    const uint32_t used = _used;
    const uint32_t descriptors = _descriptors;
    const bool parsed = _instrumentsParsed;
    _error.clear();
    _track = WildSoundTrack{};
    if (_trackEnded)
        ParseTrack();
    _instruments.clear();
    _instrumentsParsed = false;
    if (parsed)
        ParseInstruments();
    // The sample bytes each instrument has: everything the stream position has passed
    for (WildSoundInstrument& ins : _instruments)
        ins.received = _samplePos > ins.streamOffset ? std::min(ins.length, _samplePos - ins.streamOffset) : 0;
    _used = used;
    _descriptors = descriptors;
}

}  // namespace wildsound
