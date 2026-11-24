#include "stdafx.h"

#include "ttdvdac4.h"

#include <cstring>

#include "emulator/platforms/tsconf/vdac4/esp/espapphost.h"
#include "emulator/platforms/tsconf/vdac4/esp/espxtensaruntime.h"
#include "emulator/platforms/tsconf/vdac4/vdac4board.h"
#include "emulator/platforms/tsconf/vdac4/vdac4esp.h"
#include "emulator/platforms/tsconf/vdac4/vdac4espstate.h"
#include "emulator/platforms/tsconf/vdac4/vdac4tang.h"

namespace ttd
{

namespace
{

/// 64-bit hash, 8 bytes a step (the host state is hashed at every capture)
uint64_t Hash64(const uint8_t* data, size_t size, uint64_t seed = 0x9E3779B97F4A7C15ull)
{
    uint64_t h = seed ^ (size * 0xFF51AFD7ED558CCDull);
    size_t i = 0;
    for (; i + 8 <= size; i += 8)
    {
        uint64_t w;
        std::memcpy(&w, data + i, 8);
        h = (h ^ (w * 0xC4CEB9FE1A85EC53ull)) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    for (; i < size; ++i)
        h = (h ^ data[i]) * 0x100000001B3ull;
    return h ^ (h >> 32);
}

/// A self-delimiting blob: u32 total length (itself included), then the body
void BeginSized(std::vector<uint8_t>& out)
{
    out.assign(4, 0);
}
void EndSized(std::vector<uint8_t>& out)
{
    const uint32_t size = static_cast<uint32_t>(out.size());
    std::memcpy(out.data(), &size, 4);
}
uint32_t SizedLength(const uint8_t* src)
{
    uint32_t size = 0;
    std::memcpy(&size, src, 4);
    return size;
}

/// Memory with its zero runs dropped: (u32 zeros, u32 literal bytes, bytes)... up to `size`
void PutSparse(Vdac4EspStateWriter& w, const std::vector<uint8_t>& memory)
{
    const size_t size = memory.size();
    size_t i = 0;
    while (i < size)
    {
        size_t zeros = 0;
        while (i + zeros < size && memory[i + zeros] == 0)
            ++zeros;
        size_t literal = 0;
        // A literal run ends at 32 zero bytes in a row (shorter zero gaps stay in it)
        size_t gap = 0;
        size_t j = i + zeros;
        while (j < size && gap < 32)
        {
            gap = memory[j] == 0 ? gap + 1 : 0;
            ++j;
        }
        literal = j - (i + zeros) - (gap >= 32 ? gap : 0);
        w.U32(static_cast<uint32_t>(zeros));
        w.U32(static_cast<uint32_t>(literal));
        w.Bytes(memory.data() + i + zeros, literal);
        i += zeros + literal;
    }
}

bool GetSparse(Vdac4EspStateReader& r, std::vector<uint8_t>& memory)
{
    const size_t size = memory.size();
    std::vector<uint8_t> rebuilt(size, 0);
    size_t i = 0;
    while (i < size && r.Ok())
    {
        const uint32_t zeros = r.U32();
        const uint32_t literal = r.U32();
        if (!r.Ok() || zeros > size - i || literal > size - i - zeros)
            return false;
        i += zeros;
        r.Bytes(rebuilt.data() + i, literal);
        i += literal;
    }
    if (!r.Ok())
        return false;
    memory.swap(rebuilt);
    return true;
}

constexpr uint32_t kEspMagic = 0x45344456;      // "VD4E"
constexpr uint32_t kCpuFullMagic = 0x46344456;  // "VD4F": the host now and its memory
constexpr uint32_t kCpuKeyMagic = 0x4B344456;   // "VD4K": keyframe state + journal (the engine's blob)
constexpr size_t kEspMaxState = 12u << 20;      // the firmware's resources (7.5 MB at most) and the rest
constexpr size_t kCpuMaxState = 16u << 20;      // memory 8.7 MB, host state, journal
constexpr size_t kCpuMaxEngineBlob = 4u << 20;  // the host state (tens of KB), a journal (an APP_START carries its ELF)

}  // namespace

/// region <Vdac4Tang>

namespace
{
constexpr size_t kTangStateSize = 48;

void PutTang(const Vdac4Tang::State& s, std::vector<uint8_t>& out)
{
    out.clear();
    Vdac4EspStateWriter w(out);
    w.U8(static_cast<uint8_t>(s.personality));
    w.U8(s.armed);
    w.U8(s.msel);
    w.U8(s.selected);
    w.U32(s.ftCfg);
    w.U64(s.armAt);
    w.U64(s.loadDoneAt);
    w.U64(s.lastEvoTraffic);
    w.U32(s.windowBytes);
    w.U8(s.windowFirst);
    w.U8(s.windowAllZero);
    w.U8(s.ftSeen);
    w.U8(s.reserved);
    w.U32(s.ftAddr);
    w.U32(s.scratch);
}
}  // namespace

size_t TTDVdac4Tang::TTDStateSize() const
{
    return kTangStateSize;
}

void TTDVdac4Tang::TTDSaveState(uint8_t* dst) const
{
    std::vector<uint8_t> bytes;
    PutTang(_tang.GetState(), bytes);
    std::memcpy(dst, bytes.data(), kTangStateSize);
}

void TTDVdac4Tang::TTDLoadState(const uint8_t* src)
{
    Vdac4EspStateReader r(src, kTangStateSize);
    Vdac4Tang::State s;
    const uint8_t personality = r.U8();
    s.personality = personality == 0 ? Vdac4Tang::Personality::Loading
                    : personality == 2 ? Vdac4Tang::Personality::Ft
                                       : Vdac4Tang::Personality::Ts;
    s.armed = r.U8();
    s.msel = r.U8();
    s.selected = r.U8();
    s.ftCfg = r.U32();
    s.armAt = r.U64();
    s.loadDoneAt = r.U64();
    s.lastEvoTraffic = r.U64();
    s.windowBytes = r.U32();
    s.windowFirst = r.U8();
    s.windowAllZero = r.U8();
    s.ftSeen = r.U8();
    s.reserved = r.U8();
    s.ftAddr = r.U32();
    s.scratch = r.U32();
    if (r.Ok())
        _tang.SetState(s);
}

uint64_t TTDVdac4Tang::TTDHashState() const
{
    std::vector<uint8_t> bytes;
    PutTang(_tang.GetState(), bytes);
    return Hash64(bytes.data(), bytes.size());
}

/// endregion </Vdac4Tang>

/// region <Vdac4Esp>

size_t TTDVdac4Esp::TTDStateSize() const
{
    return kEspMaxState;
}

void TTDVdac4Esp::TTDSaveStateTo(std::vector<uint8_t>& out) const
{
    BeginSized(out);
    Vdac4EspStateWriter w(out);
    w.U32(kEspMagic);
    const Vdac4Board::ClockState clock = _board.GetClock();
    w.U64(clock.lastNs);
    w.U64(clock.offsetNs);
    w.U64(clock.backSteps);
    w.U64(_baseUs);
    w.U64(_baseFrames);
    std::vector<uint8_t> esp;
    _board.Esp().Save(esp);
    w.Blob(esp);
    EndSized(out);
}

void TTDVdac4Esp::TTDSaveState(uint8_t* dst) const
{
    std::vector<uint8_t> out;
    TTDSaveStateTo(out);
    std::memcpy(dst, out.data(), out.size());
}

void TTDVdac4Esp::TTDLoadState(const uint8_t* src)
{
    const uint32_t size = SizedLength(src);
    if (size < 4 || size > kEspMaxState)
        return;
    Vdac4EspStateReader r(src + 4, size - 4);
    if (r.U32() != kEspMagic)
        return;
    Vdac4Board::ClockState clock;
    clock.lastNs = r.U64();
    clock.offsetNs = r.U64();
    clock.backSteps = r.U64();
    const uint64_t baseUs = r.U64();
    const uint64_t baseFrames = r.U64();
    std::vector<uint8_t> esp;
    if (!r.Blob(esp, kEspMaxState) || !r.AtEnd())
        return;
    // A job in flight reached a machine that is restored now: nothing of it is handed over
    _board.Runtime().DropJobs();
    _board.SetClock(clock);
    _baseUs = baseUs;
    _baseFrames = baseFrames;
    _board.Esp().Load(esp.data(), esp.size());
}

uint64_t TTDVdac4Esp::TTDHashState() const
{
    std::vector<uint8_t> out;
    TTDSaveStateTo(out);
    return Hash64(out.data(), out.size());
}

TTDDeviceDescriptor TTDVdac4Esp::TTDDescribe() const
{
    TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
    // The ESP talks to the Tang (LINK) and its FT812: they are restored first
    d.restoreAfter.push_back({TTDDeviceType::Vdac4Tang, "vdac4tang"});
    return d;
}

/// endregion </Vdac4Esp>

/// region <Vdac4EspCpu>

TTDVdac4EspCpu::TTDVdac4EspCpu(Vdac4Board& board) : _board(board)
{
    vdac4::EspMemory& memory = _board.Runtime().Host().Memory();
    _trackers[0].Bind(memory.PsramBytes().data(), memory.PsramBytes().size());
    _trackers[1].Bind(memory.SramBytes().data(), memory.SramBytes().size());
    _trackers[2].Bind(memory.RomBytes().data(), memory.RomBytes().size());
}

size_t TTDVdac4EspCpu::TTDStateSize() const
{
    return kCpuMaxState;
}

void TTDVdac4EspCpu::TTDSaveStateTo(std::vector<uint8_t>& out) const
{
    // A job in flight (a frame end, a bus access) writes this memory on the ESP thread: the state is the one after it
    _board.Runtime().WaitIdle();
    vdac4::EspAppHost& host = _board.Runtime().Host();
    BeginSized(out);
    Vdac4EspStateWriter w(out);
    w.U32(kCpuFullMagic);
    host.SaveState(_scratch);
    w.Blob(_scratch);
    vdac4::EspMemory& memory = host.Memory();
    PutSparse(w, memory.PsramBytes());
    PutSparse(w, memory.SramBytes());
    PutSparse(w, memory.RomBytes());
    EndSized(out);
}

void TTDVdac4EspCpu::TTDSaveState(uint8_t* dst) const
{
    std::vector<uint8_t> out;
    TTDSaveStateTo(out);
    std::memcpy(dst, out.data(), out.size());
}

void TTDVdac4EspCpu::TTDLoadState(const uint8_t* src)
{
    const uint32_t size = SizedLength(src);
    if (size < 4 || size > kCpuMaxState)
        return;
    Vdac4EspStateReader r(src + 4, size - 4);
    if (r.U32() != kCpuFullMagic)
        return;
    std::vector<uint8_t> state;
    if (!r.Blob(state, kCpuMaxState))
        return;
    vdac4::EspAppHost& host = _board.Runtime().Host();
    vdac4::EspMemory& memory = host.Memory();
    std::vector<uint8_t> psram(memory.PsramBytes().size()), sram(memory.SramBytes().size()),
        rom(memory.RomBytes().size());
    if (!GetSparse(r, psram) || !GetSparse(r, sram) || !GetSparse(r, rom) || !r.AtEnd())
        return;
    _board.Runtime().DropJobs();
    // In place: the CPU maps these buffers
    std::memcpy(memory.PsramBytes().data(), psram.data(), psram.size());
    std::memcpy(memory.SramBytes().data(), sram.data(), sram.size());
    std::memcpy(memory.RomBytes().data(), rom.data(), rom.size());
    host.LoadState(state.data(), state.size());
    // The memory moved without marks, the journal does not start from it: the next capture is a keyframe
    _keyDue = true;
    MarkAllMemory();
}

uint64_t TTDVdac4EspCpu::HostHash() const
{
    _board.Runtime().WaitIdle();
    _board.Runtime().Host().SaveState(_scratch);
    return Hash64(_scratch.data(), _scratch.size());
}

uint64_t TTDVdac4EspCpu::TTDHashState() const
{
    return HostHash();
}

TTDDeviceDescriptor TTDVdac4EspCpu::TTDDescribe() const
{
    TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
    // The firmware's load sets the runtime's view of the app; the host comes after it
    d.restoreAfter.push_back({TTDDeviceType::Vdac4Esp, "vdac4esp"});
    return d;
}

void TTDVdac4EspCpu::TTDRecordingStarted()
{
    _keyDue = true;
}

void TTDVdac4EspCpu::TTDRecordingStopped()
{
    _board.Runtime().Host().JournalStop();
    _keyState.clear();
    _keyState.shrink_to_fit();
    _keyDue = true;
}

void TTDVdac4EspCpu::TTDRegions(std::vector<TTDDeviceRegion>& out)
{
    static constexpr TTDRegionId kIds[] = {TTDRegionId::Vdac4EspPsram, TTDRegionId::Vdac4EspSram,
                                           TTDRegionId::Vdac4EspRom};
    static constexpr const char* kNames[] = {"vdac4.esp.psram", "vdac4.esp.sram", "vdac4.esp.rom"};
    for (size_t i = 0; i < 3; ++i)
    {
        TTDDeviceRegion r;
        r.desc.id = kIds[i];
        r.desc.name = kNames[i];
        r.desc.ownerType = static_cast<uint16_t>(TTDDeviceType::Vdac4EspCpu);
        r.desc.memory = _trackers[i].Memory();
        r.desc.bytes = static_cast<uint32_t>(_trackers[i].Bytes());
        r.desc.pieces = _trackers[i].Pieces();
        r.tracker = &_trackers[i];
        out.push_back(r);
    }
}

void TTDVdac4EspCpu::MarkAllMemory()
{
    for (TTDRegionTracker& tracker : _trackers)
        tracker.MarkAll();
}

void TTDVdac4EspCpu::TTDCaptureBegins(bool rescan)
{
    vdac4::EspXtensaRuntime& runtime = _board.Runtime();
    vdac4::EspAppHost& host = runtime.Host();
    runtime.WaitIdle();  // a frame end flushed it already; a capture never sees a job running
    ++_sinceKey;
    const bool key = rescan || _keyDue || _keyState.empty() || host.Journal() != vdac4::EspAppHost::JournalMode::Record ||
                     _sinceKey >= kKeyframeFrames || host.JournalBytes().size() > kJournalLimit;
    if (key)
    {
        host.SaveState(_keyState);
        host.JournalBegin();
        _sinceKey = 0;
        _keyDue = false;
        ++_stats.keyframes;
        // The engine stores the pieces that changed since the last keyframe (a rescan offers them all anyway)
        if (_armed && !rescan)
            MarkAllMemory();
        _hashNow = Hash64(_keyState.data(), _keyState.size());
    }
    else
    {
        (void)host.JournalBytes();  // the read run in progress closed: the blob holds every input so far
        _hashNow = HostHash();
    }
}

void TTDVdac4EspCpu::TTDBeforeRestore()
{
    // Before anything is restored: a job in flight (a pause inside a frame) finishes, nothing of it is handed
    // over, and its FT worker's writes land before the Tang and its chip are restored, not after
    _board.Runtime().DropJobs();
    // Between keyframes the ESP memory moves on unmarked, and a restore re-runs it: every piece is restored
    if (_armed)
        MarkAllMemory();
}

size_t TTDVdac4EspCpu::TTDStateWithoutRegionsLimit() const
{
    // The keyframe's host state, a journal a little over its limit (the capture that passes it keys the next),
    // the framing
    return kCpuMaxEngineBlob;
}

bool TTDVdac4EspCpu::TTDStateWithoutRegions(std::vector<uint8_t>& state) const
{
    state.clear();
    Vdac4EspStateWriter w(state);
    w.U32(kCpuKeyMagic);
    w.Blob(_keyState);
    w.Blob(_board.Runtime().Host().JournalData());
    w.U64(_hashNow);
    return true;
}

bool TTDVdac4EspCpu::TTDLoadStateWithoutRegions(const uint8_t* state, size_t size)
{
    Vdac4EspStateReader r(state, size);
    if (r.U32() != kCpuKeyMagic)
        return false;
    std::vector<uint8_t> key, journal;
    r.Blob(key, kCpuMaxState);
    r.Blob(journal, kCpuMaxState);
    const uint64_t hash = r.U64();
    if (!r.Ok() || !r.AtEnd())
        return false;
    if (key.empty())
        return true;  // nothing recorded yet (the registry's probe)

    // The engine restored the keyframe's memory; the keyframe's state, then the ESP again up to this frame
    vdac4::EspXtensaRuntime& runtime = _board.Runtime();
    vdac4::EspAppHost& host = runtime.Host();
    runtime.DropJobs();
    ++_stats.restores;
    if (!host.LoadState(key.data(), key.size()))
    {
        ++_stats.divergences;
        _stats.lastDivergence = "keyframe state does not load";
        return false;
    }
    std::string error;
    const bool replayed = host.JournalReplay(journal.data(), journal.size(), error);
    _stats.replayedBytes += journal.size();
    if (!replayed || HostHash() != hash)
    {
        ++_stats.divergences;
        _stats.lastDivergence = !replayed ? error : "replay ended at another host state";
    }
    _keyState.swap(key);
    // The next capture is a rescan (the engine's after every restore): a keyframe
    _keyDue = true;
    return true;
}

/// endregion </Vdac4EspCpu>

}  // namespace ttd
