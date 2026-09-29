#pragma once

/// @file szxwriter.h
/// @brief Serializes an szx::Stage as SZX 1.5 (design §9). Standard blocks
/// get their exact sizes (Fuse requires Z80R = 37, SPCR = 8, AY = 18 bytes);
/// a RAM page is stored compressed only when that is smaller, as libspectrum
/// does. Block order: CRTR, Z80R, SPCR, RAMP pages, AY, B128.

#include <cstdint>
#include <vector>

#include "loaders/snapshot/szx/szxformat.h"

class SzxWriter
{
public:
    static std::vector<uint8_t> Write(const szx::Stage& stage);

private:
    static void Block(std::vector<uint8_t>& out, uint32_t id, const std::vector<uint8_t>& body);
};
