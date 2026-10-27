#include "asmsyncservice.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/asm/diskfiles.h"
#include "debugger/asm/sync/synccontrol.h"
#include "debugger/asm/sync/syncshared.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/labels/symbolfiles.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "unrealasm/dialect.h"
#include "unrealasm/symbols/fromsource.h"
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
/// The files of a host project as the push watch sees them: the main source and every path its INCLUDEs lead to
/// (HostProject; a missing one too), each with its size and time. `files` counts them
std::string ProjectSignature(const std::string& path, const std::string& from, size_t& files)
{
    namespace fs = std::filesystem;
    const fs::path main(std::u8string(path.begin(), path.end()));
    std::string signature;
    files = 0;
    const auto read = [&](const fs::path& file) {
        std::error_code sizeFailed, timeFailed;
        const uintmax_t size = fs::file_size(file, sizeFailed);
        const fs::file_time_type time = fs::last_write_time(file, timeFailed);
        std::ifstream in(file, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // The content's hash as well: a time can be whole seconds (libstdc++ on Windows, FAT), and a save
        // of the same size within that second would leave size and time as they were
        uint64_t hash = 14695981039346656037ull;  // FNV-1a
        for (const uint8_t b : bytes)
            hash = (hash ^ b) * 1099511628211ull;
        const std::u8string name = file.u8string();
        signature.append(name.begin(), name.end());
        signature += sizeFailed || timeFailed ? std::string("|missing\n")
                                              : "|" + std::to_string(size) + "|" + std::to_string(static_cast<long long>(time.time_since_epoch().count())) +
                                                    "|" + std::to_string(hash) + "\n";
        ++files;
        return bytes;
    };
    const std::vector<uint8_t> bytes = read(main);
    SourceDocument document = SourceDocument::FromText(std::string(bytes.begin(), bytes.end()), from);
    for (SourceLine& line : document.lines)
        if (!line.text.empty() && line.text.back() == '\r')
            line.text.pop_back();
    Diagnostics ignored;
    HostProject(document, [&](const std::string& name) { return read(main.parent_path() / fs::path(std::u8string(name.begin(), name.end()))); },
                ignored);
    return signature;
}
}  // namespace

AsmSyncService::AsmSyncService(EmulatorContext* context) : _context(context) {}

AsmSyncService::~AsmSyncService()
{
    Stop();
    StopPush();
}

void AsmSyncService::StartPush(const PushOptions& options)
{
    StopPush();
    std::lock_guard<std::mutex> lock(_mutex);
    _push = options;
    _pushStop = false;
    _pushing = true;
    _pushState = "waiting";
    _pushes = 0;
    _pushFiles = 0;
    _pushLast = StateNode::Object();
    _pushError.clear();
    _pushThread = std::thread([this, options]() { RunPush(options); });
}

void AsmSyncService::StopPush()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_pushThread.joinable())
            return;
        _pushStop = true;
    }
    _pushWake.notify_all();
    _pushThread.join();
    std::lock_guard<std::mutex> lock(_mutex);
    _pushing = false;
    _pushState = "off";
}

bool AsmSyncService::Pushing() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _pushing;
}

StateNode AsmSyncService::PushStatusValue() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    StateNode body = StateNode::Object();
    body["pushing"] = _pushing;
    body["state"] = _pushState;
    StateNode options = StateNode::Object();
    options["path"] = _push.path;
    options["from"] = _push.from;
    options["assembler"] = _push.assembler;
    options["title"] = _push.title;
    options["force"] = _push.force;
    options["interval"] = static_cast<unsigned>(_push.intervalMs);
    options["quiet"] = static_cast<unsigned>(_push.quietMs);
    body["options"] = std::move(options);
    body["files"] = static_cast<uint64_t>(_pushFiles);
    body["pushes"] = static_cast<uint64_t>(_pushes);
    body["last"] = _pushLast;
    body["error"] = _pushError;
    return body;
}

void AsmSyncService::RunPush(PushOptions options)
{
    std::vector<std::vector<uint8_t>> written;   // the files of the last write: the watch's own text in the guest
    std::string seen, handled = "\x01";          // nothing handled yet: the first look is due
    uint64_t seenAt = 0;
    bool pendingPosted = false;
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_pushStop)
                return;
        }
        size_t files = 0;
        const std::string signature = ProjectSignature(options.path, options.from, files);
        const uint64_t now = NowMs();
        if (signature != seen)
        {
            seen = signature;
            seenAt = now;
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _pushFiles = files;
        }
        if (seen != handled && now - seenAt >= options.quietMs)
        {
            AsmRequest request;
            request.verb = "sync-inject";
            request.options["path"] = options.path;
            request.options["from"] = options.from;
            if (!options.assembler.empty())
                request.options["assembler"] = options.assembler;
            if (!options.title.empty())
                request.options["title"] = options.title;
            if (options.force)
                request.options["force"] = "true";
            if (!options.drive.empty())
                request.options["drive"] = options.drive;
            const AsmReply reply = SyncControl(_context).PushInject(request, written);
            // Written; refused for the user's changes in the guest (stays until the file changes again); refused
            // while the assembler is busy (retried); or not written at all (the conversion, too many sources, an
            // assembler without a writer or an idle rule)
            const bool busy = reply.message.find("not waiting for a command") != std::string::npos ||
                              reply.message.find("no coherent moment") != std::string::npos;
            std::string state, event;
            if (reply.Ok())
            {
                state = event = "pushed";
                handled = seen;
                pendingPosted = false;
            }
            else if (reply.error == AsmControlError::Refused && reply.message.find("changes not saved") != std::string::npos)
            {
                state = event = "conflict";
                handled = seen;
            }
            else if (reply.error == AsmControlError::Refused && busy)
            {
                state = "pending";
                if (!pendingPosted)
                    event = "pending";
                pendingPosted = true;
            }
            else
            {
                state = "error";
                event = "push-error";
                handled = seen;
            }
            const StateNode* assembler = reply.body.find("assembler");
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _pushState = state;
                _pushes += reply.Ok() ? 1 : 0;
                _pushLast = reply.body;
                _pushLast["ok"] = reply.Ok();
                _pushLast["message"] = reply.message;
                _pushError = reply.Ok() ? std::string() : reply.message;
            }
            if (!event.empty())
                Post(_context, event, assembler ? assembler->s : options.assembler);
        }
        std::unique_lock<std::mutex> lock(_mutex);
        _pushWake.wait_for(lock, std::chrono::milliseconds(options.intervalMs), [this]() { return _pushStop; });
    }
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

void AsmSyncService::GatherProject(const WatchOptions& options, sync::BuildInput& input, std::string& note) const
{
    namespace fs = std::filesystem;
    // The folder first: SymbolsFromProject takes the last source of a name, so the disk's file wins
    if (!options.projectDir.empty())
    {
        std::error_code failed;
        for (const fs::directory_entry& entry : fs::directory_iterator(options.projectDir, failed))
        {
            if (!entry.is_regular_file())
                continue;
            std::ifstream in(entry.path(), std::ios::binary);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::string ext = entry.path().extension().string();
            std::string lower(ext);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower == ".asm" || lower == ".a80" || lower == ".z80" || lower == ".inc" || lower == ".s" || lower == ".txt")
            {
                const unrealasm::symbols::SourceProject text = unrealasm::symbols::ProjectFromText(entry.path().string(), bytes);
                input.project.insert(input.project.end(), text.sources.begin(), text.sources.end());
                input.sizes.insert(input.sizes.end(), text.sizes.begin(), text.sizes.end());
            }
            else
                input.sizes.emplace_back(entry.path().filename().string(), bytes.size());
        }
        if (failed)
            note = "project folder " + options.projectDir + ": " + failed.message();
    }
    if (options.projectDrive >= 0 && options.projectDrive <= 3)
    {
        std::vector<unrealasm::containers::TrdosFile> files;
        std::string error;
        if (ReadDiskFiles(_context, static_cast<uint8_t>(options.projectDrive), files, error))
        {
            const unrealasm::symbols::SourceProject disk = unrealasm::symbols::ProjectFromFiles(files);
            input.project.insert(input.project.end(), disk.sources.begin(), disk.sources.end());
            input.sizes.insert(input.sizes.end(), disk.sizes.begin(), disk.sizes.end());
        }
    }
    // Last, so they win: the other texts the assembler holds in memory (ALASM: one per page)
    if (input.descriptor && input.descriptor->textsPerPage)
    {
        asmsync::MachineCopy all;
        std::string error;
        if (asmsync::CopyMachine(_context, all, error))
        {
            const std::vector<ProjectFile> memory = sync::SyncSession::MemoryProject(all.view, *input.descriptor, input.page);
            input.project.insert(input.project.end(), memory.begin(), memory.end());
        }
    }
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
                std::string projectNote;
                GatherProject(options, *input, projectNote);
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
                    hints["project"] = static_cast<uint64_t>(input->project.size());
                    if (!projectNote.empty())
                        hints["project_note"] = projectNote;
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
