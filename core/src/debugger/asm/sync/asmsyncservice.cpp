#include "asmsyncservice.h"

#include <chrono>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/asm/sync/syncshared.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/labels/symbolfiles.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "unrealasm/sync/session.h"

namespace
{
using namespace unrealasm;

uint64_t NowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

const char* EventName(sync::TickEvent event)
{
    switch (event)
    {
        case sync::TickEvent::Found: return "found";
        case sync::TickEvent::Changed: return "changed";
        case sync::TickEvent::Lost: return "lost";
        case sync::TickEvent::Ambiguous: return "ambiguous";
        case sync::TickEvent::Unreadable: return "unreadable";
        case sync::TickEvent::None: break;
    }
    return "";
}

std::string EmulatorId(EmulatorContext* context)
{
    return context && context->pEmulator ? context->pEmulator->GetUUID().toString() : std::string();
}

void Post(EmulatorContext* context, const std::string& event, const std::string& assembler, uint64_t generation = 0, bool complete = false,
          size_t labels = 0, size_t hints = 0)
{
    auto* payload = new AsmSyncPayload(EmulatorId(context));
    payload->event = event;
    payload->assembler = assembler;
    payload->generation = generation;
    payload->complete = complete;
    payload->labels = static_cast<uint32_t>(labels);
    payload->hints = static_cast<uint32_t>(hints);
    MessageCenter::DefaultMessageCenter().Post(NC_ASM_SYNC, payload, true);
}
}  // namespace

AsmSyncService::AsmSyncService(EmulatorContext* context) : _context(context) {}

AsmSyncService::~AsmSyncService()
{
    Stop();
}

void AsmSyncService::Start(const WatchOptions& options)
{
    Stop();
    std::lock_guard<std::mutex> lock(_mutex);
    _options = options;
    _stop = false;
    _watching = true;
    _state = "searching";
    _assembler.clear();
    _ticks = 0;
    _builds = 0;
    _error.clear();
    _thread = std::thread([this, options]() { Run(options); });
}

void AsmSyncService::Stop()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_thread.joinable())
            return;
        _stop = true;
    }
    _wake.notify_all();
    _thread.join();
    std::lock_guard<std::mutex> lock(_mutex);
    _watching = false;
    _state = "off";
}

bool AsmSyncService::Watching() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _watching;
}

StateNode AsmSyncService::StatusValue() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    StateNode body = StateNode::Object();
    body["watching"] = _watching;
    body["state"] = _state;
    body["assembler"] = _assembler;
    StateNode options = StateNode::Object();
    options["assembler"] = _options.assembler;
    options["interval"] = static_cast<unsigned>(_options.intervalMs);
    options["quiet"] = static_cast<unsigned>(_options.quietMs);
    options["output"] = _options.output;
    options["as"] = _options.as;
    options["to"] = _options.to;
    body["options"] = std::move(options);
    body["ticks"] = static_cast<uint64_t>(_ticks);
    body["builds"] = static_cast<uint64_t>(_builds);
    body["text"] = _text;
    body["error"] = _error;
    return body;
}

StateNode AsmSyncService::HintsValue() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _hints;
}

std::string AsmSyncService::LastText(uint64_t& generation) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    generation = _lastTextGeneration;
    return _lastText;
}

void AsmSyncService::Run(WatchOptions options)
{
    sync::SessionOptions sessionOptions;
    sessionOptions.assembler = options.assembler;
    sessionOptions.quietMs = options.quietMs;
    sync::SyncSession session(sessionOptions);
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_stop)
                return;
        }
        // One look: the pages the CPU sees and the text's page once it is known, every page while searching
        asmsync::MachineCopy machine;
        std::string error;
        const std::vector<int> pages = session.TextPages();
        const bool copied = asmsync::CopyMachine(_context, machine, error, session.NeedsAllPages() ? nullptr : &pages);
        if (copied)
        {
            const sync::TickResult tick = session.Tick(machine.view, NowMs());
            const std::string assembler = session.Descriptor() ? session.Descriptor()->id : std::string();
            {
                std::lock_guard<std::mutex> lock(_mutex);
                ++_ticks;
                _assembler = assembler;
                _state = !session.Descriptor() ? (tick.event == sync::TickEvent::Ambiguous ? "ambiguous" : "searching")
                         : tick.event == sync::TickEvent::Unreadable ? "unreadable"
                         : session.Pending()                          ? "changed"
                                                                      : "watching";
                if (session.Descriptor() && session.Last().ok)
                {
                    StateNode text = StateNode::Object();
                    asmsync::Describe(*session.Descriptor(), session.Last(), text);
                    _text = std::move(text);
                }
                _error.clear();
            }
            if (tick.event != sync::TickEvent::None && tick.event != sync::TickEvent::Unreadable)
                Post(_context, EventName(tick.event), assembler);

            if (std::optional<sync::BuildInput> input = session.TakeBuild(NowMs()))
            {
                sync::BuildResult built = sync::SyncSession::Build(*input);
                const std::string setId = "live:sync:" + input->descriptor->id;
                size_t published = 0;
                if (built.decoded && _context && _context->pDebugManager)
                {
                    // The last good labels stay when the text does not decode (a half-typed token)
                    LabelManager* labels = _context->pDebugManager->GetLabelManager();
                    labels->DropSymbolSet(setId);
                    unrealasm::symbols::Origin origin;
                    origin.kind = "live";
                    origin.where = "asm-sync:" + input->descriptor->id;
                    published = built.labels.symbols.size();
                    labels->ImportRecords(std::move(built.labels.symbols), SymbolImportRequest{}, setId,
                                          input->descriptor->title + " in RAM" + (input->name.empty() ? "" : " (" + input->name + ")"), origin);
                    labels->SetSymbolSetPriority(setId, kLabelPriority);
                }
                std::string outputError;
                if (!options.output.empty() && built.decoded)
                {
                    const AsmReply written =
                        asmsync::Render(_context, *input->descriptor, input->file, input->name, options.as, options.to, "", options.output);
                    if (!written.Ok())
                        outputError = written.message;
                }
                size_t warnings = 0;
                for (const unrealasm::Diagnostic& d : built.hints)
                    warnings += d.severity >= unrealasm::Severity::Warning ? 1 : 0;
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    ++_builds;
                    StateNode hints = StateNode::Object();
                    hints["generation"] = static_cast<uint64_t>(built.generation);
                    hints["assembler"] = input->descriptor->id;
                    hints["decoded"] = built.decoded;
                    hints["complete"] = built.complete;
                    hints["lines"] = static_cast<uint64_t>(built.document.lines.size());
                    hints["labels"] = static_cast<uint64_t>(published);
                    hints["set"] = built.decoded ? setId : std::string();
                    hints["hints"] = asmsync::DiagnosticsValue(built.hints);
                    hints["output"] = options.output;
                    hints["output_error"] = outputError;
                    _hints = std::move(hints);
                    _error = outputError;
                    if (built.decoded)
                    {
                        _lastText = built.document.Text();
                        _lastTextGeneration = built.generation;
                    }
                }
                Post(_context, "built", input->descriptor->id, built.generation, built.complete, published, warnings);
            }
        }
        else
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _error = error;
        }
        std::unique_lock<std::mutex> lock(_mutex);
        _wake.wait_for(lock, std::chrono::milliseconds(options.intervalMs), [this]() { return _stop; });
    }
}
