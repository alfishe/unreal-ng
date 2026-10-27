#include "stdafx.h"

#include "nextcopper.h"

#include <algorithm>
#include <iterator>

void NextCopper::SetGeometry(unsigned hcPerLine, unsigned linesPerFrame)
{
    _hcPerLine = hcPerLine;
    _lines = linesPerFrame;
    _frameClocks = static_cast<uint64_t>(hcPerLine) * linesPerFrame * 4;
    _idleUntil = 0;
    if (_t >= _frameClocks)
        _t = 0;
}

void NextCopper::Reset(bool clearCode)
{
    if (clearCode)
        std::fill(std::begin(_code), std::end(_code), uint16_t(0));
    _pc = 0;
    _address = 0;
    _stored = 0;
    _mode = _lastMode = 0;
    _offset = 0;
    _pending = false;
    _t = 0;
    _idleUntil = 0;
    _observed = false;
}

void NextCopper::WriteData(uint8_t value)
{
    _idleUntil = 0;
    const unsigned word = (_address >> 1) & 0x3FF;
    if (!(_address & 1))
    {
        _stored = value;
        _code[word] = static_cast<uint16_t>((_code[word] & 0x00FF) | (value << 8));
    }
    else
        _code[word] = static_cast<uint16_t>((_code[word] & 0xFF00) | value);
    _address = (_address + 1) & 0x7FF;
}

void NextCopper::WriteAddressLow(uint8_t value)
{
    _address = static_cast<uint16_t>((_address & 0x700) | value);
}

void NextCopper::WriteControl(uint8_t value)
{
    Sync();  // the old mode ran up to now
    _idleUntil = 0;
    _mode = (value >> 6) & 3;
    _address = static_cast<uint16_t>((_address & 0x0FF) | ((value & 7) << 8));
}

void NextCopper::WriteWord(uint8_t value)
{
    _idleUntil = 0;
    const unsigned word = (_address >> 1) & 0x3FF;
    if (!(_address & 1))
        _stored = value;
    else
        _code[word] = static_cast<uint16_t>((_stored << 8) | value);
    _address = (_address + 1) & 0x7FF;
}

void NextCopper::Sync()
{
    if (_now && !_running)
        RunTo(_now());
}

void NextCopper::RunTo(uint64_t target)
{
    if (_running)
        return;  // a MOVE of the copper's own registers
    if (target >= _frameClocks)
        target = _frameClocks - 1;
    // A WAIT that is not true before `_idleUntil`: the cursor only moves (the common case between two MOVEs)
    if (target >= _t && target < _idleUntil)
    {
        _t = target;
        return;
    }
    _running = true;
    _idleUntil = 0;
    if (target < _t)
    {
        if (!RunSegment(_frameClocks))
        {
            _running = false;
            return;
        }
        _t = 0;
        _idleUntil = 0;  // a new frame: the wait of the old one says nothing
        // mode 11 restarts at line 0, horizontal 0
        if (_mode == 3)
        {
            _pc = 0;
            _pending = false;
        }
    }
    RunSegment(target);
    _running = false;
}

bool NextCopper::RunSegment(uint64_t end)
{
    const uint64_t lineClocks = static_cast<uint64_t>(_hcPerLine) * 4;
    while (_t < end)
    {
        if (_mode != _lastMode)
        {
            _lastMode = _mode;
            if (_mode == 1 || _mode == 3)
                _pc = 0;
            _pending = false;
            _t++;  // no execution on the clock the mode changes
            continue;
        }
        if (_mode == 0)
        {
            _t = end;
            break;
        }
        if (_pending)
        {
            _pending = false;
            _t++;
            continue;
        }
        const uint16_t instruction = _code[_pc & 0x3FF];
        if (instruction & 0x8000)  // WAIT: line 8:0, horizontal 14:9 in units of 8 pixels
        {
            const unsigned vpos = instruction & 0x1FF;
            const unsigned threshold = static_cast<unsigned>((((instruction >> 9) & 0x3F) << 3) + 12) & 0x1FF;
            const uint64_t cvc = ((_t / lineClocks) + _offset) % _lines;
            const unsigned hc = static_cast<unsigned>((_t % lineClocks) / 4);
            if (cvc == vpos && hc >= threshold)
            {
                if (_observer && !_observed)
                {
                    _observed = true;
                    if (_observer(Step{_pc, _t, false, 0, 0}))
                        return false;  // held: the machine stopped here, the WAIT leaves when it runs on
                }
                _observed = false;
                _pc = (_pc + 1) & 0x3FF;
                _t++;
                continue;
            }
            // when the beam gets there: the raw line that shows `vpos` with the offset, at the threshold
            uint64_t wanted = UINT64_MAX;
            if (vpos < _lines && threshold < _hcPerLine)
            {
                const uint64_t rawLine = (vpos + _lines - (_offset % _lines)) % _lines;
                wanted = (rawLine * _hcPerLine + threshold) * 4;
            }
            const bool comes = wanted != UINT64_MAX && wanted > _t;
            // Nothing happens before `wanted`; a beam that is past the place for this frame waits for the next one
            _idleUntil = comes ? wanted : _frameClocks;
            _t = comes ? (wanted < end ? wanted : end) : end;
            continue;
        }
        const uint8_t reg = static_cast<uint8_t>((instruction >> 8) & 0x7F);
        const uint8_t value = static_cast<uint8_t>(instruction & 0xFF);
        const uint64_t at = _t;
        if (_observer && !_observed)
        {
            _observed = true;
            if (_observer(Step{_pc, at, true, reg, value}))
                return false;  // held: the machine stopped before this MOVE writes; it writes when the copper runs on
        }
        _observed = false;
        _lastMoveAt = at;
        _pc = (_pc + 1) & 0x3FF;
        _t++;
        if (reg != 0)
        {
            if (_beforeWrite)
                _beforeWrite(at, reg, value);
            if (_write)
                _write(reg, value);
            _pending = true;
        }
    }
    return true;
}
