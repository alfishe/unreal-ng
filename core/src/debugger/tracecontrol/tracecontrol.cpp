#include "debugger/tracecontrol/tracecontrol.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "debugger/analyzers/aylog/ayloganalyzer.h"
#include "debugger/analyzers/coverage/coverageanalyzer.h"
#include "debugger/debugmanager.h"
#include "debugger/pchistory/pchistory.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/opcode_profiler.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/calltrace.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/ports/portdiagrecorder.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

namespace tracecontrol
{
namespace
{

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::optional<std::string> TextParam(const Options& options, const char* name)
{
    const auto it = options.find(name);
    return it != options.end() ? std::optional<std::string>(it->second) : std::nullopt;
}

std::optional<bool> BoolParam(const Options& options, const char* name, std::string& error)
{
    const std::optional<std::string> text = TextParam(options, name);
    if (!text)
        return std::nullopt;
    const std::string value = Lower(*text);
    if (value == "true" || value == "on" || value == "yes" || value == "1")
        return true;
    if (value == "false" || value == "off" || value == "no" || value == "0")
        return false;
    error = std::string(name) + ": true or false";
    return std::nullopt;
}

std::optional<int64_t> IntParam(const Options& options, const char* name, std::string& error)
{
    const std::optional<std::string> text = TextParam(options, name);
    if (!text)
        return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(text->c_str(), &end, 0);
    if (text->empty() || end == nullptr || *end != '\0' || errno != 0)
    {
        error = std::string(name) + ": a whole number";
        return std::nullopt;
    }
    return value;
}

std::optional<double> NumberParam(const Options& options, const char* name, std::string& error)
{
    const std::optional<std::string> text = TextParam(options, name);
    if (!text)
        return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(text->c_str(), &end);
    if (text->empty() || end == nullptr || *end != '\0')
    {
        error = std::string(name) + ": a number";
        return std::nullopt;
    }
    return value;
}


/// One profiler or trace as the catalogue sees it (use cases D10): on / off, paused, cleared; the state it reads is
/// published (atomics and session flags), never the machine
struct Tracer
{
    const char* name;
    const char* what;
    std::function<std::string(Emulator&, const Options&, StateNode&)> start;  ///< "" or the refusal
    std::function<void(Emulator&)> stop;
    std::function<void(Emulator&)> pause;   ///< null: it cannot pause
    std::function<void(Emulator&)> resume;
    std::function<void(Emulator&)> clear;   ///< null: nothing to clear
    std::function<std::optional<ProfilerSessionState>(Emulator&)> state;
};

FeatureManager* FeatureManagerOf(Emulator& emulator)
{
    return emulator.GetFeatureManager();
}

Z80* Cpu(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pCore != nullptr ? context->pCore->GetZ80() : nullptr;
}

OpcodeProfiler* Opcodes(Emulator& emulator)
{
    Z80* z80 = Cpu(emulator);
    return z80 != nullptr ? z80->GetOpcodeProfiler() : nullptr;
}

MemoryAccessTracker* Tracker(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pMemory != nullptr ? &context->pMemory->GetAccessTracker() : nullptr;
}

AnalyzerManager* Analyzers(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetAnalyzerManager()
                                                                   : nullptr;
}

PortDiagnosticRecorder* PortTrace(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pPortDecoder != nullptr ? context->pPortDecoder->getPortTraceRecorder()
                                                                  : nullptr;
}

GeneralSoundCard* GeneralSound(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pSoundManager != nullptr ? context->pSoundManager->getGeneralSound()
                                                                   : nullptr;
}

/// The features a profiler needs, on (as the REST start routes do)
void EnableFeatures(Emulator& emulator, std::initializer_list<const char*> names)
{
    FeatureManager* features = FeatureManagerOf(emulator);
    if (features == nullptr)
        return;
    for (const char* name : names)
        features->setFeature(name, true);
    if (MemoryAccessTracker* tracker = Tracker(emulator))
        tracker->UpdateFeatureCache();
    if (Z80* z80 = Cpu(emulator))
        z80->UpdateFeatureCache();
}

ProfilerSessionState FromPortTrace(PortTraceSessionState state)
{
    return state == PortTraceSessionState::Capturing ? ProfilerSessionState::Capturing
           : state == PortTraceSessionState::Paused  ? ProfilerSessionState::Paused
                                                     : ProfilerSessionState::Stopped;
}

/// An analyzer's on / off as a session (they have no pause)
std::optional<ProfilerSessionState> AnalyzerState(Emulator& emulator, const char* id)
{
    AnalyzerManager* analyzers = Analyzers(emulator);
    if (analyzers == nullptr || !analyzers->hasAnalyzer(id))
        return std::nullopt;
    return analyzers->isActive(id) ? ProfilerSessionState::Capturing : ProfilerSessionState::Stopped;
}

const std::vector<Tracer>& Tracers()
{
    static const std::vector<Tracer> tracers = [] {
        std::vector<Tracer> t;
        t.push_back({"opcode", "the opcode profiler",
                     [](Emulator& emulator, const Options&, StateNode&) -> std::string {
                         if (Opcodes(emulator) == nullptr)
                             return "no opcode profiler";
                         EnableFeatures(emulator, {Features::kOpcodeProfiler});
                         Opcodes(emulator)->Start();
                         return {};
                     },
                     [](Emulator& emulator) { if (OpcodeProfiler* p = Opcodes(emulator)) p->Stop(); },
                     [](Emulator& emulator) { if (OpcodeProfiler* p = Opcodes(emulator)) p->Pause(); },
                     [](Emulator& emulator) { if (OpcodeProfiler* p = Opcodes(emulator)) p->Resume(); },
                     [](Emulator& emulator) { if (OpcodeProfiler* p = Opcodes(emulator)) p->Clear(); },
                     [](Emulator& emulator) -> std::optional<ProfilerSessionState> {
                         OpcodeProfiler* p = Opcodes(emulator);
                         return p != nullptr ? std::optional<ProfilerSessionState>(p->GetSessionState()) : std::nullopt;
                     }});
        t.push_back({"memory", "memory tracking (read / write / execute counters)",
                     [](Emulator& emulator, const Options&, StateNode&) -> std::string {
                         if (Tracker(emulator) == nullptr)
                             return "no memory tracker";
                         EnableFeatures(emulator, {Features::kDebugMode, Features::kMemoryTracking});
                         Tracker(emulator)->StartMemorySession();
                         return {};
                     },
                     [](Emulator& emulator) { if (MemoryAccessTracker* m = Tracker(emulator)) m->StopMemorySession(); },
                     [](Emulator& emulator) { if (MemoryAccessTracker* m = Tracker(emulator)) m->PauseMemorySession(); },
                     [](Emulator& emulator) { if (MemoryAccessTracker* m = Tracker(emulator)) m->ResumeMemorySession(); },
                     [](Emulator& emulator) { if (MemoryAccessTracker* m = Tracker(emulator)) m->ClearMemoryData(); },
                     [](Emulator& emulator) -> std::optional<ProfilerSessionState> {
                         MemoryAccessTracker* m = Tracker(emulator);
                         return m != nullptr ? std::optional<ProfilerSessionState>(m->GetMemorySessionState())
                                             : std::nullopt;
                     }});
        t.push_back({"calltrace", "the call trace",
                     [](Emulator& emulator, const Options&, StateNode&) -> std::string {
                         if (Tracker(emulator) == nullptr)
                             return "no memory tracker";
                         EnableFeatures(emulator, {Features::kDebugMode, Features::kCallTrace});
                         Tracker(emulator)->StartCalltraceSession();
                         return {};
                     },
                     [](Emulator& emulator) {
                         if (MemoryAccessTracker* m = Tracker(emulator))
                         {
                             m->StopCalltraceSession();
                             if (CallTraceBuffer* buffer = m->GetCallTraceBuffer())
                                 buffer->FlushAllHotToCold();
                         }
                     },
                     [](Emulator& emulator) { if (MemoryAccessTracker* m = Tracker(emulator)) m->PauseCalltraceSession(); },
                     [](Emulator& emulator) { if (MemoryAccessTracker* m = Tracker(emulator)) m->ResumeCalltraceSession(); },
                     [](Emulator& emulator) { if (MemoryAccessTracker* m = Tracker(emulator)) m->ClearCalltraceData(); },
                     [](Emulator& emulator) -> std::optional<ProfilerSessionState> {
                         MemoryAccessTracker* m = Tracker(emulator);
                         return m != nullptr ? std::optional<ProfilerSessionState>(m->GetCalltraceSessionState())
                                             : std::nullopt;
                     }});
        t.push_back({"coverage", "the code coverage",
                     [](Emulator& emulator, const Options& value, StateNode&) -> std::string {
                         AnalyzerManager* analyzers = Analyzers(emulator);
                         CoverageAnalyzer* coverage =
                             analyzers != nullptr ? analyzers->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
                         if (coverage == nullptr)
                             return "no coverage analyzer";
                         std::string error;
                         if (!BoolParam(value, "keep", error).value_or(false))
                             coverage->clear();
                         analyzers->activate("coverage");
                         return {};
                     },
                     [](Emulator& emulator) { if (AnalyzerManager* a = Analyzers(emulator)) a->deactivate("coverage"); },
                     nullptr, nullptr,
                     [](Emulator& emulator) {
                         if (AnalyzerManager* a = Analyzers(emulator))
                             if (CoverageAnalyzer* c = a->getAnalyzer<CoverageAnalyzer>("coverage"))
                                 c->clear();
                     },
                     [](Emulator& emulator) { return AnalyzerState(emulator, "coverage"); }});
        t.push_back({"porttrace", "the port trace",
                     [](Emulator& emulator, const Options&, StateNode&) -> std::string {
                         EnableFeatures(emulator, {Features::kPortTrace});
                         PortDiagnosticRecorder* recorder = PortTrace(emulator);
                         if (recorder == nullptr)
                             return "no port trace on this machine";
                         recorder->start();
                         return {};
                     },
                     [](Emulator& emulator) { if (PortDiagnosticRecorder* r = PortTrace(emulator)) r->stop(); },
                     [](Emulator& emulator) { if (PortDiagnosticRecorder* r = PortTrace(emulator)) r->pause(); },
                     [](Emulator& emulator) { if (PortDiagnosticRecorder* r = PortTrace(emulator)) r->resume(); },
                     [](Emulator& emulator) { if (PortDiagnosticRecorder* r = PortTrace(emulator)) r->clear(); },
                     [](Emulator& emulator) -> std::optional<ProfilerSessionState> {
                         // No recorder until the feature is first switched on (the start does it): stopped
                         EmulatorContext* context = emulator.GetContext();
                         if (context == nullptr || context->pPortDecoder == nullptr)
                             return std::nullopt;
                         PortDiagnosticRecorder* r = PortTrace(emulator);
                         return r != nullptr ? FromPortTrace(r->getSessionState()) : ProfilerSessionState::Stopped;
                     }});
        t.push_back({"gsporttrace", "the General Sound port trace",
                     [](Emulator& emulator, const Options&, StateNode&) -> std::string {
                         GeneralSoundCard* gs = GeneralSound(emulator);
                         if (gs == nullptr)
                             return "no General Sound on this machine";
                         gs->startPortTrace();
                         return {};
                     },
                     [](Emulator& emulator) { if (GeneralSoundCard* gs = GeneralSound(emulator)) gs->stopPortTrace(); },
                     [](Emulator& emulator) { if (GeneralSoundCard* gs = GeneralSound(emulator)) gs->pausePortTrace(); },
                     [](Emulator& emulator) { if (GeneralSoundCard* gs = GeneralSound(emulator)) gs->resumePortTrace(); },
                     [](Emulator& emulator) { if (GeneralSoundCard* gs = GeneralSound(emulator)) gs->clearPortTrace(); },
                     [](Emulator& emulator) -> std::optional<ProfilerSessionState> {
                         GeneralSoundCard* gs = GeneralSound(emulator);
                         if (gs == nullptr)
                             return std::nullopt;
                         return gs->isPortTraceCapturing() ? ProfilerSessionState::Capturing
                                : gs->isPortTraceArmed()   ? ProfilerSessionState::Paused
                                                           : ProfilerSessionState::Stopped;
                     }});
        t.push_back({"pchist", "the PC history",
                     [](Emulator& emulator, const Options&, StateNode&) -> std::string {
                         return PcHistory::SetArmed(&emulator, true);
                     },
                     [](Emulator& emulator) { PcHistory::SetArmed(&emulator, false); }, nullptr, nullptr, nullptr,
                     [](Emulator& emulator) -> std::optional<ProfilerSessionState> {
                         EmulatorContext* context = emulator.GetContext();
                         PcHistory* history = context != nullptr && context->pDebugManager != nullptr
                                                  ? context->pDebugManager->GetPcHistory()
                                                  : nullptr;
                         if (history == nullptr)
                             return std::nullopt;
                         return history->IsArmed() ? ProfilerSessionState::Capturing : ProfilerSessionState::Stopped;
                     }});
        t.push_back({"aylog", "the AY register log",
                     [](Emulator& emulator, const Options& value, StateNode&) -> std::string {
                         AnalyzerManager* analyzers = Analyzers(emulator);
                         AYLogAnalyzer* log = analyzers != nullptr ? analyzers->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
                         if (log == nullptr)
                             return "no AY log analyzer";
                         std::string error;
                         if (const std::optional<int64_t> capacity = IntParam(value, "capacity", error);
                             capacity && *capacity > 0)
                             log->setCapacity(static_cast<size_t>(*capacity));
                         analyzers->activate("aylog");
                         return {};
                     },
                     [](Emulator& emulator) { if (AnalyzerManager* a = Analyzers(emulator)) a->deactivate("aylog"); },
                     nullptr, nullptr,
                     [](Emulator& emulator) {
                         if (AnalyzerManager* a = Analyzers(emulator))
                             if (AYLogAnalyzer* log = a->getAnalyzer<AYLogAnalyzer>("aylog"))
                                 log->clear();
                     },
                     [](Emulator& emulator) { return AnalyzerState(emulator, "aylog"); }});
        t.push_back({"audio", "the audio capture",
                     [](Emulator& emulator, const Options& value, StateNode& body) -> std::string {
                         AnalyzerManager* analyzers = Analyzers(emulator);
                         AudioCaptureAnalyzer* capture =
                             analyzers != nullptr ? analyzers->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
                         EmulatorContext* context = emulator.GetContext();
                         if (capture == nullptr || context == nullptr)
                             return "no audio capture analyzer";
                         std::string error;
                         const double seconds = NumberParam(value, "seconds", error).value_or(1.0);
                         if (!error.empty() || seconds < 0.01 || seconds > 30.0)
                             return "seconds: 0.01 .. 30";
                         AudioSourceType source = AudioSourceType::MasterMix;
                         if (!AudioMixer::Capturable(context, TextParam(value, "source").value_or(""), source, error))
                             return error;
                         const size_t rate = context->pSoundManager != nullptr ? context->pSoundManager->getCoreRate() : 44100;
                         analyzers->activate("audiocapture");
                         capture->startCapture(static_cast<size_t>(seconds * static_cast<double>(rate)) * 2, source);
                         body["seconds"] = seconds;
                         body["source"] = AudioMixer::Key(source);
                         return {};
                     },
                     [](Emulator& emulator) {
                         if (AnalyzerManager* a = Analyzers(emulator))
                             if (AudioCaptureAnalyzer* c = a->getAnalyzer<AudioCaptureAnalyzer>("audiocapture"))
                                 c->stopCapture();
                     },
                     nullptr, nullptr,
                     [](Emulator& emulator) {
                         if (AnalyzerManager* a = Analyzers(emulator))
                             if (AudioCaptureAnalyzer* c = a->getAnalyzer<AudioCaptureAnalyzer>("audiocapture"))
                                 c->clearCapture();
                     },
                     [](Emulator& emulator) -> std::optional<ProfilerSessionState> {
                         AnalyzerManager* a = Analyzers(emulator);
                         AudioCaptureAnalyzer* c =
                             a != nullptr ? a->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
                         if (c == nullptr)
                             return std::nullopt;
                         return c->isCaptureArmed() && !c->isCaptureComplete() ? ProfilerSessionState::Capturing
                                                                                : ProfilerSessionState::Stopped;
                     }});
        // The three profilers at once (POST /profiler/start ...): on when any of them captures
        auto each = [](void (*fn)(Emulator&, const Tracer&)) {
            return [fn](Emulator& emulator) {
                for (const char* name : {"memory", "calltrace", "opcode"})
                    for (const Tracer& tracer : Tracers())
                        if (std::string(tracer.name) == name)
                            fn(emulator, tracer);
            };
        };
        t.push_back({"all", "the memory, call trace and opcode profilers together",
                     [](Emulator& emulator, const Options&, StateNode&) -> std::string {
                         EnableFeatures(emulator, {Features::kDebugMode, Features::kMemoryTracking,
                                                   Features::kCallTrace, Features::kOpcodeProfiler});
                         if (MemoryAccessTracker* m = Tracker(emulator))
                         {
                             m->StartMemorySession();
                             m->StartCalltraceSession();
                         }
                         if (OpcodeProfiler* p = Opcodes(emulator))
                             p->Start();
                         return {};
                     },
                     each([](Emulator& e, const Tracer& tracer) { tracer.stop(e); }),
                     each([](Emulator& e, const Tracer& tracer) { tracer.pause(e); }),
                     each([](Emulator& e, const Tracer& tracer) { tracer.resume(e); }),
                     each([](Emulator& e, const Tracer& tracer) { tracer.clear(e); }),
                     [](Emulator& emulator) -> std::optional<ProfilerSessionState> {
                         MemoryAccessTracker* m = Tracker(emulator);
                         OpcodeProfiler* p = Opcodes(emulator);
                         if (m == nullptr && p == nullptr)
                             return std::nullopt;
                         bool capturing = false;
                         bool paused = false;
                         for (const ProfilerSessionState state :
                              {m != nullptr ? m->GetMemorySessionState() : ProfilerSessionState::Stopped,
                               m != nullptr ? m->GetCalltraceSessionState() : ProfilerSessionState::Stopped,
                               p != nullptr ? p->GetSessionState() : ProfilerSessionState::Stopped})
                         {
                             capturing = capturing || state == ProfilerSessionState::Capturing;
                             paused = paused || state == ProfilerSessionState::Paused;
                         }
                         return capturing ? ProfilerSessionState::Capturing
                                : paused  ? ProfilerSessionState::Paused
                                          : ProfilerSessionState::Stopped;
                     }});
        return t;
    }();
    return tracers;
}

const char* kTracerNames = "opcode, memory, calltrace, coverage, porttrace, gsporttrace, pchist, aylog, audio, all";


const Tracer* Find(const std::string& name)
{
    const std::string lower = Lower(name);
    for (const Tracer& tracer : Tracers())
        if (lower == tracer.name)
            return &tracer;
    return nullptr;
}

const char* StateName(const std::optional<ProfilerSessionState>& state)
{
    return !state                                      ? "unavailable"
           : *state == ProfilerSessionState::Capturing ? "capturing"
           : *state == ProfilerSessionState::Paused    ? "paused"
                                                       : "stopped";
}

Reply Failure(const char* error, std::string message)
{
    Reply reply;
    reply.error = error;
    reply.message = std::move(message);
    return reply;
}
}  // namespace

const char* Names()
{
    return kTracerNames;
}

bool CanPause(const std::string& name)
{
    const Tracer* tracer = Find(name);
    return tracer != nullptr && static_cast<bool>(tracer->pause);
}

std::optional<ProfilerSessionState> State(Emulator& emulator, const std::string& name)
{
    const Tracer* tracer = Find(name);
    if (tracer == nullptr || emulator.GetContext() == nullptr)
        return std::nullopt;
    return tracer->state(emulator);
}

Reply Execute(Emulator& emulator, const std::string& name, const std::string& opText, const Options& options)
{
    const Tracer* tracer = Find(name);
    if (tracer == nullptr)
        return Failure("Bad Request", std::string("name: ") + kTracerNames);
    const std::string op = Lower(opText);
    const std::optional<ProfilerSessionState> state =
        emulator.GetContext() != nullptr ? tracer->state(emulator) : std::nullopt;
    if (!state)
        return Failure("Conflict", std::string(tracer->what) + " is not available on this machine");
    std::string error;
    const std::optional<bool> on = BoolParam(options, "on", error);
    if (!error.empty())
        return Failure("Bad Request", error);

    Reply reply;
    StateNode extra = StateNode::Object();
    if (op == "status")
        reply.message = std::string(tracer->what) + ": " + StateName(state);
    else if (op == "start" || op == "stop" || op == "toggle")
    {
        const bool start = op == "start" || (op == "toggle" && on.value_or(*state == ProfilerSessionState::Stopped));
        if (start)
        {
            if (std::string refused = tracer->start(emulator, options, extra); !refused.empty())
                return Failure("Conflict", std::move(refused));
        }
        else
            tracer->stop(emulator);
        reply.message = std::string(start ? "Started " : "Stopped ") + tracer->what;
    }
    else if (op == "pause" || op == "resume" || op == "pause_toggle")
    {
        if (!tracer->pause)
            return Failure("Bad Request", std::string(tracer->what) + " cannot pause: start and stop it");
        const bool pause = op == "pause" || (op == "pause_toggle" && on.value_or(*state != ProfilerSessionState::Paused));
        if (pause && *state == ProfilerSessionState::Stopped)
            return Failure("Conflict", std::string(tracer->what) + " is not running");
        pause ? tracer->pause(emulator) : tracer->resume(emulator);
        reply.message = std::string(pause ? "Paused " : "Resumed ") + tracer->what;
    }
    else if (op == "clear")
    {
        if (!tracer->clear)
            return Failure("Bad Request", std::string(tracer->what) + " has nothing to clear (it starts empty)");
        tracer->clear(emulator);
        reply.message = std::string("Cleared ") + tracer->what;
    }
    else
        return Failure("Bad Request", "op: start, stop, toggle, pause, resume, pause_toggle, clear or status");

    reply.body["name"] = tracer->name;
    reply.body["state"] = StateName(tracer->state(emulator));
    reply.body["can_pause"] = static_cast<bool>(tracer->pause);
    for (auto& [key, member] : extra.members)
        reply.body[key] = member;
    return reply;
}

}  // namespace tracecontrol
