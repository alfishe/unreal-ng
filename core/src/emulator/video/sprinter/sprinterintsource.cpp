#include "stdafx.h"

#include "sprinterintsource.h"

#include <algorithm>

#include "emulator/emulatorcontext.h"
#include "emulator/video/sprinter/sprintervideoram.h"

SprinterIntSource::SprinterIntSource(EmulatorContext* context, const SprinterVideoRam& vram)
    : _context(context), _vram(vram)
{
}

void SprinterIntSource::Reset()
{
    _ackedPulse = -1;
    _dirty = true;
}

void SprinterIntSource::SetModePage(uint8_t page)
{
    page &= 1;
    if (page != _modePage)
    {
        _modePage = page;
        _dirty = true;
    }
}

void SprinterIntSource::SetFrameLines(uint16_t lines)
{
    if (lines != _frameLines)
    {
        _frameLines = lines;
        _dirty = true;
    }
}

std::vector<uint32_t> SprinterIntSource::ComputePositions(const SprinterVideoRam& vram, uint8_t modePage, uint16_t frameLines)
{
    std::vector<uint32_t> positions;
    const uint8_t height = static_cast<uint8_t>(frameLines / 8);
    for (uint8_t scrB = 0; scrB < height; scrB++)
    {
        bool armed = false;
        const uint8_t b = static_cast<uint8_t>((scrB + height - 2) % height);  // 2 rows of top border
        for (uint8_t scrA = 0; scrA < kSquareColumns; scrA++)
        {
            const uint8_t a = static_cast<uint8_t>((scrA + kSquareColumns - 6) % kSquareColumns);
            if ((vram.Mode0(a, b, modePage) & 0xFD) == 0xFD)
            {
                armed = true;
            }
            else
            {
                if (armed)
                    positions.push_back((scrB * 8u + 7u) * kLineTStates + (scrA * 16u) / 4u);
                armed = false;
            }
        }
    }
    std::sort(positions.begin(), positions.end());
    return positions;
}

const std::vector<uint32_t>& SprinterIntSource::Positions()
{
    if (_dirty)
    {
        _positions = ComputePositions(_vram, _modePage, _frameLines);
        _dirty = false;
    }
    return _positions;
}

uint32_t SprinterIntSource::Multiplier() const
{
    const uint32_t multiplier = _context->emulatorState.current_z80_frequency_multiplier;
    return multiplier ? multiplier : 1;
}

int64_t SprinterIntSource::PulseAt(uint64_t frame, uint32_t raster)
{
    const std::vector<uint32_t>& positions = Positions();
    if (positions.empty())
        return -1;

    // The machine's frame in base T-states (the INT list itself follows the PLD's 320 / 312 lines)
    const uint32_t frameLength = _context->config.frame ? _context->config.frame : static_cast<uint32_t>(_frameLines) * kLineTStates;
    for (uint32_t start : positions)
    {
        if (raster >= start && raster < start + kPulseTStates)
            return static_cast<int64_t>(frame * frameLength + start);
    }
    // A pulse that started at the end of the previous frame
    if (frame > 0)
    {
        for (uint32_t start : positions)
        {
            if (start + kPulseTStates > frameLength && raster < start + kPulseTStates - frameLength)
                return static_cast<int64_t>((frame - 1) * frameLength + start);
        }
    }
    return -1;
}

bool SprinterIntSource::IsIntAsserted(uint32_t t)
{
    const int64_t pulse = PulseAt(_context->emulatorState.frame_counter, t / Multiplier());
    return pulse >= 0 && pulse != _ackedPulse;
}

uint8_t SprinterIntSource::AcknowledgeInterrupt(uint32_t t)
{
    const int64_t pulse = PulseAt(_context->emulatorState.frame_counter, t / Multiplier());
    if (pulse >= 0)
        _ackedPulse = pulse;
    return 0xFF;  // the PLD sources answer #FF (hardware-reference §2)
}
