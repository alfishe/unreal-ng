#include "symbolcontrol.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "common/base64.h"
#include "common/filehelper.h"
#include "debugger/asm/diskfiles.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/labels/symbolfiles.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "unrealasm/symbols/live.h"

/// region <Errors and the reply>

int SymbolControlErrorHttpStatus(SymbolControlError error)
{
    switch (error)
    {
        case SymbolControlError::None: return 200;
        case SymbolControlError::NotAvailable: return 503;
        case SymbolControlError::BadRequest: return 400;
        case SymbolControlError::NotFound: return 404;
        case SymbolControlError::Conflict: return 409;
    }
    return 500;
}

const char* SymbolControlErrorPhrase(SymbolControlError error)
{
    switch (error)
    {
        case SymbolControlError::None: return "";
        case SymbolControlError::NotAvailable: return "Service Unavailable";
        case SymbolControlError::BadRequest: return "Bad Request";
        case SymbolControlError::NotFound: return "Not Found";
        case SymbolControlError::Conflict: return "Conflict";
    }
    return "Internal Server Error";
}

StateNode SymbolReply::ToValue() const
{
    if (Ok())
        return body;
    StateNode value = StateNode::Object();
    value["error"] = SymbolControlErrorPhrase(error);
    value["message"] = message;
    for (const auto& [key, field] : body.members)
        if (key != "error" && key != "message")
            value[key] = field;
    return value;
}

/// endregion </Errors and the reply>

namespace
{
using namespace unrealasm::symbols;

SymbolReply Fail(SymbolControlError error, std::string message, StateNode body = StateNode::Object())
{
    SymbolReply reply;
    reply.error = error;
    reply.message = std::move(message);
    reply.body = std::move(body);
    return reply;
}

std::string Option(const SymbolRequest& request, const std::string& name)
{
    const auto it = request.options.find(name);
    return it != request.options.end() ? it->second : std::string();
}

/// A number: decimal, 0x / # / $ hex, a trailing h hex; false when it is none
bool ParseNumber(std::string text, int64_t& out)
{
    if (text.empty())
        return false;
    int base = 10;
    bool negative = false;
    if (text[0] == '-')
    {
        negative = true;
        text.erase(0, 1);
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        text.erase(0, 2);
        base = 16;
    }
    else if (!text.empty() && (text[0] == '#' || text[0] == '$'))
    {
        text.erase(0, 1);
        base = 16;
    }
    else if (text.size() > 1 && (text.back() == 'h' || text.back() == 'H'))
    {
        text.pop_back();
        base = 16;
    }
    if (text.empty())
        return false;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, base);
    if (*end != '\0')
        return false;
    out = negative ? -value : value;
    return true;
}

/// "true" / "on" / "1" / "" (a flag) and "false" / "off" / "0"
bool ParseBool(std::string text, bool& out)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (text.empty() || text == "true" || text == "on" || text == "1" || text == "yes")
    {
        out = true;
        return true;
    }
    if (text == "false" || text == "off" || text == "0" || text == "no")
    {
        out = false;
        return true;
    }
    return false;
}

std::vector<std::string> SplitList(const std::string& text)
{
    std::vector<std::string> items;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t comma = text.find(',', start);
        const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!item.empty())
            items.push_back(item);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return items;
}

const char* FamilyName(Family family)
{
    switch (family)
    {
        case Family::Text: return "text";
        case Family::Script: return "script";
        case Family::Native: return "native";
    }
    return "text";
}

const char* SeverityName(unrealasm::Severity severity)
{
    switch (severity)
    {
        case unrealasm::Severity::Info: return "info";
        case unrealasm::Severity::Warning: return "warning";
        case unrealasm::Severity::Error: return "error";
    }
    return "info";
}

StateNode DiagnosticsValue(const unrealasm::Diagnostics& diagnostics)
{
    StateNode list = StateNode::Array();
    for (const auto& d : diagnostics)
    {
        StateNode item = StateNode::Object();
        item["severity"] = SeverityName(d.severity);
        item["line"] = static_cast<unsigned>(d.line);
        item["message"] = d.message;
        list.items.push_back(std::move(item));
    }
    return list;
}

/// At least four upper-case hex digits
std::string Hex4(uint32_t value)
{
    static const char* digits = "0123456789ABCDEF";
    std::string hex;
    do
    {
        hex.insert(hex.begin(), digits[value & 0xF]);
        value >>= 4;
    } while (value);
    while (hex.size() < 4)
        hex.insert(hex.begin(), '0');
    return hex;
}

std::string LocationText(const Location& location)
{
    return location.space.Format() + ":#" + Hex4(location.offset);
}

StateNode SetValue(const SymbolSet& set)
{
    StateNode value = StateNode::Object();
    value["id"] = set.id;
    value["title"] = set.title;
    value["origin"]["kind"] = set.origin.kind;
    value["origin"]["where"] = set.origin.where;
    value["priority"] = set.priority;
    value["enabled"] = set.enabled;
    value["symbols"] = static_cast<uint64_t>(set.symbols.size());
    value["case"] = set.caseRule == CaseRule::Fold ? "fold" : "exact";
    return value;
}
}  // namespace

/// region <Construction and dispatch>

SymbolControl::SymbolControl(EmulatorContext* context) : _context(context)
{
    if (context && context->pDebugManager)
        _labels = context->pDebugManager->GetLabelManager();
}

SymbolControl::SymbolControl(LabelManager* labels) : _context(labels ? labels->GetContext() : nullptr), _labels(labels) {}

const std::vector<std::string>& SymbolControl::Verbs()
{
    static const std::vector<std::string> verbs = {"formats", "detect", "sets", "import", "export", "set", "drop", "scan", "import-live"};
    return verbs;
}

const std::vector<std::string>& SymbolControl::OptionsFor(const std::string& verb)
{
    static const std::map<std::string, std::vector<std::string>> options = {
        {"formats", {}},
        {"detect", {"path"}},
        {"sets", {}},
        {"import", {"path", "data", "name", "format", "set", "space", "base", "policy"}},
        {"export", {"path", "format", "sets", "pages"}},
        {"set", {"id", "enabled", "priority"}},
        {"drop", {"id"}},
        {"scan", {}},
        {"import-live", {"scanner", "page", "offset", "set", "policy"}},
    };
    static const std::vector<std::string> none;
    const auto it = options.find(verb);
    return it != options.end() ? it->second : none;
}

SymbolReply SymbolControl::Execute(const SymbolRequest& request)
{
    std::string verb = request.verb;
    std::transform(verb.begin(), verb.end(), verb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto& verbs = Verbs();
    if (std::find(verbs.begin(), verbs.end(), verb) == verbs.end())
    {
        std::string known;
        for (const std::string& v : verbs)
            known += (known.empty() ? "" : ", ") + v;
        return Fail(SymbolControlError::BadRequest, "unknown symbol verb '" + request.verb + "' (verbs: " + known + ")");
    }
    // Every option name is checked, so a typo fails the same way on every surface
    const auto& allowed = OptionsFor(verb);
    for (const auto& [name, value] : request.options)
    {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
        {
            std::string list;
            for (const std::string& o : allowed)
                list += (list.empty() ? "" : ", ") + o;
            return Fail(SymbolControlError::BadRequest, "'" + verb + "' has no option '" + name + "'" +
                                                            (list.empty() ? std::string(" (it takes none)") : " (options: " + list + ")"));
        }
    }
    if (verb == "formats")
        return Formats();
    if (verb == "detect")
        return Detect(request);
    if (!_labels)
        return Fail(SymbolControlError::NotAvailable, "the emulator has no label manager (debug manager not available)");
    if (verb == "sets")
        return Sets();
    if (verb == "import")
        return Import(request);
    if (verb == "export")
        return Export(request);
    if (verb == "set")
        return Set(request);
    if (verb == "scan")
        return Scan();
    if (verb == "import-live")
        return ImportLive(request);
    return Drop(request);
}

/// endregion </Construction and dispatch>

/// region <Verbs>

SymbolReply SymbolControl::Formats()
{
    SymbolReply reply;
    StateNode list = StateNode::Array();
    for (const auto& codec : SymbolCodecRegistry::Builtin().All())
    {
        const CodecInfo& info = codec->Info();
        StateNode item = StateNode::Object();
        item["id"] = info.id;
        item["title"] = info.title;
        item["family"] = FamilyName(info.family);
        item["extensions"] = StateNode::Array();
        for (const std::string& extension : info.extensions)
            item["extensions"].items.emplace_back(extension);
        item["pages"] = info.pages;
        item["comment"] = info.comment;
        list.items.push_back(std::move(item));
    }
    reply.body["formats"] = std::move(list);
    return reply;
}

SymbolReply SymbolControl::Detect(const SymbolRequest& request)
{
    const std::string path = Option(request, "path");
    if (path.empty())
        return Fail(SymbolControlError::BadRequest, "'detect' needs a path");
    std::ifstream file(FileHelper::ToFsPath(path), std::ios::binary);
    if (!file.is_open())
        return Fail(SymbolControlError::BadRequest, "Cannot read the symbol file: " + path);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string extension = FileHelper::ToFsPath(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    if (!extension.empty() && extension[0] == '.')
        extension.erase(0, 1);
    const auto detected = SymbolCodecRegistry::Builtin().Detect(bytes, extension);
    int score = 0;
    std::string reason;
    const ISymbolCodec* chosen = LabelManager::CodecForFile(path, bytes, "", score, reason);

    SymbolReply reply;
    reply.body["path"] = path;
    reply.body["candidates"] = StateNode::Array();
    for (const auto& candidate : detected.candidates)
    {
        StateNode item = StateNode::Object();
        item["format"] = candidate.codec->Info().id;
        item["score"] = candidate.score;
        reply.body["candidates"].items.push_back(std::move(item));
    }
    reply.body["format"] = chosen ? StateNode(chosen->Info().id) : StateNode();
    reply.body["score"] = score;
    reply.body["reason"] = chosen ? detected.reason : reason;
    return reply;
}

SymbolReply SymbolControl::Sets()
{
    SymbolReply reply;
    reply.body["sets"] = StateNode::Array();
    for (const SymbolSet& set : _labels->GetSymbolSets())
        reply.body["sets"].items.push_back(SetValue(set));
    reply.body["labels"] = static_cast<uint64_t>(_labels->GetLabelCount());
    return reply;
}

SymbolReply SymbolControl::Import(const SymbolRequest& request)
{
    const std::string path = Option(request, "path");
    const bool upload = request.options.count("data") > 0;
    if (path.empty() == !upload)
        return Fail(SymbolControlError::BadRequest, "'import' needs a path or data (the file as base64), not both");
    std::vector<uint8_t> bytes;
    if (upload && !base64::Decode(Option(request, "data"), bytes))
        return Fail(SymbolControlError::BadRequest, "'data' is no base64");
    const std::string name = upload ? (Option(request, "name").empty() ? std::string("upload") : Option(request, "name")) : path;
    SymbolImportRequest options;
    options.format = Option(request, "format");
    options.set = Option(request, "set");
    if (request.options.count("space"))
    {
        AddressSpace space;
        if (!AddressSpace::Parse(Option(request, "space"), space))
            return Fail(SymbolControlError::BadRequest, "'space' is no address space: '" + Option(request, "space") +
                                                            "' (cpu:main, rom0, ram3, cache0, const, port, ...)");
        options.space = space;
    }
    if (request.options.count("base"))
    {
        int64_t base = 0;
        if (!ParseNumber(Option(request, "base"), base) || base < 0 || base > 0xFFFFFFFFLL)
            return Fail(SymbolControlError::BadRequest, "'base' is no number: '" + Option(request, "base") + "'");
        options.base = static_cast<uint32_t>(base);
    }
    if (request.options.count("policy"))
    {
        MergePolicy policy = MergePolicy::Both;
        if (!ParsePolicy(Option(request, "policy"), policy))
            return Fail(SymbolControlError::BadRequest, "'policy' is one of both, keep, replace, fail: '" + Option(request, "policy") + "'");
        options.policy = policy;
    }

    // A file on a disk in a drive (disk:A/NAME.T) is read through the disk adapter
    DiskFileRef disk;
    const bool onDisk = !upload && ParseDiskFileRef(path, disk);
    if (onDisk)
    {
        unrealasm::containers::TrdosFile file;
        std::string error;
        if (!ReadDiskFile(_context, disk, file, error))
            return Fail(SymbolControlError::NotFound, error);
        bytes = file.data;
    }
    const SymbolImportResult result = upload   ? _labels->ImportSymbolBytes(bytes, name, options, "upload")
                                      : onDisk ? _labels->ImportSymbolBytes(bytes, path, options, "disk")
                                               : _labels->ImportSymbols(path, options);
    StateNode body = StateNode::Object();
    body["path"] = name;
    body["format"] = result.format.empty() ? StateNode() : StateNode(result.format);
    body["score"] = result.score;
    body["set"] = result.report.set.empty() ? StateNode() : StateNode(result.report.set);
    body["records"] = static_cast<uint64_t>(result.records);
    body["added"] = static_cast<uint64_t>(result.report.added);
    body["aliased"] = static_cast<uint64_t>(result.report.aliased);
    body["updated"] = static_cast<uint64_t>(result.report.updated);
    body["skipped"] = static_cast<uint64_t>(result.report.skipped);
    body["conflicts"] = StateNode::Array();
    for (const Conflict& c : result.report.conflicts)
    {
        StateNode item = StateNode::Object();
        item["type"] = c.type == Conflict::Type::SamePlace ? "same-place" : "moved";
        item["name"] = c.name;
        item["other"] = c.other;
        item["old"] = LocationText(c.oldLocation);
        item["new"] = LocationText(c.newLocation);
        item["line"] = static_cast<unsigned>(c.line);
        item["resolution"] = c.resolution;
        body["conflicts"].items.push_back(std::move(item));
    }
    body["diagnostics"] = DiagnosticsValue(result.report.diagnostics);
    body["labels"] = static_cast<uint64_t>(_labels->GetLabelCount());
    if (!result.ok)
        return Fail(result.report.conflicts.empty() ? SymbolControlError::BadRequest : SymbolControlError::Conflict, result.message,
                    std::move(body));
    SymbolReply reply;
    reply.body = std::move(body);
    return reply;
}

SymbolReply SymbolControl::Export(const SymbolRequest& request)
{
    const std::string path = Option(request, "path");
    if (path.empty())
        return Fail(SymbolControlError::BadRequest, "'export' needs a path");
    SymbolExportRequest options;
    options.format = Option(request, "format");
    options.sets = SplitList(Option(request, "sets"));
    if (request.options.count("pages"))
    {
        const std::string pages = Option(request, "pages");
        if (pages == "fold")
            options.pages = Unrepresentable::Fold;
        else if (pages == "comment")
            options.pages = Unrepresentable::Comment;
        else if (pages == "drop")
            options.pages = Unrepresentable::Drop;
        else
            return Fail(SymbolControlError::BadRequest, "'pages' is one of fold, comment, drop: '" + pages + "'");
    }
    const SymbolExportResult result = _labels->ExportSymbols(path, options);
    StateNode body = StateNode::Object();
    body["path"] = path;
    body["format"] = result.format.empty() ? StateNode() : StateNode(result.format);
    body["written"] = static_cast<uint64_t>(result.written);
    body["diagnostics"] = DiagnosticsValue(result.diagnostics);
    if (!result.ok)
    {
        const bool missingSet = result.message.compare(0, 14, "No symbol set:") == 0;
        return Fail(missingSet ? SymbolControlError::NotFound : SymbolControlError::BadRequest, result.message, std::move(body));
    }
    SymbolReply reply;
    reply.body = std::move(body);
    return reply;
}

SymbolReply SymbolControl::Set(const SymbolRequest& request)
{
    const std::string id = Option(request, "id");
    if (id.empty())
        return Fail(SymbolControlError::BadRequest, "'set' needs the set's id");
    const bool hasEnabled = request.options.count("enabled") > 0;
    const bool hasPriority = request.options.count("priority") > 0;
    if (!hasEnabled && !hasPriority)
        return Fail(SymbolControlError::BadRequest, "'set' needs enabled or priority");
    bool enabled = true;
    if (hasEnabled && !ParseBool(Option(request, "enabled"), enabled))
        return Fail(SymbolControlError::BadRequest, "'enabled' is true or false: '" + Option(request, "enabled") + "'");
    int64_t priority = 0;
    if (hasPriority && (!ParseNumber(Option(request, "priority"), priority) || priority < -1000000000LL || priority > 1000000000LL))
        return Fail(SymbolControlError::BadRequest, "'priority' is no number: '" + Option(request, "priority") + "'");

    const auto sets = _labels->GetSymbolSets();
    if (std::none_of(sets.begin(), sets.end(), [&](const SymbolSet& set) { return set.id == id; }))
        return Fail(SymbolControlError::NotFound, "No symbol set: " + id);
    if (hasEnabled)
        _labels->SetSymbolSetEnabled(id, enabled);
    if (hasPriority)
        _labels->SetSymbolSetPriority(id, static_cast<int>(priority));

    SymbolReply reply;
    for (const SymbolSet& set : _labels->GetSymbolSets())
        if (set.id == id)
            reply.body["set"] = SetValue(set);
    reply.body["labels"] = static_cast<uint64_t>(_labels->GetLabelCount());
    return reply;
}

SymbolReply SymbolControl::Drop(const SymbolRequest& request)
{
    const std::string id = Option(request, "id");
    if (id.empty())
        return Fail(SymbolControlError::BadRequest, "'drop' needs the set's id");
    if (!_labels->DropSymbolSet(id))
        return Fail(SymbolControlError::NotFound, "No symbol set: " + id);
    SymbolReply reply;
    reply.body["dropped"] = id;
    reply.body["labels"] = static_cast<uint64_t>(_labels->GetLabelCount());
    return reply;
}

namespace
{
StateNode CandidateValue(const LiveCandidate& c)
{
    StateNode value = StateNode::Object();
    value["scanner"] = c.scanner;
    value["version"] = c.version;
    value["page"] = static_cast<unsigned>(c.page);
    value["offset"] = static_cast<uint64_t>(c.offset);
    value["end"] = static_cast<uint64_t>(c.end);
    value["count"] = static_cast<uint64_t>(c.count);
    value["score"] = c.score;
    if (c.lowerPage >= 0)
    {
        value["lower_page"] = c.lowerPage;
        value["split"] = static_cast<uint64_t>(c.split);
    }
    return value;
}

MemoryView ViewOf(const std::vector<std::vector<uint8_t>>& pages)
{
    MemoryView view;
    for (size_t i = 0; i < pages.size(); i++)
        view.pages.push_back({static_cast<uint16_t>(i), std::span<const uint8_t>(pages[i])});
    return view;
}
}  // namespace

bool SymbolControl::CopyRam(std::vector<std::vector<uint8_t>>& pages, std::string& error) const
{
    if (!_context || !_context->pMemory)
    {
        error = "the label table scan needs a machine (no memory)";
        return false;
    }
    const uint32_t count = std::min<uint32_t>(_context->config.ramsize / 16, MAX_RAM_PAGES);
    if (count == 0)
    {
        error = "the machine reports no RAM pages";
        return false;
    }
    pages.assign(count, std::vector<uint8_t>(PAGE_SIZE));
    Memory* memory = _context->pMemory;
    const auto copy = [&]() {
        for (uint32_t i = 0; i < count; i++)
            if (const uint8_t* page = memory->RAMPageAddress(static_cast<uint16_t>(i)))
                std::copy(page, page + PAGE_SIZE, pages[i].begin());
    };
    if (_context->pEmulator)
    {
        if (_context->pEmulator->RunAtCoherentMoment(copy, 2000) == Emulator::CoherentMoment::Busy)
        {
            error = "no coherent moment to copy the RAM within 2 s (another client is stepping the machine)";
            return false;
        }
    }
    else
        copy();
    return true;
}

SymbolReply SymbolControl::Scan()
{
    std::vector<std::vector<uint8_t>> pages;
    std::string error;
    if (!CopyRam(pages, error))
        return Fail(SymbolControlError::NotAvailable, error);
    SymbolReply reply;
    reply.body["pages"] = static_cast<uint64_t>(pages.size());
    reply.body["candidates"] = StateNode::Array();
    for (const LiveCandidate& c : FindLabelTables(ViewOf(pages)))
        reply.body["candidates"].items.push_back(CandidateValue(c));
    return reply;
}

SymbolReply SymbolControl::ImportLive(const SymbolRequest& request)
{
    std::vector<std::vector<uint8_t>> pages;
    std::string error;
    if (!CopyRam(pages, error))
        return Fail(SymbolControlError::NotAvailable, error);
    int64_t page = -1;
    int64_t offset = -1;
    if (request.options.count("page") && !ParseNumber(Option(request, "page"), page))
        return Fail(SymbolControlError::BadRequest, "'page' is no number: '" + Option(request, "page") + "'");
    if (request.options.count("offset") && !ParseNumber(Option(request, "offset"), offset))
        return Fail(SymbolControlError::BadRequest, "'offset' is no number: '" + Option(request, "offset") + "'");
    SymbolImportRequest options;
    options.set = Option(request, "set");
    if (request.options.count("policy"))
    {
        MergePolicy policy = MergePolicy::Both;
        if (!ParsePolicy(Option(request, "policy"), policy))
            return Fail(SymbolControlError::BadRequest, "'policy' is one of both, keep, replace, fail: '" + Option(request, "policy") + "'");
        options.policy = policy;
    }

    const MemoryView view = ViewOf(pages);
    const std::vector<LiveCandidate> candidates = FindLabelTables(view);
    const std::string scanner = Option(request, "scanner");
    const auto chosen = std::find_if(candidates.begin(), candidates.end(), [&](const LiveCandidate& c) {
        return (scanner.empty() || c.scanner == scanner) && (page < 0 || c.page == page) && (offset < 0 || c.offset == offset);
    });
    if (chosen == candidates.end())
        return Fail(SymbolControlError::NotFound, candidates.empty() ? "no label table in RAM (scanners: ALASM, XAS)"
                                                                     : "no label table in RAM matches scanner / page / offset");
    LiveReadResult read = ReadLabelTable(view, *chosen);
    if (!read.ok)
        return Fail(SymbolControlError::BadRequest, "the label table at ram" + std::to_string(chosen->page) + " could not be read");

    const std::string where = "ram" + std::to_string(chosen->page) + ":#" + Hex4(chosen->offset);
    Origin origin;
    origin.kind = "live";
    origin.where = where;
    const std::string title = read.set.title.empty() ? chosen->scanner + " " + chosen->version + " at " + where : read.set.title;
    SymbolImportResult result =
        _labels->ImportRecords(std::move(read.set.symbols), options, "live:" + chosen->scanner + "@" + where, title, origin);

    StateNode body = StateNode::Object();
    body["candidate"] = CandidateValue(*chosen);
    body["set"] = result.report.set;
    body["records"] = static_cast<uint64_t>(result.records);
    body["added"] = static_cast<uint64_t>(result.report.added);
    body["aliased"] = static_cast<uint64_t>(result.report.aliased);
    body["updated"] = static_cast<uint64_t>(result.report.updated);
    body["skipped"] = static_cast<uint64_t>(result.report.skipped);
    read.diagnostics.insert(read.diagnostics.end(), result.report.diagnostics.begin(), result.report.diagnostics.end());
    body["diagnostics"] = DiagnosticsValue(read.diagnostics);
    body["labels"] = static_cast<uint64_t>(_labels->GetLabelCount());
    if (!result.ok)
        return Fail(SymbolControlError::Conflict, result.message, std::move(body));
    SymbolReply reply;
    reply.body = std::move(body);
    return reply;
}

/// endregion </Verbs>
