#include "midiline.h"

#include <cstring>

#ifdef UNREALNG_HAVE_SAM2695
#include "sam2695/sam2695.h"
#endif

namespace
{
inline void PutU64(uint8_t*& cur, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        *cur++ = static_cast<uint8_t>(v >> (8 * i));
}

inline uint64_t GetU64(const uint8_t*& cur)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= static_cast<uint64_t>(*cur++) << (8 * i);
    return v;
}
}  // namespace

MidiLine::MidiLine(int port, int bit) : _port(port), _bit(bit & 7)
{
}

void MidiLine::Reset(uint64_t t)
{
    // Reset clears R7: the port becomes an input and its pull-up takes the line high
    SetLevel(t, true);
}

void MidiLine::OnIoPortPins(uint64_t t, int port, uint8_t pins)
{
    if (port != _port)
        return;
    SetLevel(t, ((pins >> _bit) & 1u) != 0);
}

void MidiLine::SetLevel(uint64_t t, bool level)
{
    if (level == _level)
        return;
    _level = level;
    _lastChangeTime = t;
    _edges++;
#ifdef UNREALNG_HAVE_SAM2695
    if (_synth != nullptr)
        _synth->WriteLine(t, level);
#endif
}

void MidiLine::Describe(MidiLineReport& out) const
{
    out = MidiLineReport{};
    out.port = _port;
    out.bit = _bit;
    out.connected = _synth != nullptr;
    out.level = _level;
    out.lastChangeTime = _lastChangeTime;
    out.edges = _edges;
}

/// region <TTDSerializable>

void MidiLine::TTDSaveState(uint8_t* dst) const
{
    uint8_t* cur = dst;
    *cur++ = kStateVersion;
    *cur++ = _level ? 1 : 0;
    PutU64(cur, _lastChangeTime);
    PutU64(cur, _edges);
}

void MidiLine::TTDLoadState(const uint8_t* src)
{
    const uint8_t* cur = src;
    if (*cur++ != kStateVersion)
        return;  // another layout: keep the current state
    _level = *cur++ != 0;
    _lastChangeTime = GetU64(cur);
    _edges = GetU64(cur);
}

uint64_t MidiLine::TTDHashState() const
{
    uint8_t blob[kBlobSize];
    TTDSaveState(blob);
    uint64_t h = 14695981039346656037ull;
    for (uint8_t b : blob)
    {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

/// endregion </TTDSerializable>
