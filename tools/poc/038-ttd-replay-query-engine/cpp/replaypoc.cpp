/// @file replaypoc.cpp
/// @brief POC 038 (tools/poc/038-ttd-replay-query-engine/README.md): window queries on a TTD recording, answered by
/// replaying the window once with observers, on the source (a-inplace, today's verbs) or on headless clones that
/// load the serialized session (b-clone: one, c-parallel: N on N threads, contiguous checkpoint stripes).
///
/// Observers on a clone: breakpoint taps to a POC sink (exec / write / read probes on a CPU address, AY port-out
/// probes) and the Z80 M1 trace hook (a PC profile). Each stripe ends with the determinism check: the clone's machine
/// after the forward run against the same clone after a seek to that frame (the recorded checkpoint).
///
///   ttd-replay-poc --workload w1 --frames 3000 --clones 1,2,4,8 --repeat 3 [--verify-every 25] [--out f.json]

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"

namespace
{
using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

size_t ResidentBytes()
{
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return info.resident_size;
    return 0;
#else
    std::ifstream statm("/proc/self/statm");
    size_t pages = 0, resident = 0;
    statm >> pages >> resident;
    return resident * 4096;
#endif
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
struct Options
{
    std::string workload = "w1";
    unsigned frames = 3000;
    std::vector<unsigned> clones{1, 2, 4, 8};
    unsigned repeat = 1;
    unsigned verifyEvery = 0;  ///< 0: check at the stripe ends only
    bool inplace = true;       ///< run the a-inplace oracle and timings
    bool liveImpact = false;
    uint64_t from = 0;         ///< the window's first frame (0: the session's first)
    bool skipBaseline = false; ///< start after the baseline checkpoint (it is taken inside a frame)
    std::string out;
    // Query targets (defaults: W1's program)
    uint16_t execPc = 0x8200;
    uint16_t writeFrom = 0x8103, writeTo = 0x8103;
    uint16_t readFrom = 0x80F0, readTo = 0x80F0;
};

std::vector<unsigned> ParseList(const std::string& s)
{
    std::vector<unsigned> v;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty())
            v.push_back(static_cast<unsigned>(std::stoul(item)));
    return v;
}

// ---------------------------------------------------------------------------
// An instance: the source, a clone or the live-impact machine
// ---------------------------------------------------------------------------
struct Instance
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelController* ttd = nullptr;

    bool Make(const std::string& id, const std::string& model, bool timeTravel = true)
    {
        emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel(id, model, LoggerLevel::LogNone);
        if (!emulator)
        {
            std::fprintf(stderr, "CreateEmulatorWithModel(%s) failed\n", model.c_str());
            return false;
        }
        context = emulator->GetContext();
        context->pFeatureManager->setFeature(Features::kDebugMode, true);
        context->pFeatureManager->setFeature(Features::kTimeTravel, timeTravel);
        // POC038_CLONE_SOUND / POC038_CLONE_HQ = 0|1: the clones' sound generation and HQ (render-mode experiments)
        for (const auto& [env, feature] : {std::pair{"POC038_CLONE_SOUND", Features::kSoundGeneration},
                                           std::pair{"POC038_CLONE_HQ", Features::kSoundHQ}})
            if (const char* v = std::getenv(env); v && id.rfind("poc038-clone", 0) == 0)
                context->pFeatureManager->setFeature(feature, std::string(v) != "0");
        context->pMemory->UpdateFeatureCache();
        context->pSoundManager->UpdateFeatureCache();
        // POC038_TURBO=0: no turbo anywhere; POC038_CLONE_TURBO=0/1 overrides it for the clones
        const char* turbo = std::getenv("POC038_TURBO");
        bool on = !turbo || std::string(turbo) != "0";
        const char* cloneTurbo = std::getenv("POC038_CLONE_TURBO");
        if (cloneTurbo && id.rfind("poc038-clone", 0) == 0)
            on = std::string(cloneTurbo) != "0";
        const bool withAudio = cloneTurbo && std::string(cloneTurbo) == "audio" && id.rfind("poc038-clone", 0) == 0;
        if (on)
            emulator->EnableTurboMode(withAudio);
        ttd = context->pTimeTravelController;
        if (timeTravel && ttd == nullptr)
            std::fprintf(stderr, "no time-travel controller on %s\n", id.c_str());
        return !timeTravel || ttd != nullptr;
    }
    ~Instance()
    {
        if (emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    }
    Z80* Cpu() const { return context->pCore->GetZ80(); }
};

// ---------------------------------------------------------------------------
// Workloads
// ---------------------------------------------------------------------------

/// W1: the program of ttdanalysisqueries_test.cpp. A new value to #8100 every pass, #8103 flips every 16 passes, every
/// 64 passes page 1 / page 3 is swapped into bank 3 and #C000 there is called through #8200
void SetUpW1(Instance& s)
{
    const std::vector<uint8_t> loop = {0xF3, 0x0E, 0x00, 0x3E, 0x11, 0x32, 0xF0, 0x80, 0x06, 0x00, 0x10, 0xFE,
                                       0x0C, 0x79, 0x32, 0x00, 0x81, 0x3E, 0x05, 0x32, 0x01, 0x81, 0x79, 0x0F,
                                       0x0F, 0x0F, 0x0F, 0xE6, 0x01, 0x32, 0x03, 0x81, 0x3A, 0x00, 0x00, 0x79,
                                       0xE6, 0x3F, 0xCC, 0x00, 0x82, 0x18, 0xDD};
    const std::vector<uint8_t> swap = {0xC5, 0x3A, 0xF0, 0x80, 0xEE, 0x02, 0x32, 0xF0, 0x80, 0x01,
                                       0xFD, 0x7F, 0xED, 0x79, 0xCD, 0x00, 0xC0, 0xC1, 0xC9};
    Memory* memory = s.context->pMemory;
    for (size_t i = 0; i < loop.size(); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), loop[i]);
    for (size_t i = 0; i < swap.size(); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8200 + i), swap[i]);
    memory->RAMPageAddress(1)[0] = 0xC9;
    memory->RAMPageAddress(3)[0] = 0xC9;
    s.Cpu()->pc = 0x8000;
    s.Cpu()->sp = 0xBF00;
}

bool SetUpWorkload(Instance& s, const std::string& workload, std::string& error)
{
    if (workload == "w1")
    {
        SetUpW1(s);
        return true;
    }
    if (workload == "w2")
    {
        // A demo with AY music and paging
        if (!s.emulator->LoadSnapshot("testdata/loaders/sna/7threality.sna"))
        {
            error = "cannot load testdata/loaders/sna/7threality.sna (run from the repository root)";
            return false;
        }
        s.emulator->RunNFrames(100, true);  // settle, as the corpus fixture does
        return true;
    }
    if (workload == "w3")
    {
        // The 48K ROM's editor at the K prompt, typed into: recorded keyboard input on the replay's path
        s.emulator->RunNFrames(150, true);
        return true;
    }
    error = "unknown workload " + workload;
    return false;
}

/// The model each workload runs on
std::string ModelOf(const std::string& workload)
{
    return workload == "w3" ? "48K" : "PENTAGON";
}

/// W3's query targets: the ROM's KEY-SCAN, LAST-K (written by the interrupt, read by the editor)
void TargetsOf(const std::string& workload, Options& o)
{
    if (workload == "w3")
    {
        o.execPc = 0x028E;
        o.writeFrom = o.writeTo = 0x5C08;
        o.readFrom = o.readTo = 0x5C08;
    }
}

/// Record @p frames on @p s; W3 types a fixed key sequence through the live input path (journaled)
void RecordFrames(Instance& s, const std::string& workload, unsigned frames)
{
    if (workload != "w3")
    {
        s.emulator->RunNFrames(frames, true);
        return;
    }
    static const uint8_t keys[] = {ZXKEY_P, ZXKEY_1, ZXKEY_SPACE, ZXKEY_A, ZXKEY_ENTER, ZXKEY_L, ZXKEY_E, ZXKEY_Q,
                                   ZXKEY_ENTER};
    unsigned k = 0;
    for (unsigned f = 0; f < frames; ++f)
    {
        if (f % 9 == 0 || f % 9 == 3)
        {
            ttd::TTDInputEvent ev;
            ev.kind = ttd::TTDInputKind::Key;
            ev.key = keys[(k / 2) % (sizeof(keys) / sizeof(keys[0]))];
            ev.pressed = f % 9 == 0;
            s.ttd->SubmitLiveInput(ev);
            ++k;
        }
        s.emulator->RunNFrames(1, true);
    }
}

// ---------------------------------------------------------------------------
// The machine's fingerprint (as TimeTravelControllerCorpus_Test compares it)
// ---------------------------------------------------------------------------
struct Fingerprint
{
    ttd::TTDCpuState cpu{};
    ttd::TTDChipsetState chipset{};
    std::unordered_map<uint8_t, std::vector<uint8_t>> devices;
    std::vector<uint64_t> ram;
};

uint64_t Fnv1a(const uint8_t* data, size_t size)
{
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i)
        h = (h ^ data[i]) * 1099511628211ull;
    return h;
}

Fingerprint Take(const Instance& s)
{
    Fingerprint f;
    const Z80* z80 = s.Cpu();
    f.cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
    f.chipset = ttd::CaptureChipsetState(s.context->emulatorState, static_cast<uint32_t>(z80->t));
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    s.ttd->GetPeripheralRegistry().CaptureAll(blobs);
    for (const auto& [id, blob] : blobs)
        f.devices[id] = ttd::TTDPeripheralRegistry::DecodeBlob(id, blob);
    const uint16_t pages = s.ttd->GetSessionInfo().modelRamPages;
    for (uint16_t page = 0; page < pages; ++page)
        if (const uint8_t* bytes = s.context->pMemory->RAMPageAddress(page))
            f.ram.push_back(Fnv1a(bytes, 0x4000));
    return f;
}

/// Empty when equal, else the first part that differs
std::string Differs(const Fingerprint& a, const Fingerprint& b)
{
    if (std::memcmp(&a.cpu, &b.cpu, sizeof(a.cpu)) != 0)
        return "cpu (pc " + std::to_string(a.cpu.pc) + " vs " + std::to_string(b.cpu.pc) + ")";
    if (std::memcmp(&a.chipset, &b.chipset, sizeof(a.chipset)) != 0)
        return "chipset (t " + std::to_string(ttd::GetChipsetCpuTInFrame(a.chipset)) + " vs " +
               std::to_string(ttd::GetChipsetCpuTInFrame(b.chipset)) + ")";
    if (a.devices.size() != b.devices.size())
        return "device set";
    for (const auto& [id, blob] : a.devices)
    {
        const auto it = b.devices.find(id);
        if (it == b.devices.end())
            return "device " + std::to_string(id) + " missing";
        if (it->second != blob)
        {
            size_t first = 0;
            while (first < blob.size() && first < it->second.size() && blob[first] == it->second[first])
                ++first;
            size_t differing = 0;
            std::string offsets;
            for (size_t i = 0; i < std::min(blob.size(), it->second.size()); ++i)
                if (blob[i] != it->second[i])
                {
                    if (++differing <= 12)
                        offsets += " " + std::to_string(i);
                }
            return "device " + std::to_string(id) + " (" + std::to_string(blob.size()) + " vs " +
                   std::to_string(it->second.size()) + " bytes, first differing byte " + std::to_string(first) + ", " +
                   std::to_string(differing) + " differ:" + offsets + ")";
        }
    }
    if (a.ram != b.ram)
    {
        for (size_t i = 0; i < std::min(a.ram.size(), b.ram.size()); ++i)
            if (a.ram[i] != b.ram[i])
                return "ram page " + std::to_string(i);
        return "ram size";
    }
    return {};
}

// ---------------------------------------------------------------------------
// Answers
// ---------------------------------------------------------------------------
struct Hit
{
    uint64_t frame = 0;
    uint32_t t = 0;    ///< TTD units in the frame
    uint16_t pc = 0;   ///< the instruction (exec: its start)
    uint16_t addr = 0; ///< address or port
    uint16_t page = ttd::kPhysPageNone;
    int value = -1;    ///< write: the new byte; port: the value
    int old = -1;      ///< write: the byte replaced
    bool operator==(const Hit& o) const
    {
        return frame == o.frame && t == o.t && pc == o.pc && addr == o.addr && page == o.page && value == o.value &&
               old == o.old;
    }
};

enum Probe : uint32_t
{
    kExec = 0,
    kWrite,
    kRead,
    kAyFFFD,
    kAyBFFD,
    kProbes
};
const char* kProbeNames[kProbes] = {"exec", "write", "read", "ay_fffd", "ay_bffd"};

struct Answers
{
    std::array<std::vector<Hit>, kProbes> hits;
    std::vector<uint64_t> profile;  ///< O5: instruction starts per PC (65,536), empty when not collected
    uint64_t instructions = 0;
    std::vector<std::string> mismatches;  ///< determinism: "frame F: what"
    uint64_t checks = 0;

    void Append(Answers&& other)
    {
        for (size_t p = 0; p < kProbes; ++p)
            hits[p].insert(hits[p].end(), other.hits[p].begin(), other.hits[p].end());
        if (!other.profile.empty())
        {
            if (profile.empty())
                profile.assign(65536, 0);
            for (size_t i = 0; i < 65536; ++i)
                profile[i] += other.profile[i];
        }
        instructions += other.instructions;
        mismatches.insert(mismatches.end(), other.mismatches.begin(), other.mismatches.end());
        checks += other.checks;
    }
};

/// O1 from the hit lists: per probe, the frames it fired in and the hits per frame
std::map<uint64_t, uint32_t> FiredFrames(const std::vector<Hit>& hits)
{
    std::map<uint64_t, uint32_t> frames;
    for (const Hit& h : hits)
        frames[h.frame]++;
    return frames;
}

// ---------------------------------------------------------------------------
// The tap sink of one clone
// ---------------------------------------------------------------------------
class Sink : public IBreakpointTapSink
{
public:
    Sink(Instance& clone, Answers& out) : _s(clone), _out(out) {}

    uint64_t frame = 0;

    void Arm(const Options& o)
    {
        BreakpointManager* bp = _s.context->pDebugManager->GetBreakpointsManager();
        _id = bp->RegisterTapSink(this);
        BreakpointManager::Batch batch(*bp);
        auto memory = [&](uint32_t cookie, uint8_t type, uint16_t from, uint16_t to) {
            auto* d = new BreakpointDescriptor();
            d->hidden = true;
            d->owner = "poc038";
            d->tapSink = _id;
            d->tapCookie = cookie;
            d->type = BRK_MEMORY;
            d->memoryType = type;
            d->z80address = from;
            if (to > from)
            {
                d->isRange = true;
                d->z80addressEnd = to;
            }
            bp->AddBreakpoint(d);
        };
        auto port = [&](uint32_t cookie, uint16_t address, uint16_t mask) {
            auto* d = new BreakpointDescriptor();
            d->hidden = true;
            d->owner = "poc038";
            d->tapSink = _id;
            d->tapCookie = cookie;
            d->type = BRK_IO;
            d->ioType = BRK_IO_OUT;
            d->z80address = address;
            d->portMask = mask;
            bp->AddBreakpoint(d);
        };
        memory(kExec, BRK_MEM_EXECUTE, o.execPc, o.execPc);
        memory(kWrite, BRK_MEM_WRITE, o.writeFrom, o.writeTo);
        memory(kRead, BRK_MEM_READ, o.readFrom, o.readTo);
        port(kAyFFFD, 0xFFFD, 0xC002);
        port(kAyBFFD, 0xBFFD, 0xC002);
        _writeFrom = o.writeFrom;
        _writeTo = o.writeTo;
    }

    void Disarm()
    {
        BreakpointManager* bp = _s.context->pDebugManager->GetBreakpointsManager();
        if (_id != BRK_NO_TAP)
            bp->UnregisterTapSink(_id);
        _id = BRK_NO_TAP;
    }

    /// The bytes of the write range on every RAM page as they stand now: the old value of the next write to each
    void SnapWriteRange()
    {
        _before.clear();
        // The session's RAM pages (a 48K maps #4000 to page 5: the configured RAM size counts only three pages)
        const uint16_t pages = _s.ttd->GetSessionInfo().modelRamPages;
        for (uint16_t page = 0; page < pages; ++page)
            if (const uint8_t* bytes = _s.context->pMemory->RAMPageAddress(page))
                for (uint32_t a = _writeFrom; a <= _writeTo; ++a)
                    _before[(static_cast<uint32_t>(page) << 14) | (a & 0x3FFF)] = bytes[a & 0x3FFF];
    }

    void OnTap(const BreakpointTapHit& hit) override
    {
        if (hit.cookie >= kProbes)
            return;
        const Z80* z80 = _s.Cpu();
        Hit h;
        h.frame = frame;
        h.t = _s.context->emulatorState.TtdTInFrame(static_cast<uint32_t>(z80->t));
        h.addr = hit.address;
        h.pc = hit.cookie == kExec ? hit.address : z80->m1_pc;
        if (hit.cookie == kAyFFFD || hit.cookie == kAyBFFD)
            h.value = hit.value;
        else
        {
            const MemoryPageDescriptor d = _s.context->pMemory->MapZ80AddressToPhysicalPage(hit.address);
            h.page = d.mode == BANK_RAM ? d.page : ttd::kPhysPageNone;
            if (hit.cookie == kWrite)
            {
                h.value = _s.context->pMemory->DirectReadFromZ80Memory(hit.address);
                if (h.page != ttd::kPhysPageNone)
                {
                    uint8_t& before = _before[(static_cast<uint32_t>(h.page) << 14) | (hit.address & 0x3FFF)];
                    h.old = before;
                    before = static_cast<uint8_t>(h.value);
                }
            }
        }
        _out.hits[hit.cookie].push_back(h);
    }

private:
    Instance& _s;
    Answers& _out;
    uint8_t _id = BRK_NO_TAP;
    uint16_t _writeFrom = 0, _writeTo = 0;
    std::unordered_map<uint32_t, uint8_t> _before;
};

enum class Observers
{
    None,
    Taps,
    M1
};
const char* ObserversName(Observers o)
{
    return o == Observers::None ? "O0-none" : o == Observers::Taps ? "O1-O4-taps" : "O5-m1-profile";
}

/// Replay frames [from, to) on @p clone with @p observers. Every frame is run from where the previous one left the
/// CPU to the frame end (as the controller's replay does: RunNFrames would run a whole frame from the overshoot)
void ReplayStripe(Instance& clone, uint64_t from, uint64_t to, Observers observers, const Options& o, Answers& out)
{
    Sink sink(clone, out);
    clone.ttd->SeekTo({from, 0});
    if (observers == Observers::Taps)
    {
        sink.Arm(o);
        sink.SnapWriteRange();
    }
    Z80* z80 = clone.Cpu();
    if (observers == Observers::M1)
    {
        out.profile.assign(65536, 0);
        uint64_t* profile = out.profile.data();
        uint64_t* count = &out.instructions;
        z80->m1TraceHook = [profile, count](uint16_t pc) {
            profile[pc]++;
            (*count)++;
        };
    }
    const bool skipBreakpoints = observers != Observers::Taps;
    for (uint64_t f = from; f < to; ++f)
    {
        sink.frame = f;
        const uint32_t frameT = clone.context->GetFrameTStates();
        const uint32_t startT = static_cast<uint32_t>(z80->t);
        if (startT < frameT)
            clone.emulator->RunTStates(frameT - startT, skipBreakpoints);
        const bool last = f + 1 == to;
        if (last || (o.verifyEvery != 0 && (f + 1 - from) % o.verifyEvery == 0))
        {
            // The determinism check: the forward run against the recorded checkpoint of frame f + 1. A seek runs
            // the machine too (the M1 hook would count it), so the hook is off across it
            const Fingerprint run = Take(clone);
            auto hook = std::move(z80->m1TraceHook);
            z80->m1TraceHook = nullptr;
            clone.ttd->SeekTo({f + 1, 0});
            if (!last)
                z80->m1TraceHook = std::move(hook);
            const Fingerprint recorded = Take(clone);
            out.checks++;
            const std::string what = Differs(run, recorded);
            if (!what.empty())
                out.mismatches.push_back("frame " + std::to_string(f + 1) + ": " + what);
            if (observers == Observers::Taps && !last)
                sink.SnapWriteRange();
        }
    }
    if (observers == Observers::M1)
        z80->m1TraceHook = nullptr;
    sink.Disarm();
}

// ---------------------------------------------------------------------------
// a-inplace: today's verbs on the source, paged with their cursors
// ---------------------------------------------------------------------------
std::vector<Hit> InplaceExecHits(Instance& s, uint16_t pc, uint64_t fromFrame)
{
    std::vector<Hit> all;
    ttd::TTDReverseContinueQuery q;
    q.pcs = {pc};
    q.all = true;
    q.limit = ttd::TimeTravelController::kReverseHitLimitMax;
    q.fromFrame = fromFrame;
    q.beforeGlobalT = s.ttd->GlobalT(s.ttd->SessionEndPosition());
    while (true)
    {
        const auto r = s.ttd->ReverseContinue(q);
        for (const auto& h : r.hits)
        {
            Hit x;
            x.frame = h.time.frame;
            x.t = h.time.tInFrame;
            x.pc = h.pc;
            x.addr = h.pc;
            x.page = h.physPage;
            all.push_back(x);
        }
        if (!r.truncated || r.hits.empty())
            break;
        q.beforeGlobalT = r.hits.back().globalT - 1;
    }
    std::reverse(all.begin(), all.end());
    return all;
}

std::vector<Hit> InplaceWrites(Instance& s, uint16_t from, uint16_t to, uint64_t fromFrame, bool changesOnly = false)
{
    std::vector<Hit> all;
    ttd::TTDSearchQuery q;
    q.addrFrom = from;
    q.addrTo = to;
    q.access = ttd::TTDAccessType::Write;
    q.beforeGlobalT = s.ttd->GlobalT(s.ttd->SessionEndPosition());
    ttd::TTDFindOptions options;
    options.all = true;
    options.limit = ttd::TimeTravelController::kReverseHitLimitMax;
    options.fromFrame = fromFrame;
    options.changesOnly = changesOnly;
    while (true)
    {
        const auto r = s.ttd->FindAccesses(q, options);
        for (const auto& f : r.hits)
        {
            Hit x;
            x.frame = f.access.time.frame;
            x.t = f.access.time.tInFrame;
            x.pc = f.access.pc;
            x.addr = f.access.addr;
            x.page = f.access.physPage;
            x.value = f.access.value;
            x.old = changesOnly && f.hasOldValue ? f.oldValue : -1;
            all.push_back(x);
        }
        if (!r.truncated || r.hits.empty())
            break;
        q.beforeGlobalT = r.hits.back().globalT - 1;
    }
    std::reverse(all.begin(), all.end());
    return all;
}

/// The frame cache over the window: today's per-instruction trace, frame by frame on the source
uint64_t InplaceTrace(Instance& s, uint64_t from, uint64_t to, std::vector<uint64_t>& profile)
{
    profile.assign(65536, 0);
    uint64_t instructions = 0;
    for (uint64_t f = from; f < to; ++f)
        if (const ttd::TTDFrameCache* cache = s.ttd->GetFrameCache(f))
            for (const auto& e : cache->entries)
            {
                profile[e.pc]++;
                instructions++;
            }
    return instructions;
}

// ---------------------------------------------------------------------------
// Comparisons
// ---------------------------------------------------------------------------

/// How two hit lists differ, on the fields both sides have; empty when equal
std::string CompareHits(const std::vector<Hit>& a, const std::vector<Hit>& b, bool time, bool old, bool value)
{
    if (a.size() != b.size())
    {
        auto span = [](const std::vector<Hit>& v) {
            return v.empty() ? std::string("-")
                             : std::to_string(v.front().frame) + ":" + std::to_string(v.front().t) + ".." +
                                   std::to_string(v.back().frame) + ":" + std::to_string(v.back().t);
        };
        return "count " + std::to_string(a.size()) + " vs " + std::to_string(b.size()) + " (" + span(a) + " vs " +
               span(b) + ")";
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        const Hit& x = a[i];
        const Hit& y = b[i];
        const bool same = x.frame == y.frame && (!time || x.t == y.t) && x.pc == y.pc && x.addr == y.addr &&
                          x.page == y.page && (!value || x.value == y.value) && (!old || x.old == y.old);
        if (!same)
        {
            char line[256];
            std::snprintf(line, sizeof(line), "hit %zu: frame %llu t %u pc %04X page %u value %d old %d vs frame %llu t %u "
                                              "pc %04X page %u value %d old %d",
                          i, static_cast<unsigned long long>(x.frame), x.t, x.pc, x.page, x.value, x.old,
                          static_cast<unsigned long long>(y.frame), y.t, y.pc, y.page, y.value, y.old);
            return line;
        }
    }
    return {};
}

/// The largest t difference between two lists that agree on everything else
uint32_t MaxTimeSkew(const std::vector<Hit>& a, const std::vector<Hit>& b)
{
    uint32_t skew = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        skew = std::max(skew, a[i].t > b[i].t ? a[i].t - b[i].t : b[i].t - a[i].t);
    return skew;
}


/// The PCs whose counts differ most, for the report
std::string ProfileDiff(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b)
{
    if (a.size() != b.size())
        return "sizes " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
    std::vector<std::pair<int64_t, uint32_t>> d;
    int64_t total = 0;
    for (uint32_t pc = 0; pc < a.size(); ++pc)
        if (a[pc] != b[pc])
        {
            const int64_t delta = static_cast<int64_t>(a[pc]) - static_cast<int64_t>(b[pc]);
            d.push_back({delta, pc});
            total += delta;
        }
    std::sort(d.begin(), d.end(), [](auto& x, auto& y) { return std::llabs(x.first) > std::llabs(y.first); });
    std::string out = std::to_string(d.size()) + " PCs, total " + std::to_string(total) + ":";
    for (size_t i = 0; i < std::min<size_t>(d.size(), 6); ++i)
    {
        char item[48];
        std::snprintf(item, sizeof(item), " %04X%+lld", d[i].second, static_cast<long long>(d[i].first));
        out += item;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The live machine's frame time while the clones run (G4)
// ---------------------------------------------------------------------------
double LiveFrameMs(unsigned frames)
{
    Instance live;
    if (!live.Make("poc038-live", "PENTAGON", false))
        return -1;
    SetUpW1(live);
    live.emulator->RunNFrames(50, true);
    const auto start = Clock::now();
    live.emulator->RunNFrames(frames, true);
    return MsSince(start) / frames;
}

struct Json
{
    std::ostringstream s;
    bool first = true;
    void Field(const std::string& k, const std::string& v)
    {
        s << (first ? "" : ",\n") << "  \"" << k << "\": " << v;
        first = false;
    }
    void Num(const std::string& k, double v) { Field(k, std::to_string(v)); }
    void Str(const std::string& k, const std::string& v) { Field(k, "\"" + v + "\""); }
};

}  // namespace

int main(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--workload") o.workload = next();
        else if (a == "--frames") o.frames = static_cast<unsigned>(std::stoul(next()));
        else if (a == "--clones") o.clones = ParseList(next());
        else if (a == "--repeat") o.repeat = static_cast<unsigned>(std::stoul(next()));
        else if (a == "--verify-every") o.verifyEvery = static_cast<unsigned>(std::stoul(next()));
        else if (a == "--no-inplace") o.inplace = false;
        else if (a == "--from") o.from = std::stoull(next());
        else if (a == "--skip-baseline") o.skipBaseline = true;
        else if (a == "--live-impact") o.liveImpact = true;
        else if (a == "--out") o.out = next();
        else if (a == "--exec-pc") o.execPc = static_cast<uint16_t>(std::stoul(next(), nullptr, 0));
        else if (a == "--write") o.writeFrom = o.writeTo = static_cast<uint16_t>(std::stoul(next(), nullptr, 0));
        else if (a == "--read") o.readFrom = o.readTo = static_cast<uint16_t>(std::stoul(next(), nullptr, 0));
        else
        {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }

    Json json;
    json.Str("workload", o.workload);
    json.Num("frames", o.frames);

    // ---- Record the source ----
    Instance source;
    TargetsOf(o.workload, o);
    if (!source.Make("poc038-source", ModelOf(o.workload)))
    {
        std::fprintf(stderr, "cannot create the source\n");
        return 1;
    }
    std::string error;
    if (!SetUpWorkload(source, o.workload, error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    auto start = Clock::now();
    source.ttd->StartRecording();
    RecordFrames(source, o.workload, o.frames);
    source.ttd->StopRecording();
    const double recordMs = MsSince(start);
    source.ttd->SeekTo(source.ttd->SessionEndPosition());
    const uint64_t firstFrame =
        std::max<uint64_t>(source.ttd->GetCheckpoint(0)->time.frame + (o.skipBaseline ? 1 : 0), o.from);
    const uint64_t lastFrame = source.ttd->GetCheckpoint(source.ttd->GetCheckpointCount() - 1)->time.frame;
    std::printf("recorded %u frames in %.0f ms: checkpoints %zu, frames %llu..%llu\n", o.frames, recordMs,
                source.ttd->GetCheckpointCount(), static_cast<unsigned long long>(firstFrame),
                static_cast<unsigned long long>(lastFrame));
    json.Num("record_ms", recordMs);
    json.Num("checkpoints", static_cast<double>(source.ttd->GetCheckpointCount()));

    // ---- Serialize ----
    start = Clock::now();
    std::ostringstream serialized;
    if (!source.ttd->SerializeSession(serialized, error))
    {
        std::fprintf(stderr, "serialize: %s\n", error.c_str());
        return 1;
    }
    const std::string session = serialized.str();
    const double serializeMs = MsSince(start);
    std::printf("serialized %zu bytes in %.1f ms\n", session.size(), serializeMs);
    json.Num("session_bytes", static_cast<double>(session.size()));
    json.Num("serialize_ms", serializeMs);

    // ---- a-inplace: today's verbs (the oracle for exec and writes) and the frame-cache trace ----
    std::vector<Hit> oracleExec, oracleWrites, oracleChanges;
    std::vector<uint64_t> oracleProfile;
    if (o.inplace)
    {
        start = Clock::now();
        oracleExec = InplaceExecHits(source, o.execPc, firstFrame);
        const double execMs = MsSince(start);
        start = Clock::now();
        oracleWrites = InplaceWrites(source, o.writeFrom, o.writeTo, firstFrame);
        const double writesMs = MsSince(start);
        start = Clock::now();
        oracleChanges = InplaceWrites(source, o.writeFrom, o.writeTo, firstFrame, true);
        const double changesMs = MsSince(start);
        json.Num("inplace_changes_ms", changesMs);
        json.Num("inplace_changes_hits", static_cast<double>(oracleChanges.size()));
        std::printf("a-inplace: changes %zu hits %.0f ms\n", oracleChanges.size(), changesMs);
        start = Clock::now();
        // The clones replay [firstFrame, lastFrame): the verbs also see the part of the last frame after its checkpoint
        for (std::vector<Hit>* v : {&oracleExec, &oracleWrites, &oracleChanges})
            v->erase(std::remove_if(v->begin(), v->end(), [&](const Hit& h) { return h.frame >= lastFrame; }), v->end());
        const uint64_t instructions = InplaceTrace(source, firstFrame, lastFrame, oracleProfile);
        const double traceMs = MsSince(start);
        std::printf("a-inplace: exec %zu hits %.0f ms | writes %zu hits %.0f ms | trace %llu instructions %.0f ms "
                    "(%.3f ms/frame)\n",
                    oracleExec.size(), execMs, oracleWrites.size(), writesMs,
                    static_cast<unsigned long long>(instructions), traceMs, traceMs / (lastFrame - firstFrame));
        json.Num("inplace_exec_ms", execMs);
        json.Num("inplace_exec_hits", static_cast<double>(oracleExec.size()));
        json.Num("inplace_writes_ms", writesMs);
        json.Num("inplace_writes_hits", static_cast<double>(oracleWrites.size()));
        json.Num("inplace_trace_ms", traceMs);
        json.Num("inplace_trace_instructions", static_cast<double>(instructions));
    }

    // ---- b-clone / c-parallel ----
    const double liveAlone = o.liveImpact ? LiveFrameMs(500) : 0;
    if (o.liveImpact)
    {
        std::printf("live machine alone: %.3f ms/frame\n", liveAlone);
        json.Num("live_alone_ms_per_frame", liveAlone);
    }

    std::map<Observers, Answers> reference;  // the first run of each observer set (N = the first clone count)
    int failures = 0;
    for (unsigned n : o.clones)
    {
        const size_t rssBefore = ResidentBytes();
        start = Clock::now();
        std::vector<std::unique_ptr<Instance>> clones;
        for (unsigned c = 0; c < n; ++c)
        {
            auto clone = std::make_unique<Instance>();
            if (!clone->Make("poc038-clone-" + std::to_string(c), ModelOf(o.workload)))
            {
                std::fprintf(stderr, "cannot create clone %u\n", c);
                return 1;
            }
            clones.push_back(std::move(clone));
        }
        const double createMs = MsSince(start);
        start = Clock::now();
        {
            std::vector<std::thread> loaders;
            std::vector<std::string> errors(n);
            for (unsigned c = 0; c < n; ++c)
                loaders.emplace_back([&, c] {
                    std::istringstream in(session);
                    if (!clones[c]->ttd->DeserializeSession(in, errors[c]))
                        errors[c] = errors[c].empty() ? "failed" : errors[c];
                    else
                        errors[c].clear();
                });
            for (auto& t : loaders)
                t.join();
            for (unsigned c = 0; c < n; ++c)
                if (!errors[c].empty())
                {
                    std::fprintf(stderr, "clone %u load: %s\n", c, errors[c].c_str());
                    return 1;
                }
        }
        const double loadMs = MsSince(start);
        const size_t rssAfter = ResidentBytes();
        std::printf("N=%u: create %.0f ms, load %.0f ms, +%.1f MB resident (%.1f MB per clone)\n", n, createMs, loadMs,
                    (rssAfter - rssBefore) / 1048576.0, (rssAfter - rssBefore) / 1048576.0 / n);
        const std::string key = "n" + std::to_string(n) + "_";
        json.Num(key + "create_ms", createMs);
        json.Num(key + "load_ms", loadMs);
        json.Num(key + "rss_mb_per_clone", (rssAfter - rssBefore) / 1048576.0 / n);

        // Contiguous stripes of frames, one per clone
        const uint64_t total = lastFrame - firstFrame;
        for (Observers observers : {Observers::None, Observers::Taps, Observers::M1})
        {
            std::vector<double> times;
            Answers merged;
            double liveDuring = 0;
            for (unsigned r = 0; r < o.repeat; ++r)
            {
                std::vector<Answers> parts(n);
                std::atomic<bool> running{true};
                std::thread liveThread;
                if (o.liveImpact && r == 0)
                    liveThread = std::thread([&] { liveDuring = LiveFrameMs(500); });
                start = Clock::now();
                std::vector<std::thread> workers;
                for (unsigned c = 0; c < n; ++c)
                {
                    const uint64_t from = firstFrame + total * c / n;
                    const uint64_t to = firstFrame + total * (c + 1) / n;
                    workers.emplace_back([&, c, from, to] { ReplayStripe(*clones[c], from, to, observers, o, parts[c]); });
                }
                for (auto& t : workers)
                    t.join();
                times.push_back(MsSince(start));
                running = false;
                if (liveThread.joinable())
                    liveThread.join();
                if (r == 0)
                    for (auto& p : parts)
                        merged.Append(std::move(p));
            }
            std::sort(times.begin(), times.end());
            const double ms = times[times.size() / 2];
            std::printf("  %-14s %8.0f ms  %7.0f frames/s  checks %llu mismatches %zu", ObserversName(observers), ms,
                        total / (ms / 1000.0), static_cast<unsigned long long>(merged.checks),
                        merged.mismatches.size());
            if (observers == Observers::Taps)
                std::printf("  hits exec %zu write %zu read %zu ay %zu+%zu", merged.hits[kExec].size(),
                            merged.hits[kWrite].size(), merged.hits[kRead].size(), merged.hits[kAyFFFD].size(),
                            merged.hits[kAyBFFD].size());
            if (observers == Observers::M1)
                std::printf("  instructions %llu", static_cast<unsigned long long>(merged.instructions));
            if (o.liveImpact)
                std::printf("  live %.3f ms/frame", liveDuring);
            std::printf("\n");
            for (size_t i = 0; i < std::min<size_t>(merged.mismatches.size(), 5); ++i)
                std::printf("    MISMATCH %s\n", merged.mismatches[i].c_str());
            if (!merged.mismatches.empty())
                failures++;
            const std::string k = key + ObserversName(observers);
            json.Num(k + "_ms", ms);
            json.Num(k + "_mismatches", static_cast<double>(merged.mismatches.size()));
            if (o.liveImpact)
                json.Num(k + "_live_ms_per_frame", liveDuring);

            // Answers: the same for every N, and the oracle's where the verbs give one
            auto ref = reference.find(observers);
            if (ref == reference.end())
                reference.emplace(observers, merged);
            else
            {
                for (size_t p = 0; p < kProbes; ++p)
                {
                    const std::string d = CompareHits(merged.hits[p], ref->second.hits[p], true, true, true);
                    if (!d.empty())
                    {
                        std::printf("    DIFFERS from N=%u: %s %s\n", o.clones.front(), kProbeNames[p], d.c_str());
                        failures++;
                    }
                }
                if (merged.profile != ref->second.profile)
                {
                    std::printf("    DIFFERS from N=%u: profile %s\n", o.clones.front(),
                                ProfileDiff(merged.profile, ref->second.profile).c_str());
                    failures++;
                }
            }
            if (o.inplace && n == o.clones.front())
            {
                if (observers == Observers::Taps)
                {
                    // Exec: the reverse hits carry the M1 time, the tap the instruction start: both exact
                    std::string d = CompareHits(merged.hits[kExec], oracleExec, true, false, false);
                    std::printf("    oracle exec (reverse-continue all): %s\n", d.empty() ? "equal" : d.c_str());
                    if (!d.empty())
                        failures++;
                    // Writes: frame, PC, address, page and value; the time is reported as the skew
                    d = CompareHits(merged.hits[kWrite], oracleWrites, false, false, true);
                    std::printf("    oracle writes (find all): %s, max t skew %u\n", d.empty() ? "equal" : d.c_str(),
                                MaxTimeSkew(merged.hits[kWrite], oracleWrites));
                    if (!d.empty())
                        failures++;
                    // Changes: the clone's writes that changed the byte, old value included
                    std::vector<Hit> changes;
                    for (const Hit& h : merged.hits[kWrite])
                        if (h.old != h.value)
                            changes.push_back(h);
                    d = CompareHits(changes, oracleChanges, false, true, true);
                    std::printf("    oracle changes with old values (find changes_only): %s\n",
                                d.empty() ? "equal" : d.c_str());
                    if (!d.empty())
                        failures++;
                    // O1: the frames the exec probe fired in
                    std::printf("    probe exec fired in %zu frames, write in %zu, read in %zu, AY in %zu\n",
                                FiredFrames(merged.hits[kExec]).size(), FiredFrames(merged.hits[kWrite]).size(),
                                FiredFrames(merged.hits[kRead]).size(), FiredFrames(merged.hits[kAyFFFD]).size());
                }
                if (observers == Observers::M1)
                {
                    const bool same = merged.profile == oracleProfile;
                    std::printf("    oracle profile (frame cache): %s\n",
                                same ? "equal" : ("DIFFERS " + ProfileDiff(merged.profile, oracleProfile)).c_str());
                    if (!same)
                        failures++;
                }
            }
        }
    }

    json.Num("failures", failures);
    if (!o.out.empty())
    {
        std::ofstream f(o.out);
        f << "{\n" << json.s.str() << "\n}\n";
    }
    std::printf("%s\n", failures == 0 ? "ALL EXACT" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
