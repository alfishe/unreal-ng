#include "stdafx.h"

#include "ttdvdac4.h"

#include <algorithm>
#include <cstring>

#include "emulator/platforms/tsconf/vdac4/esp/espapphost.h"
#include "emulator/platforms/tsconf/vdac4/esp/espxtensaruntime.h"
#include "emulator/platforms/tsconf/vdac4/vdac4board.h"
#include "emulator/platforms/tsconf/vdac4/vdac4bomzh.h"
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
uint64_t Word(const uint8_t* at)
{
    uint64_t w;
    std::memcpy(&w, at, sizeof(w));
    return w;
}

/// Runs of zeros and literals; a literal run ends where 32 zero bytes in a row begin (shorter zero gaps stay in
/// it). Scanned by 8-byte words: a run of 32 zeros holds at least three whole aligned words
void PutSparse(Vdac4EspStateWriter& w, const uint8_t* m, size_t size)
{
    auto zeroEnd = [&](size_t k) {
        while (k < size && (k & 7) != 0 && m[k] == 0)
            ++k;
        while (k + 8 <= size && Word(m + k) == 0)
            k += 8;
        while (k < size && m[k] == 0)
            ++k;
        return k;
    };
    size_t i = 0;
    while (i < size)
    {
        const size_t start = zeroEnd(i);  // m[start] is not zero (or the end)
        size_t end = size;                // where the literal ends
        size_t k = (start + 8) & ~size_t(7);
        while (k + 8 <= size)
        {
            if (Word(m + k) != 0)
            {
                k += 8;
                continue;
            }
            size_t runStart = k;
            while (runStart > start && m[runStart - 1] == 0)
                --runStart;
            const size_t runEnd = zeroEnd(k);
            if (runEnd - runStart >= 32)
            {
                end = runStart;
                break;
            }
            k = (runEnd + 7) & ~size_t(7);
        }
        w.U32(static_cast<uint32_t>(start - i));
        w.U32(static_cast<uint32_t>(end - start));
        w.Bytes(m + start, end - start);
        i = end;
    }
}

void PutSparse(Vdac4EspStateWriter& w, const std::vector<uint8_t>& memory)
{
    PutSparse(w, memory.data(), memory.size());
}

/// PutSparse's runs into `memory` (`size` bytes). `write` false only checks that they fill it exactly
bool GetSparse(Vdac4EspStateReader& r, uint8_t* memory, size_t size, bool write)
{
    size_t i = 0;
    while (i < size && r.Ok())
    {
        const uint32_t zeros = r.U32();
        const uint32_t literal = r.U32();
        if (!r.Ok() || zeros > size - i || literal > size - i - zeros)
            return false;
        if (write)
            std::memset(memory + i, 0, zeros);
        i += zeros;
        if (write)
            r.Bytes(memory + i, literal);
        else
            r.Skip(literal);
        i += literal;
    }
    return r.Ok() && i == size;
}

constexpr uint32_t kEspMagic = 0x45344456;      // "VD4E"
constexpr uint32_t kCpuFullMagic = 0x46344456;  // "VD4F": the host now and its memory
constexpr uint32_t kCpuKeyMagic = 0x4B344456;   // "VD4K": keyframe state + journal (the engine's blob)
constexpr uint32_t kCpuKey2Magic = 0x4C344456;  // "VD4L": keyframe state + the Quake II core's + journal
constexpr size_t kEspMaxState = 12u << 20;      // the firmware's resources (7.5 MB at most) and the rest
constexpr size_t kCpuMaxState = 16u << 20;      // memory 8.7 MB, host state, journal
constexpr size_t kCpuMaxEngineBlob = 4u << 20;  // the host state (tens of KB), a journal (an APP_START carries its ELF)
constexpr uint32_t kQ2FullMagic = 0x51344456;   // "VD4Q": the Quake II core's state and its SDRAM
constexpr size_t kQ2MaxKey = 256u * 1024;       // Vdac4BomzhCore::Save: about 33 KB, a command and a WRITE chunk
constexpr size_t kQ2MaxEngineBlob = 1u << 20;   // registers and tables (about 33 KB), a picture (at most 3 MB: never)
constexpr size_t kQ2MaxState = 12u << 20;       // the SDRAM's runs (8 MB and their headers), the blob

/// Vdac4EspCpu's engine blob, read: the ESP's keyframe state, the Quake II core's (empty: not running, or a blob
/// from before it), the journal since, the hash now, the journal length at the capture before
struct CpuKeyBlob
{
    std::vector<uint8_t> key, q2Key, journal;
    uint64_t hash = 0;
    uint64_t prev = 0;
};

bool ReadCpuKeyBlob(const uint8_t* state, size_t size, CpuKeyBlob& out, size_t maxState)
{
    Vdac4EspStateReader r(state, size);
    const uint32_t magic = r.U32();
    if (magic != kCpuKeyMagic && magic != kCpuKey2Magic)
        return false;
    r.Blob(out.key, maxState);
    if (magic == kCpuKey2Magic)
        r.Blob(out.q2Key, kQ2MaxKey);
    r.Blob(out.journal, maxState);
    out.hash = r.U64();
    out.prev = r.AtEnd() ? 0 : r.U64();
    return r.Ok() && r.AtEnd();
}

/// The replay point's key: the ESP's keyframe and the core's with it
uint64_t KeyHash(const std::vector<uint8_t>& key, const std::vector<uint8_t>& q2Key)
{
    const uint64_t h = Hash64(key.data(), key.size());
    return q2Key.empty() ? h : Hash64(q2Key.data(), q2Key.size(), h);
}

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
                    : personality == 3 ? Vdac4Tang::Personality::Q2
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
    std::vector<uint8_t>* parts[] = {&memory.PsramBytes(), &memory.SramBytes(), &memory.RomBytes()};
    // A check first (the memory stays as it is if the state does not fit), then straight into the buffers the
    // CPU maps: no copy of 9 MB to allocate and clear on every live-state restore of a seek
    Vdac4EspStateReader check = r;
    for (std::vector<uint8_t>* part : parts)
        if (!GetSparse(check, part->data(), part->size(), false))
            return;
    if (!check.AtEnd())
        return;
    _board.Runtime().DropJobs();
    for (std::vector<uint8_t>* part : parts)
        (void)GetSparse(r, part->data(), part->size(), true);
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
    _points.clear();  // another session's memory: let it go
}

void TTDVdac4EspCpu::TTDRecordingStopped()
{
    _board.Runtime().Host().JournalStop();
    _keyState.clear();
    _keyState.shrink_to_fit();
    _q2Key.clear();
    _q2Key.shrink_to_fit();
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
    // The Quake II core started or stopped, or took a write no replay gives back (the firmware's): a keyframe, so
    // the core runs, or does not, for the whole span and the span's replay is all its input
    Vdac4Tang& tang = _board.Tang();
    const uint64_t q2Epoch = tang.Q2Epoch();
    const uint64_t q2Outside = tang.Q2OutsideInputs();
    const bool q2Moved = q2Epoch != _q2Epoch || q2Outside != _q2Outside;
    _q2Epoch = q2Epoch;
    _q2Outside = q2Outside;
    // A journal over its limit keys this capture, so the blob carries it no more: whatever one frame sent (an
    // APP_START's ELF of megabytes), the blob stays the host state and at most kJournalLimit of journal
    const bool key = rescan || _keyDue || _keyState.empty() || host.Journal() != vdac4::EspAppHost::JournalMode::Record ||
                     _sinceKey >= kKeyframeFrames || host.JournalBytes().size() > kJournalLimit || q2Moved;
    _keyedNow = key;
    if (key)
    {
        host.SaveState(_keyState);
        host.JournalBegin();
        _q2Key.clear();
        if (const Vdac4BomzhCore* core = tang.Q2Core())
        {
            Vdac4EspStateWriter w(_q2Key);
            core->Save(w);
        }
        _sinceKey = 0;
        _keyDue = false;
        ++_stats.keyframes;
        // The engine stores the pieces that changed since the last keyframe (a rescan offers them all anyway)
        if (_armed && !rescan)
            MarkAllMemory();
        _hashNow = Hash64(_keyState.data(), _keyState.size());
        _journalLast = _journalPrev = 0;
    }
    else
    {
        // the read run in progress closed: the blob holds every input so far
        _journalPrev = _journalLast;
        _journalLast = host.JournalBytes().size();
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
    // A seek inside one run of an app brings the same code bytes back: the load keeps the translated code then
    _board.Runtime().Host().HoldCode();
    _regionsLeft = false;
}

bool TTDVdac4EspCpu::TTDLoadRestoresRegions(const uint8_t* state, size_t size)
{
    // The load below goes on from a replay point when one has this keyframe and a journal this one begins with:
    // it copies the point's memory whole, so the engine need not decode the keyframe's first
    _regionsLeft = false;
    CpuKeyBlob blob;
    if (!ReadCpuKeyBlob(state, size, blob, kCpuMaxState) || blob.key.empty())
        return false;
    _regionsLeft = FindReplayPoint(KeyHash(blob.key, blob.q2Key), blob.journal) != nullptr;
    return _regionsLeft;
}

size_t TTDVdac4EspCpu::TTDStateWithoutRegionsLimit() const
{
    // The keyframe's host state (tens of KB), a journal of at most kJournalLimit (a capture that finds more keys
    // itself, TTDCaptureBegins), the framing
    return kCpuMaxEngineBlob;
}

bool TTDVdac4EspCpu::TTDStateWithoutRegions(std::vector<uint8_t>& state) const
{
    state.clear();
    Vdac4EspStateWriter w(state);
    // The keyframe's parts first, the journal growing last: a capture stores the pieces of the blob that changed
    w.U32(kCpuKey2Magic);
    w.Blob(_keyState);
    w.Blob(_q2Key);
    w.Blob(_board.Runtime().Host().JournalData());
    w.U64(_hashNow);
    w.U64(_journalPrev);
    return true;
}

bool TTDVdac4EspCpu::TTDLoadDiverged(std::string& detail)
{
    if (_loadDivergence.empty())
        return false;
    detail.swap(_loadDivergence);
    _loadDivergence.clear();
    return true;
}

bool TTDVdac4EspCpu::TTDLoadStateWithoutRegions(const uint8_t* state, size_t size)
{
    _loadDivergence.clear();
    CpuKeyBlob blob;
    if (!ReadCpuKeyBlob(state, size, blob, kCpuMaxState))
        return false;
    std::vector<uint8_t>& key = blob.key;
    std::vector<uint8_t>& journal = blob.journal;
    const uint64_t hash = blob.hash;
    const uint64_t prev = blob.prev;
    const bool regionsLeft = _regionsLeft;
    _regionsLeft = false;
    if (key.empty())
        return true;  // nothing recorded yet (the registry's probe)

    // The engine restored the keyframe's memory; the keyframe's state, then the ESP again up to this frame. A
    // replay point on the way (the same keyframe, a journal this one begins with) saves the start of that run
    vdac4::EspXtensaRuntime& runtime = _board.Runtime();
    vdac4::EspAppHost& host = runtime.Host();
    runtime.DropJobs();
    ++_stats.restores;
    const uint64_t keyHash = KeyHash(key, blob.q2Key);

    // The Quake II core ran at the keyframe: it goes with the ESP (the Tang, restored first, runs it again), fed the
    // LINK writes of the replay. The engine restored its SDRAM to the keyframe's (the region moves at keyframes only)
    Vdac4Tang& tang = _board.Tang();
    Vdac4BomzhCore* q2 = nullptr;
    if (!blob.q2Key.empty())
    {
        q2 = tang.Q2Core() != nullptr ? tang.Q2Model() : nullptr;
        if (q2 == nullptr)
        {
            ++_stats.divergences;
            _stats.lastDivergence = _loadDivergence = "the Quake II core of the keyframe does not run";
        }
    }
    struct FeedQ2
    {
        vdac4::EspAppHost& host;
        explicit FeedQ2(vdac4::EspAppHost& h, Vdac4Tang* tang) : host(h)
        {
            if (tang == nullptr)
                return;
            host.SetReplayLinkSink(
                [tang](uint32_t address, uint32_t value) { tang->Q2ReplayLinkWrite(address, value); },
                [tang](uint32_t ftAddress, const uint8_t* words, size_t count) {
                    tang->Q2ReplayLinkFtWrite(ftAddress, words, count);
                });
        }
        ~FeedQ2() { host.SetReplayLinkSink({}, {}); }
        FeedQ2(const FeedQ2&) = delete;
        FeedQ2& operator=(const FeedQ2&) = delete;
    } feed(host, q2 ? &tang : nullptr);

    size_t from = 0;
    if (const ReplayPoint* point = FindReplayPoint(keyHash, journal))
    {
        vdac4::EspMemory& memory = host.Memory();
        // In place: the CPU maps these buffers
        std::copy(point->psram.begin(), point->psram.end(), memory.PsramBytes().begin());
        std::copy(point->sram.begin(), point->sram.end(), memory.SramBytes().begin());
        std::copy(point->rom.begin(), point->rom.end(), memory.RomBytes().begin());
        bool q2Back = q2 == nullptr;
        if (q2 != nullptr && point->q2Sdram.size() == Vdac4BomzhCore::kMemoryBytes)
        {
            Vdac4EspStateReader r(point->q2State.data(), point->q2State.size());
            q2Back = q2->Load(r);
            if (q2Back)
                std::copy(point->q2Sdram.begin(), point->q2Sdram.end(), q2->Memory());
        }
        if (q2Back && host.LoadState(point->state.data(), point->state.size()))
        {
            host.SetJournalReadMap(point->reads);
            from = point->journal.size();
            ++_stats.replayPointHits;
        }
        else
            _points.clear();  // cannot happen (its own save): start over from the keyframe
    }
    if (from == 0 && regionsLeft)
    {
        // Cannot happen (the same blob found the point a moment ago, its own save loads): the engine left the
        // memory to the point, so the keyframe's is not in it
        ++_stats.divergences;
        _stats.lastDivergence = _loadDivergence = "replay point lost: the ESP memory was not restored";
        _keyDue = true;
        return true;
    }
    if (from > 0 && regionsLeft)
        ++_stats.regionsLeft;
    if (from == 0 && !host.LoadState(key.data(), key.size()))
    {
        ++_stats.divergences;
        _stats.lastDivergence = "keyframe state does not load";
        return false;
    }
    if (from == 0 && q2 != nullptr)
    {
        Vdac4EspStateReader r(blob.q2Key.data(), blob.q2Key.size());
        if (!q2->Load(r) || !r.AtEnd())
        {
            ++_stats.divergences;
            _stats.lastDivergence = _loadDivergence = "the Quake II core's keyframe state does not load";
        }
    }
    std::string error;
    bool replayed = true;
    if (prev > from && prev < journal.size())
    {
        replayed = host.JournalReplay(journal.data(), journal.size(), from, prev, error);
        if (replayed)
            KeepReplayPoint(keyHash, journal.data(), prev, q2 != nullptr);
        from = prev;
    }
    const bool atPoint = from == journal.size();
    if (replayed)
        replayed = host.JournalReplay(journal.data(), journal.size(), from, journal.size(), error);
    else
        host.JournalBegin();  // stopped inside: the journal starts here (the next capture keys anyway)
    _stats.replayedBytes += journal.size() - std::min(journal.size(), from);
    if (!replayed || HostHash() != hash)
    {
        ++_stats.divergences;
        _stats.lastDivergence = !replayed ? error : "replay ended at another host state";
        _loadDivergence = _stats.lastDivergence;
    }
    else if (!atPoint)
        KeepReplayPoint(keyHash, journal.data(), journal.size(), q2 != nullptr);
    _keyState.swap(key);
    _q2Key.swap(blob.q2Key);
    _journalLast = journal.size();
    _journalPrev = prev;
    // The next capture is a rescan (the engine's after every restore): a keyframe
    _keyDue = true;
    return true;
}

TTDVdac4EspCpu::ReplayPoint* TTDVdac4EspCpu::FindReplayPoint(uint64_t keyHash, const std::vector<uint8_t>& journal)
{
    ReplayPoint* best = nullptr;
    for (ReplayPoint& p : _points)
        if (p.keyHash == keyHash && p.journal.size() <= journal.size() &&
            (!best || p.journal.size() > best->journal.size()) &&
            std::equal(p.journal.begin(), p.journal.end(), journal.begin()))
            best = &p;
    if (best)
        best->used = ++_pointClock;
    return best;
}

void TTDVdac4EspCpu::KeepReplayPoint(uint64_t keyHash, const uint8_t* journal, size_t size, bool q2)
{
    ReplayPoint* slot = nullptr;
    for (ReplayPoint& p : _points)
        if (p.keyHash == keyHash && p.journal.size() == size && std::equal(journal, journal + size, p.journal.begin()))
        {
            p.used = ++_pointClock;  // the same point again: it holds this already
            return;
        }
    _points.reserve(kReplayPoints);
    if (_points.size() < kReplayPoints)
        slot = &_points.emplace_back();
    if (!slot)
        slot = &*std::min_element(_points.begin(), _points.end(),
                                  [](const ReplayPoint& a, const ReplayPoint& b) { return a.used < b.used; });
    vdac4::EspAppHost& host = _board.Runtime().Host();
    slot->keyHash = keyHash;
    slot->journal.assign(journal, journal + size);
    host.SaveState(slot->state);
    slot->reads = host.JournalReadMap();
    vdac4::EspMemory& memory = host.Memory();
    slot->psram.assign(memory.PsramBytes().begin(), memory.PsramBytes().end());
    slot->sram.assign(memory.SramBytes().begin(), memory.SramBytes().end());
    slot->rom.assign(memory.RomBytes().begin(), memory.RomBytes().end());
    // The Quake II core the replay fed: its state and SDRAM at this point too
    slot->q2State.clear();
    slot->q2Sdram.clear();
    Vdac4BomzhCore* core = q2 ? _board.Tang().Q2Model() : nullptr;
    if (core != nullptr && core->Memory() != nullptr)
    {
        Vdac4EspStateWriter w(slot->q2State);
        core->Save(w);
        slot->q2Sdram.assign(core->Memory(), core->Memory() + Vdac4BomzhCore::kMemoryBytes);
    }
    else
    {
        slot->q2State.shrink_to_fit();
        slot->q2Sdram.shrink_to_fit();
    }
    slot->used = ++_pointClock;
}

/// endregion </Vdac4EspCpu>

/// region <Vdac4Q2>

TTDVdac4Q2::TTDVdac4Q2(Vdac4Tang& tang, const TTDVdac4EspCpu& espCpu) : _tang(tang), _espCpu(espCpu)
{
    // The model (and its SDRAM) exists from here on: the region is the same memory for every session
    if (Vdac4BomzhCore* core = _tang.Q2Model(); core && core->IsReady())
        _tracker.Bind(core->Memory(), Vdac4BomzhCore::kMemoryBytes);
}

size_t TTDVdac4Q2::TTDStateSize() const
{
    return kQ2MaxState;
}

bool TTDVdac4Q2::TTDStateWithoutRegions(std::vector<uint8_t>& state) const
{
    state.clear();
    Vdac4EspStateWriter w(state);
    _tang.SaveQ2(w, Vdac4Tang::Q2Part::Live);
    return true;
}

size_t TTDVdac4Q2::TTDStateWithoutRegionsLimit() const
{
    return kQ2MaxEngineBlob;
}

bool TTDVdac4Q2::TTDLoadStateWithoutRegions(const uint8_t* state, size_t size)
{
    // After Vdac4EspCpu: its keyframe and replay gave the core; this checkpoint's Evo side and picture now
    _loadDivergence.clear();
    Vdac4EspStateReader r(state, size);
    bool matches = true;
    if (!_tang.LoadQ2(r, Vdac4Tang::Q2Part::Live, &matches) || !r.AtEnd())
        return false;
    if (!matches)
        _loadDivergence = "the Quake II core's replay ended at another state";
    return true;
}

bool TTDVdac4Q2::TTDLoadDiverged(std::string& detail)
{
    if (_loadDivergence.empty())
        return false;
    detail = _loadDivergence;
    _loadDivergence.clear();
    return true;
}

void TTDVdac4Q2::TTDSaveStateTo(std::vector<uint8_t>& out) const
{
    BeginSized(out);
    Vdac4EspStateWriter w(out);
    w.U32(kQ2FullMagic);
    std::vector<uint8_t> blob;
    Vdac4EspStateWriter whole(blob);
    _tang.SaveQ2(whole, Vdac4Tang::Q2Part::Whole);
    w.Blob(blob);
    const uint8_t* memory = _tracker.Memory();
    w.Bool(memory != nullptr);
    if (memory)
        PutSparse(w, memory, _tracker.Bytes());
    EndSized(out);
}

void TTDVdac4Q2::TTDSaveState(uint8_t* dst) const
{
    std::vector<uint8_t> out;
    TTDSaveStateTo(out);
    std::memcpy(dst, out.data(), out.size());
}

void TTDVdac4Q2::TTDLoadState(const uint8_t* src)
{
    const uint32_t size = SizedLength(src);
    if (size < 4 || size > kQ2MaxState)
        return;
    Vdac4EspStateReader r(src + 4, size - 4);
    if (r.U32() != kQ2FullMagic)
        return;
    std::vector<uint8_t> blob;
    if (!r.Blob(blob, kQ2MaxEngineBlob))
        return;
    const bool hasMemory = r.Bool();
    uint8_t* memory = _tracker.Memory();
    if (!r.Ok() || hasMemory != (memory != nullptr))
        return;
    if (memory)
    {
        // Checked first: the SDRAM stays as it is if the state does not fit
        Vdac4EspStateReader check = r;
        if (!GetSparse(check, memory, _tracker.Bytes(), false) || !check.AtEnd())
            return;
        (void)GetSparse(r, memory, _tracker.Bytes(), true);
        // The memory moved without marks: a recording stores it whole at its next capture (LoadQ2 moves the
        // Tang's epoch, so that capture is a keyframe of the ESP)
        _tracker.MarkAll();
    }
    else if (!r.AtEnd())
        return;
    Vdac4EspStateReader whole(blob.data(), blob.size());
    (void)_tang.LoadQ2(whole, Vdac4Tang::Q2Part::Whole);
}

uint64_t TTDVdac4Q2::TTDHashState() const
{
    std::vector<uint8_t> blob;
    Vdac4EspStateWriter w(blob);
    _tang.SaveQ2(w, Vdac4Tang::Q2Part::Whole);
    return Hash64(blob.data(), blob.size());
}

TTDDeviceDescriptor TTDVdac4Q2::TTDDescribe() const
{
    TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
    // The Tang's state says whether the core runs; the ESP's keyframe and replay give the core (Vdac4EspCpu restores
    // after the Tang); this checkpoint's Evo side and picture come last
    d.restoreAfter.push_back({TTDDeviceType::Vdac4EspCpu, "vdac4espcpu"});
    return d;
}

void TTDVdac4Q2::TTDRegions(std::vector<TTDDeviceRegion>& out)
{
    if (_tracker.Memory() == nullptr)
        return;
    TTDDeviceRegion r;
    r.desc.id = TTDRegionId::Vdac4Q2Sdram;
    r.desc.name = "vdac4.q2.sdram";
    r.desc.ownerType = static_cast<uint16_t>(TTDDeviceType::Vdac4Q2);
    r.desc.memory = _tracker.Memory();
    r.desc.bytes = static_cast<uint32_t>(_tracker.Bytes());
    r.desc.pieces = _tracker.Pieces();
    r.tracker = &_tracker;
    out.push_back(r);
}

void TTDVdac4Q2::TTDBeforeCapture()
{
    // The SDRAM moves with the ESP's keyframes: offered whole at one (the engine keeps the pieces that changed),
    // never in between, where the ESP's replay computes it again
    if (_armed && _espCpu.LastCaptureKeyed())
        _tracker.MarkAll();
}

void TTDVdac4Q2::TTDBeforeRestore()
{
    // Between keyframes the SDRAM moved on unmarked, and a restore re-runs it: every piece is restored
    if (_armed)
        _tracker.MarkAll();
}

/// endregion </Vdac4Q2>

}  // namespace ttd
