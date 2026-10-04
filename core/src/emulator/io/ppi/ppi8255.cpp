#include "stdafx.h"

#include "ppi8255.h"

void Ppi8255::Write(uint8_t reg, uint8_t value)
{
    switch (reg & 0x03)
    {
        case kPortA:
            _state.outA = value;
            break;
        case kPortB:
            _state.outB = value;
            break;
        case kPortC:
            _state.outC = value;
            break;
        case kControl:
            if (value & 0x80)
            {
                // Mode word: new directions, every output latch cleared (Intel 8255A data sheet, "Mode Selection")
                _state.control = value;
                _state.outA = 0;
                _state.outB = 0;
                _state.outC = 0;
            }
            else
            {
                // Port C bit set / reset: bits 3-1 the bit, bit 0 the level
                const uint8_t bit = static_cast<uint8_t>(1u << ((value >> 1) & 0x07));
                if (value & 0x01)
                    _state.outC |= bit;
                else
                    _state.outC &= static_cast<uint8_t>(~bit);
            }
            break;
    }
}

uint8_t Ppi8255::Read(uint8_t reg) const
{
    switch (reg & 0x03)
    {
        case kPortA:
            return IsInputA() ? (_inputA ? _inputA() : 0xFF) : _state.outA;
        case kPortB:
            return IsInputB() ? (_inputB ? _inputB() : 0xFF) : _state.outB;
        case kPortC:
        {
            const uint8_t in = (IsInputCUpper() || IsInputCLower()) ? (_inputC ? _inputC() : 0xFF) : 0xFF;
            const uint8_t upper = IsInputCUpper() ? (in & 0xF0) : (_state.outC & 0xF0);
            const uint8_t lower = IsInputCLower() ? (in & 0x0F) : (_state.outC & 0x0F);
            return static_cast<uint8_t>(upper | lower);
        }
        default:
            return 0xFF;   // the control register cannot be read on an 8255 (the bus floats)
    }
}
