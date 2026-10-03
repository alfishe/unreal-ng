#include "emulator/io/network/ethernet/eeprom93c46.h"

namespace
{
constexpr uint8_t kPhaseCommand = 0;
constexpr uint8_t kPhaseDataIn = 1;
constexpr uint8_t kPhaseDataOut = 2;
constexpr uint8_t kPhaseDone = 3;
constexpr uint8_t kNotStarted = 0xFF;
}  // namespace

void Eeprom93c46::SetPins(bool cs, bool sk, bool di)
{
    const bool wasCs = _s.cs != 0;
    const bool wasSk = _s.sk != 0;
    _s.cs = cs ? 1 : 0;
    _s.sk = sk ? 1 : 0;
    if (wasCs && !cs)
    {
        Finish();
        return;
    }
    if (!wasCs && cs)
    {
        // A new instruction: leading zeros are ignored until the start bit; DO shows ready
        _s.phase = kPhaseCommand;
        _s.bitCount = kNotStarted;
        _s.shift = 0;
        _s.dataOut = 1;
    }
    if (cs && sk && !wasSk)
        RisingEdge(di);
}

void Eeprom93c46::RisingEdge(bool di)
{
    switch (_s.phase)
    {
        case kPhaseCommand:
        {
            if (_s.bitCount == kNotStarted)
            {
                if (di)
                    _s.bitCount = 0;   // the start bit
                return;
            }
            _s.shift = static_cast<uint16_t>((_s.shift << 1) | (di ? 1 : 0));
            if (++_s.bitCount < 8)
                return;
            _s.opcode = static_cast<uint8_t>((_s.shift >> 6) & 3);
            _s.address = static_cast<uint8_t>(_s.shift & 0x3F);
            _s.shift = 0;
            _s.bitCount = 0;
            switch (_s.opcode)
            {
                case 2:   // READ: a dummy 0 with the last address bit, then the word
                    _s.phase = kPhaseDataOut;
                    _s.outWord = _s.words[_s.address];
                    _s.outBits = 16;
                    _s.dataOut = 0;
                    break;
                case 1:   // WRITE: 16 data bits follow
                    _s.phase = kPhaseDataIn;
                    break;
                case 3:   // ERASE: on CS falling
                    _s.phase = kPhaseDone;
                    break;
                default:
                    switch (_s.address >> 4)
                    {
                        case 3: _s.writeEnabled = 1; _s.phase = kPhaseDone; _s.opcode = 0xFF; break;   // EWEN
                        case 0: _s.writeEnabled = 0; _s.phase = kPhaseDone; _s.opcode = 0xFF; break;   // EWDS
                        case 2: _s.phase = kPhaseDone; break;                                          // ERAL
                        default: _s.phase = kPhaseDataIn; break;                                       // WRAL
                    }
                    break;
            }
            return;
        }
        case kPhaseDataIn:
            _s.shift = static_cast<uint16_t>((_s.shift << 1) | (di ? 1 : 0));
            if (++_s.bitCount >= 16)
                _s.phase = kPhaseDone;
            return;
        case kPhaseDataOut:
            if (_s.outBits == 0)
            {
                // Sequential read: the next word follows without a dummy bit
                _s.address = static_cast<uint8_t>((_s.address + 1) & (kWords - 1));
                _s.outWord = _s.words[_s.address];
                _s.outBits = 16;
            }
            --_s.outBits;
            _s.dataOut = static_cast<uint8_t>((_s.outWord >> _s.outBits) & 1);
            return;
        default:
            return;
    }
}

void Eeprom93c46::Finish()
{
    if (_s.phase == kPhaseDone && _s.writeEnabled && _s.opcode != 0xFF)
    {
        switch (_s.opcode)
        {
            case 1: _s.words[_s.address] = _s.shift; break;   // WRITE
            case 3: _s.words[_s.address] = 0xFFFF; break;      // ERASE
            default:
                if ((_s.address >> 4) == 2)
                    _s.words.fill(0xFFFF);                      // ERAL
                else if ((_s.address >> 4) == 1)
                    _s.words.fill(_s.shift);                    // WRAL
                break;
        }
    }
    _s.phase = kPhaseCommand;
    _s.bitCount = kNotStarted;
    _s.shift = 0;
    _s.dataOut = 1;
}
