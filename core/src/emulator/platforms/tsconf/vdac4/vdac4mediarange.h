#pragma once

/// @file vdac4mediarange.h
/// @brief The app commands the running ESP app allowed as raw media DMA (zm_core_t::link_media, ABI 1.13).
/// The firmware keeps it in one global (protocol.hpp appMediaRangeValue) read by the link core and the frame
/// parser; here one object owned by Vdac4Esp and shared by reference. first > last: none (the default, and
/// again whenever the app stops).

#include <cstdint>

class Vdac4MediaRange
{
public:
    void Set(uint8_t first, uint8_t last)
    {
        _first = first;
        _last = last;
    }
    void Clear() { Set(1, 0); }
    bool Contains(uint8_t command) const { return command >= _first && command <= _last; }
    uint8_t First() const { return _first; }
    uint8_t Last() const { return _last; }

private:
    uint8_t _first = 1;
    uint8_t _last = 0;
};
