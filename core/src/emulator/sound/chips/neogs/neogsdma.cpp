#include "neogsdma.h"

#include <algorithm>
#include <cstring>

#include "common/statebytes.h"

#include "emulator/io/spi/spidevice.h"
#include "emulator/sound/chips/neogs/neogsinterrupts.h"
#include "emulator/sound/chips/neogs/neogsmemory.h"
#include "emulator/sound/chips/neogs/vs10xx.h"

namespace
{
constexpr int kMaxPollBytesPerEvent = 64; // an absent card answers #FF forever: poll in slices
} // namespace

void NeoGSDma::reset()
{
    for (auto& regs : _regs)
        regs[3] = static_cast<uint8_t>(regs[3] & 0x7F);
    _select = 0;
    _sdPhase = Phase::Idle;
    _sdAt = kNever;
    _mp3Phase = Phase::Idle;
    _mp3At = kNever;
}

uint32_t NeoGSDma::address(Module m) const
{
    // HAD is 6 bits wide but the sequencer passes only [20:0]
    return (static_cast<uint32_t>(_regs[m][0] & 0x1F) << 16) | (static_cast<uint32_t>(_regs[m][1]) << 8) | _regs[m][2];
}

void NeoGSDma::advanceAddress(Module m, int bytes)
{
    const uint32_t full = ((static_cast<uint32_t>(_regs[m][0] & 0x3F) << 16) | (static_cast<uint32_t>(_regs[m][1]) << 8) | _regs[m][2]) +
                          static_cast<uint32_t>(bytes);
    _regs[m][0] = static_cast<uint8_t>((full >> 16) & 0x3F);
    _regs[m][1] = static_cast<uint8_t>(full >> 8);
    _regs[m][2] = static_cast<uint8_t>(full);
}

uint8_t NeoGSDma::readRegister(int reg) const
{
    if (_select < 1 || _select > 3)
        return 0xFF;
    const uint8_t value = _regs[_select - 1][reg & 3];
    switch (reg & 3)
    {
        case 0: return static_cast<uint8_t>(value | 0xC0); // HAD: 6 bits
        case 3: return static_cast<uint8_t>(value | 0x7F); // CST: only bit 7
        default: return value;
    }
}

void NeoGSDma::writeRegister(int reg, uint8_t value, int64_t now)
{
    if (_select < 1 || _select > 3)
        return;
    const Module m = static_cast<Module>(_select - 1);
    switch (reg & 3)
    {
        case 0: _regs[m][0] = static_cast<uint8_t>(value & 0x3F); return;
        case 1: _regs[m][1] = value; return;
        case 2: _regs[m][2] = value; return;
        case 3:
        {
            const bool wasRunning = running(m);
            _regs[m][3] = static_cast<uint8_t>(value & 0x80);
            if (!wasRunning && running(m))
                start(m, now);
            else if (wasRunning && !running(m))
                finish(m, false); // aborted: no interrupt
            return;
        }
    }
}

void NeoGSDma::start(Module m, int64_t now)
{
    switch (m)
    {
        case SD:
            _sdPhase = Phase::SdWait;
            _sdAt = now;
            break;
        case MP3:
        {
            // Burst: 512 bytes from RAM into the FIFO, CPU stalled
            const uint32_t base = address(MP3);
            for (int i = 0; i < BLOCK; i++)
                _mp3Buffer[i] = _mem.ram()[(base + static_cast<uint32_t>(i)) % _mem.ramSize()];
            advanceAddress(MP3, BLOCK);
            const int64_t burst = static_cast<int64_t>(BLOCK * BURST_CLOCKS_PER_BYTE + GRANT_OVERHEAD_CLOCKS) * _host.dmaUnitsPerCycle();
            _host.dmaStall(burst);
            _mp3Phase = Phase::Mp3Send;
            _mp3Sent = 0;
            _mp3At = now + burst;
            break;
        }
        case ZX:
            break; // phase 5: the host-side hook
    }
}

void NeoGSDma::finish(Module m, bool raiseInterrupt)
{
    _regs[m][3] = static_cast<uint8_t>(_regs[m][3] & 0x7F);
    if (m == SD)
    {
        _sdPhase = Phase::Idle;
        _sdAt = kNever;
        if (raiseInterrupt)
            _irq.raise(NeoGSInterrupts::REQ_SD_DMA);
    }
    else if (m == MP3)
    {
        _mp3Phase = Phase::Idle;
        _mp3At = kNever;
        if (raiseInterrupt)
            _irq.raise(NeoGSInterrupts::REQ_MP3_DMA);
    }
}

int64_t NeoGSDma::nextEvent() const
{
    return std::min(_sdAt, _mp3At);
}

void NeoGSDma::run(int64_t now)
{
    if (now >= _sdAt)
        runSd(now);
    if (now >= _mp3At)
        runMp3(now);
}

namespace
{
/// Time left until `at`, counted in card clocks of the old rate, in the new rate
void rescaleToClock(int64_t& at, int64_t now, int64_t oldUnits, int64_t newUnits)
{
    if (at == NeoGSDma::kNever || at <= now || oldUnits <= 0)
        return;
    const int64_t clocks = (at - now + oldUnits - 1) / oldUnits;
    at = now + clocks * newUnits;
}
} // namespace

void NeoGSDma::onClockChange(int64_t now, int64_t oldUnitsPerCycle, int64_t newUnitsPerCycle)
{
    if (_sdPhase != Phase::Idle)
        rescaleToClock(_sdAt, now, oldUnitsPerCycle, newUnitsPerCycle);
    if (_mp3Phase != Phase::Idle)
        rescaleToClock(_mp3At, now, oldUnitsPerCycle, newUnitsPerCycle);
}

void NeoGSDma::runSd(int64_t now)
{
    const int64_t byteTime = static_cast<int64_t>(SD_BYTE_CLOCKS) * _host.dmaUnitsPerCycle();
    if (_sdPhase == Phase::SdWait)
    {
        int64_t t = _sdAt;
        for (int polls = 0; polls < kMaxPollBytesPerEvent; polls++)
        {
            t += byteTime;
            const uint8_t token = _sd ? _sd->exchange(0xFF) : 0xFF;
            _host.dmaSdByteDone(token);
            if (token == 0xFF)
                continue;
            if (token != 0xFE)
            {
                // Not a data token: stop with nothing written, still an interrupt
                finish(SD, true);
                return;
            }
            // Receive 512 data bytes and 2 CRC bytes into the FIFO
            for (int i = 0; i < BLOCK; i++)
                _sdBuffer[i] = _sd->exchange(0xFF);
            _sd->exchange(0xFF);
            _host.dmaSdByteDone(_sd->exchange(0xFF));
            _sdPhase = Phase::SdBurst;
            // The burst begins while the second CRC byte is still clocking
            _sdAt = t + static_cast<int64_t>(BLOCK + 1) * byteTime;
            return;
        }
        _sdAt = t; // still #FF: keep polling
        return;
    }

    if (_sdPhase == Phase::SdBurst)
    {
        const uint32_t base = address(SD);
        for (int i = 0; i < BLOCK; i++)
            _mem.ram()[(base + static_cast<uint32_t>(i)) % _mem.ramSize()] = _sdBuffer[i];
        advanceAddress(SD, BLOCK);
        _bytesMoved += BLOCK;
        const int64_t burst = static_cast<int64_t>(BLOCK * BURST_CLOCKS_PER_BYTE + GRANT_OVERHEAD_CLOCKS) * _host.dmaUnitsPerCycle();
        _host.dmaStall(burst);
        // Done at the end of the burst
        _sdPhase = Phase::Idle;
        _sdAt = kNever;
        _regs[SD][3] = static_cast<uint8_t>(_regs[SD][3] & 0x7F);
        _irq.raise(NeoGSInterrupts::REQ_SD_DMA);
        (void)now;
    }
}

void NeoGSDma::runMp3(int64_t now)
{
    if (_mp3Phase != Phase::Mp3Send || !_mp3)
    {
        if (_mp3Phase == Phase::Mp3Send)
            finish(MP3, true); // no decoder fitted: the bytes go nowhere
        return;
    }
    // One byte per MD byte time while DREQ is up; otherwise look again later
    const int64_t byteTime = static_cast<int64_t>(SD_BYTE_CLOCKS) * _host.dmaUnitsPerCycle();
    int64_t t = _mp3At;
    while (t <= now && _mp3Sent < BLOCK)
    {
        if (!_mp3->dreq(t))
        {
            t += 60 * byteTime; // ~50 us at 20 MHz
            continue;
        }
        _mp3->sdi()->exchange(_mp3Buffer[_mp3Sent++]);
        _bytesMoved++;
        t += byteTime;
    }
    if (_mp3Sent >= BLOCK)
    {
        finish(MP3, true);
        return;
    }
    _mp3At = t;
}

/// region <State snapshot>

// Layout: 0 version, 1 select, 2..13 registers, 14 sdPhase, 15 sdAt,
// 23 mp3Phase, 24 mp3At, 32 mp3Sent u16, 64 sdBuffer, 576 mp3Buffer
void NeoGSDma::saveState(uint8_t* dst) const
{
    using namespace statebytes;
    static_assert(64 + 2 * BLOCK <= STATE_SIZE);
    memset(dst, 0, STATE_SIZE);
    dst[0] = 1;
    dst[1] = _select;
    memcpy(dst + 2, _regs, sizeof _regs);
    dst[14] = static_cast<uint8_t>(_sdPhase);
    put64(dst + 15, _sdAt);
    dst[23] = static_cast<uint8_t>(_mp3Phase);
    put64(dst + 24, _mp3At);
    put16(dst + 32, static_cast<uint16_t>(_mp3Sent));
    memcpy(dst + 64, _sdBuffer, BLOCK);
    memcpy(dst + 64 + BLOCK, _mp3Buffer, BLOCK);
}

void NeoGSDma::loadState(const uint8_t* src)
{
    using namespace statebytes;
    _select = src[1];
    memcpy(_regs, src + 2, sizeof _regs);
    _sdPhase = static_cast<Phase>(src[14]);
    _sdAt = get64(src + 15);
    _mp3Phase = static_cast<Phase>(src[23]);
    _mp3At = get64(src + 24);
    _mp3Sent = get16(src + 32);
    memcpy(_sdBuffer, src + 64, BLOCK);
    memcpy(_mp3Buffer, src + 64 + BLOCK, BLOCK);
}

/// endregion </State snapshot>
