#include "neogszxdma.h"

#include <cmath>
#include <cstring>

#include "common/statebytes.h"

#include "emulator/sound/chips/neogs/neogsdma.h"
#include "emulator/sound/chips/neogs/neogsmemory.h"

/// region <Mode and watch window (neogs-zxdma-design.md §5.4)>

void NeoGSZxDma::setWatchAlways(bool always)
{
    _watchAlways = always;
    updateMode();
}

void NeoGSZxDma::setAvailable(bool available)
{
    _available = available;
    updateMode();
}

void NeoGSZxDma::reset()
{
    _pending = Pending::None;
    _pendingDone = kNever;
    _lastDoneAt = std::numeric_limits<int64_t>::min() / 2;
    _readLatch = 0xFF;
    _watchOpen = false;
    updateMode();
}

void NeoGSZxDma::shutdown()
{
    _available = false;
    _watchAlways = false;
    _mode = Mode::Off;
    if (_installed && _host.zxInstall(false))
        _installed = false;
}

void NeoGSZxDma::updateMode()
{
    Mode mode = Mode::Off;
    if (_available)
    {
        if (_dma.running(NeoGSDma::ZX))
            mode = Mode::Divert; // CST belongs to the module, whatever DMA_MOD shows now
        else if (_watchAlways || (_watchOpen && _dma.moduleSelect() == 1))
            mode = Mode::Watch;
    }
    _mode = mode;
    const bool want = mode != Mode::Off && _host.zxHostMemoryBus();
    if (want != _installed && _host.zxInstall(want))
        _installed = want;
}

int32_t NeoGSZxDma::watchFramesLeft() const
{
    if (_watchAlways)
        return static_cast<int32_t>(_watchFrames);
    if (!_watchOpen)
        return -1;
    const int32_t left = static_cast<int32_t>(_watchUntilFrame - _host.zxFrame());
    return left < 0 ? 0 : left;
}

void NeoGSZxDma::onModuleSelectWritten()
{
    if (_dma.moduleSelect() == 1)
        renewWatch();
    updateMode();
}

void NeoGSZxDma::onControlWritten(bool wasRunning, int64_t now)
{
    const bool running = _dma.running(NeoGSDma::ZX);
    if (!wasRunning && running && !_installed)
    {
        // Started while nothing watched the window: the host may already have
        // run past this moment (§5.5.3). Counted, never silent.
        int64_t hostNow = 0;
        if (_host.zxHostNowUnits(hostNow) && hostNow > now)
        {
            _lateStarts++;
            _lateStartUnits += static_cast<uint64_t>(hostNow - now);
            _host.zxLateStart(hostNow - now);
        }
    }
    if (wasRunning && !running)
    {
        // Abort / end: both state machines idle, /WAIT released, a pending
        // byte is lost (dma_zx.v:166-167, 266-267, 293-294)
        _pending = Pending::None;
        _pendingDone = kNever;
        renewWatch(); // a next block may follow
    }
    updateMode();
    _host.zxReschedule();
}

void NeoGSZxDma::onHostPortAccess()
{
    if (_available && _dma.moduleSelect() == 1)
        renewWatch();
    updateMode();
}

void NeoGSZxDma::onFrameEnd()
{
    if (_watchOpen && static_cast<int32_t>(_host.zxFrame() - _watchUntilFrame) >= 0 && !_dma.running(NeoGSDma::ZX))
        _watchOpen = false;
    updateMode();
}

/// endregion

/// region <Bytes (§5.6)>

void NeoGSZxDma::run(int64_t now)
{
    if (_pending != Pending::None && now >= _pendingDone)
        complete();
}

void NeoGSZxDma::complete()
{
    uint8_t& cell = _mem.ram()[_pendingAddress % _mem.ramSize()];
    if (_pending == Pending::Read)
        _readLatch = cell;
    else
        cell = _pendingData;
    _dma.advanceAddress(NeoGSDma::ZX, 1); // the address moves at the grant (dma_zx.v:110-111)
    _lastDoneAt = _pendingDone;
    _pending = Pending::None;
    _pendingDone = kNever;
}

bool NeoGSZxDma::syncForAccess(int64_t& accessEnd)
{
    // Everything the card did before this access has happened: CST on or
    // off, the previous byte's grant (§5.5.2)
    _host.zxCatchUp();
    if (_mode != Mode::Divert)
        return false;
    return _host.zxHostNowUnits(accessEnd);
}

void NeoGSZxDma::waitForPending(int64_t& accessEnd, Pending incoming)
{
    // The host calls the memory interface after the 3 T of the access;
    // /WAIT is sampled in T2, 2 T before its end
    const double unitsPerT = _host.zxUnitsPerHostT();
    const int64_t sample = accessEnd - std::llround(2 * unitsPerT);
    const int64_t done = _pending != Pending::None ? _pendingDone : _lastDoneAt;

    if (_pending != Pending::None && _pending != incoming && done > sample)
    {
        // A read while a write waits for its grant, or the other way round:
        // the request is dropped "to prevent dead ends" (dma_zx.v:214, 238,
        // 310, 326) - no wait, the byte is lost
        _pending = Pending::None;
        _pendingDone = kNever;
        _bytesDropped++;
        return;
    }

    if (done > sample)
    {
        const uint32_t wait = static_cast<uint32_t>(std::ceil(static_cast<double>(done - sample) / unitsPerT));
        _host.zxAddHostWait(wait);
        _waitTStates += wait;
        accessEnd += std::llround(wait * unitsPerT);
        _host.zxCatchUp(); // the card runs through the wait: the byte completes
    }
    run(accessEnd);
}

void NeoGSZxDma::stallCard(int64_t requestAt, int64_t& doneAt)
{
    // The sequencer takes turns while busy: during an SD / MP3 burst the byte
    // gets the next slot (dma_sequencer.v:86, 177)
    if (_host.zxStallUntil() > requestAt)
        doneAt += clocks(BURST_SLOT_CLOCKS);
    _host.zxStall(clocks(CARD_STALL_CLOCKS));
}

uint8_t NeoGSZxDma::onRead(uint16_t /*addr*/, uint8_t normal, bool /*isExecution*/, bool romPaged)
{
    int64_t end = 0;
    if (!syncForAccess(end))
        return normal;
    waitForPending(end, Pending::Read);
    if (_mode != Mode::Divert || !romPaged)
        return normal; // RAM paged: no DMA cycle, but the wait above applied (zxbus.v:248)

    // The byte fetched by the previous read; this read starts the next fetch
    const uint8_t result = _readLatch;
    const int64_t start = end - std::llround(3 * _host.zxUnitsPerHostT());
    _pending = Pending::Read;
    _pendingAddress = _dma.address(NeoGSDma::ZX);
    _pendingDone = start + clocks(READ_DONE_CLOCKS);
    stallCard(start, _pendingDone);
    _bytesRead++;
    _host.zxTrace(false, _pendingAddress, result);
    _host.zxReschedule();
    return result;
}

void NeoGSZxDma::onWrite(uint16_t /*addr*/, uint8_t value, bool /*romPaged*/)
{
    int64_t end = 0;
    if (!syncForAccess(end))
        return;
    waitForPending(end, Pending::Write);
    if (_mode != Mode::Divert)
        return;

    // Captured at the end of the host's write; written at the grant
    _pending = Pending::Write;
    _pendingAddress = _dma.address(NeoGSDma::ZX);
    _pendingData = value;
    _pendingDone = end + clocks(WRITE_GRANT_CLOCKS);
    stallCard(end, _pendingDone);
    _bytesWritten++;
    _host.zxTrace(true, _pendingAddress, value);
    _host.zxReschedule();
}

/// endregion

/// region <State snapshot>

// Layout: 0 version, 1 flags (watchOpen), 2 readLatch, 3 pending kind,
// 4 pendingData, 5 pendingAddress u32, 9 pendingDone, 17 lastDoneAt,
// 25 watchUntilFrame u32, 29 bytesRead, 37 bytesWritten, 45 bytesDropped,
// 53 waitTStates, 61 lateStarts, 69 lateStartUnits
void NeoGSZxDma::saveState(uint8_t* dst) const
{
    using namespace statebytes;
    static_assert(77 <= STATE_SIZE);
    memset(dst, 0, STATE_SIZE);
    dst[0] = 1;
    dst[1] = _watchOpen ? 1 : 0;
    dst[2] = _readLatch;
    dst[3] = static_cast<uint8_t>(_pending);
    dst[4] = _pendingData;
    put32(dst + 5, _pendingAddress);
    put64(dst + 9, _pendingDone);
    put64(dst + 17, _lastDoneAt);
    put32(dst + 25, _watchUntilFrame);
    putU64(dst + 29, _bytesRead);
    putU64(dst + 37, _bytesWritten);
    putU64(dst + 45, _bytesDropped);
    putU64(dst + 53, _waitTStates);
    putU64(dst + 61, _lateStarts);
    putU64(dst + 69, _lateStartUnits);
}

void NeoGSZxDma::loadState(const uint8_t* src)
{
    using namespace statebytes;
    _watchOpen = (src[1] & 1) != 0;
    _readLatch = src[2];
    _pending = static_cast<Pending>(src[3]);
    _pendingData = src[4];
    _pendingAddress = get32(src + 5);
    _pendingDone = get64(src + 9);
    _lastDoneAt = get64(src + 17);
    _watchUntilFrame = get32(src + 25);
    _bytesRead = getU64(src + 29);
    _bytesWritten = getU64(src + 37);
    _bytesDropped = getU64(src + 45);
    _waitTStates = getU64(src + 53);
    _lateStarts = getU64(src + 61);
    _lateStartUnits = getU64(src + 69);
    updateMode();
}

/// endregion
