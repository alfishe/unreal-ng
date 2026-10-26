#include "debugger/analyzers/analysis/l3session.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>

#include "debugger/analyzers/analysis/analysisjobs.h"
#include "debugger/analyzers/analysis/l3coverage.h"
#include "debugger/analyzers/analysis/l3experiment.h"
#include "debugger/analyzers/analysis/l3review.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

namespace analysis
{

namespace
{

std::string Hex4(uint32_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", value & 0xFFFF);
    return text;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool ParseCount(const std::string& text, uint64_t limit, uint64_t& value)
{
    if (text.empty() || text.size() > 12 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return false;
    value = std::strtoull(text.c_str(), nullptr, 10);
    return value <= limit;
}

Reply Refuse(Error error, const std::string& message)
{
    Reply reply;
    reply.error = error;
    reply.message = message;
    reply.text = message;
    return reply;
}

std::string Option(const Options& options, const char* name)
{
    const auto it = options.find(name);
    return it != options.end() ? it->second : std::string();
}

/// What the last build left: L1, L2, the recording, the build's id
struct Built
{
    std::shared_ptr<const L1Result> l1;
    std::shared_ptr<const L2Result> l2;
    std::shared_ptr<const TraceBundle> bundle;
    uint64_t job = 0;
};

bool GetBuilt(Emulator& emulator, Built& built, Reply& refusal)
{
    const L1Jobs& jobs = L1Jobs::Instance();
    const std::string id = emulator.GetId();
    built.l1 = jobs.Result(id);
    built.l2 = jobs.Structure(id);
    built.bundle = jobs.Bundle(id);
    built.job = jobs.ResultJob(id);
    if (!built.l1 || !built.l2)
    {
        refusal = Refuse(Error::Conflict, "no L2 yet: 'analysis start record=true', run the program, then 'analysis build'");
        return false;
    }
    return true;
}

bool ValidLabel(const std::string& label)
{
    if (label.empty() || label.size() > 32 || (!std::isalpha(static_cast<unsigned char>(label[0])) && label[0] != '_'))
        return false;
    return std::all_of(label.begin(), label.end(),
                       [](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; });
}

const Finding* FindById(const std::vector<Finding>& findings, const std::string& id)
{
    for (const Finding& f : findings)
        if (f.id == id || Lower(f.id) == Lower(id))
            return &f;
    return nullptr;
}

ReviewTools ToolsFor(Emulator& emulator, const L1Result& l1, Z80Disassembler& disassembler)
{
    (void)emulator;
    ReviewTools tools;
    tools.disassemble = [&disassembler](const uint8_t* bytes, uint16_t z80) {
        const std::vector<uint8_t> code(bytes, bytes + 4);
        uint8_t length = 0;
        return disassembler.disassembleSingleCommand(code, z80, &length);
    };
    tools.routineSheet = [&l1, &disassembler](uint16_t z80) {
        const Routine* routine = l1.RoutineAtZ80(z80);
        if (routine == nullptr)
            return std::string();
        return l1.FactSheet(
            *routine,
            [&disassembler](const Instruction& ins) {
                const std::vector<uint8_t> bytes(ins.bytes, ins.bytes + sizeof(ins.bytes));
                uint8_t length = 0;
                return disassembler.disassembleSingleCommand(bytes, ins.z80, &length);
            },
            [](uint16_t) { return std::string(); });
    };
    return tools;
}

/// Read a spec from the spec option or a file
bool SpecText(const Options& options, std::string& text, std::string& error)
{
    text = Option(options, "spec");
    const std::string path = Option(options, "path");
    if (text.empty() && path.empty())
    {
        error = "experiment needs a spec (JSON: one experiment or an array) or a path to one";
        return false;
    }
    if (!text.empty() && !path.empty())
    {
        error = "experiment takes a spec or a path, not both";
        return false;
    }
    if (!path.empty())
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "could not read " + path;
            return false;
        }
        std::ostringstream content;
        content << file.rdbuf();
        text = content.str();
    }
    return true;
}

bool ParseParallel(const Options& options, uint32_t& parallel, Reply& refusal)
{
    parallel = 0;
    const std::string text = Option(options, "parallel");
    if (text.empty())
        return true;
    uint64_t value = 0;
    if (!ParseCount(text, 8, value) || value == 0)
    {
        refusal = Refuse(Error::BadRequest, "parallel: workers, 1 to 8");
        return false;
    }
    parallel = static_cast<uint32_t>(value);
    return true;
}

/// Mark findings with experiment results (or add an experiment finding); the reply lines
void Record(const std::string& id, const std::vector<Finding>& findings, const std::vector<ExperimentSpec>& specs,
            const ExperimentBatch& batch)
{
    L3Sessions& sessions = L3Sessions::Instance();
    for (size_t i = 0; i < batch.results.size(); ++i)
    {
        const ExperimentResult& r = batch.results[i];
        if (r.verdict == "error")
            continue;
        const bool observed = r.verdict == "observed";
        const std::string line = "experiment " + ExperimentResultLine(r);
        if (const Finding* f = r.finding.empty() ? nullptr : FindById(findings, r.finding))
        {
            Resolution res;
            res.start = f->start;
            res.statusSet = true;
            res.status = observed ? FindingStatus::Observed : FindingStatus::Refuted;
            res.verdict = r.verdict;
            res.by = "experiment";
            res.evidence.push_back(line);
            sessions.Resolve(id, f->Key(), res);
            continue;
        }
        const ExperimentSpec& spec = specs[i];
        Finding f;
        f.kind = FindingKind::Experiment;
        f.status = observed ? FindingStatus::Observed : FindingStatus::Refuted;
        f.subject = spec.id;
        f.source = "experiment";
        f.confidence = observed ? 900 : 100;
        f.claim = spec.claim.empty() ? "Experiment " + spec.id : spec.claim;
        uint16_t at = 0;
        if (!spec.pokes.empty())
            at = spec.pokes.front().first;
        else if (!spec.watch.empty())
            at = spec.watch.front().address;
        f.start = f.end = at;
        f.evidence.push_back(line);
        f.verdict = r.verdict;
        f.by = "experiment";
        sessions.Add(id, std::move(f));
    }
}

Reply FindingsAction(Emulator& emulator, const Options& options)
{
    Built built;
    Reply refusal;
    if (!GetBuilt(emulator, built, refusal))
        return refusal;
    const std::string id = emulator.GetId();
    L3Sessions& sessions = L3Sessions::Instance();
    const std::string op = Lower(Option(options, "op").empty() ? "list" : Option(options, "op"));

    if (op == "add")
    {
        const std::string claim = Option(options, "claim");
        if (claim.empty())
            return Refuse(Error::BadRequest, "findings op=add needs a claim (one sentence) and a range");
        ExportRange range;
        if (!ParseZ80Range(Option(options, "range"), range))
            return Refuse(Error::BadRequest, "range: the Z80 range the claim is about (8000-80FF, or one address)");
        FindingStatus status = FindingStatus::Inferred;
        if (const std::string s = Lower(Option(options, "status")); !s.empty() && !ParseFindingStatus(s, status))
            return Refuse(Error::BadRequest, "status: observed, inferred, refuted or confirmed");
        Finding f;
        f.kind = FindingKind::Note;
        f.status = status;
        f.start = range.start;
        f.end = range.end;
        f.subject = "note";
        f.by = Option(options, "by").empty() ? "agent" : Option(options, "by");
        f.source = f.by;
        f.confidence = status == FindingStatus::Inferred ? 500 : 900;
        f.claim = claim;
        std::istringstream evidence(Option(options, "evidence"));
        std::string line;
        while (std::getline(evidence, line, ';'))
        {
            const size_t a = line.find_first_not_of(' ');
            if (a != std::string::npos)
                f.evidence.push_back(line.substr(a));
        }
        sessions.Add(id, f);
        const std::vector<Finding> all = sessions.Findings(id, built.job, *built.l1, *built.l2);
        const Finding& added = all.back();
        Reply reply;
        reply.body["finding"] = FindingNode(added);
        reply.body["total"] = static_cast<uint64_t>(all.size());
        reply.text = "Added " + added.id + ": " + FindingRow(added);
        return reply;
    }
    if (op != "list")
        return Refuse(Error::BadRequest, "findings op: list or add");

    std::optional<FindingKind> kind;
    if (const std::string k = Lower(Option(options, "kind")); !k.empty() && k != "all")
    {
        FindingKind parsed = FindingKind::Claim;
        if (!ParseFindingKind(k, parsed))
            return Refuse(Error::BadRequest, "kind: all, claim, conflict, bug, routine, unknown, coverage, note, experiment or entry");
        kind = parsed;
    }
    std::optional<FindingStatus> status;
    if (const std::string s = Lower(Option(options, "status")); !s.empty() && s != "all")
    {
        FindingStatus parsed = FindingStatus::Inferred;
        if (!ParseFindingStatus(s, parsed))
            return Refuse(Error::BadRequest, "status: all, observed, inferred, refuted or confirmed");
        status = parsed;
    }
    ExportRange range{0x0000, 0xFFFF};
    if (const std::string r = Option(options, "range"); !r.empty() && !ParseZ80Range(r, range))
        return Refuse(Error::BadRequest, "range: start-end in hex (8000-83FF)");
    uint64_t limit = 200;
    if (const std::string l = Option(options, "limit"); !l.empty() && !ParseCount(l, 100000, limit))
        return Refuse(Error::BadRequest, "limit: the most findings to answer, 0 to 100000");
    const std::string format = Lower(Option(options, "format").empty() ? "table" : Option(options, "format"));
    if (format != "table" && format != "markdown" && format != "jsonl")
        return Refuse(Error::BadRequest, "format: table, markdown (zx-reaper re/findings.md) or jsonl");

    const std::vector<Finding> all = sessions.Findings(id, built.job, *built.l1, *built.l2);
    std::vector<Finding> picked;
    constexpr int kKinds = static_cast<int>(FindingKind::Entry) + 1;
    uint64_t byKind[kKinds] = {};
    uint64_t byStatus[4] = {};
    for (const Finding& f : all)
    {
        ++byKind[static_cast<int>(f.kind)];
        ++byStatus[static_cast<int>(f.status)];
        if ((kind && f.kind != *kind) || (status && f.status != *status) || f.end < range.start || f.start > range.end)
            continue;
        picked.push_back(f);
    }
    Reply reply;
    StateNode list = StateNode::Array();
    std::string table;
    for (size_t i = 0; i < picked.size() && i < limit; ++i)
    {
        list.push(FindingNode(picked[i]));
        table += FindingRow(picked[i]) + "\n";
    }
    reply.body["findings"] = list;
    reply.body["count"] = static_cast<uint64_t>(picked.size());
    reply.body["total"] = static_cast<uint64_t>(all.size());
    reply.body["truncated"] = picked.size() > limit;
    StateNode kinds = StateNode::Object();
    for (int k = 0; k < kKinds; ++k)
        kinds[FindingKindName(static_cast<FindingKind>(k))] = byKind[k];
    reply.body["by_kind"] = kinds;
    StateNode statuses = StateNode::Object();
    for (int s = 0; s < 4; ++s)
        statuses[FindingStatusName(static_cast<FindingStatus>(s))] = byStatus[s];
    reply.body["by_status"] = statuses;
    reply.body["format"] = format;

    std::string summary = "Findings: " + std::to_string(all.size()) + " (";
    for (int k = 0; k < kKinds; ++k)
        if (byKind[k] != 0)
            summary += std::string(summary.back() == '(' ? "" : ", ") + std::to_string(byKind[k]) + " " +
                       FindingKindName(static_cast<FindingKind>(k));
    summary += "); " + std::to_string(picked.size()) + " match";
    if (format == "table")
    {
        reply.body["content"] = "";
        reply.body["path"] = "";
        reply.text = summary + (picked.size() > limit ? " (first " + std::to_string(limit) + ")" : "") + "\n" + table;
        return reply;
    }
    const std::string content = format == "markdown" ? FindingsMarkdown(picked, Option(options, "title"))
                                                     : FindingsJsonl(picked);
    const std::string path = Option(options, "path");
    if (!path.empty())
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (file)
            file << content;
        if (!file)
            return Refuse(Error::Conflict, "could not write " + path);
        reply.body["content"] = "";
        reply.body["path"] = path;
        reply.text = summary + "; " + format + " (" + std::to_string(content.size()) + " bytes) written to " + path;
        return reply;
    }
    constexpr size_t kTextMax = 16384;
    reply.body["content"] = content;
    reply.body["path"] = "";
    reply.text = summary + "; " + format + ", " + std::to_string(content.size()) + " bytes" +
                 (content.size() > kTextMax ? " (the first 16 KB here, all in content; or give a path)" : "") + "\n" +
                 content.substr(0, kTextMax);
    return reply;
}

/// Kinds a review may give a range (the structure kinds need a geometry the review does not give)
bool ReviewKind(const std::string& text)
{
    RegionKind kind = RegionKind::Data;
    return ParseRegionKind(text, kind) && kind != RegionKind::Buffer && kind != RegionKind::RecordArray &&
           kind != RegionKind::InitTable && kind != RegionKind::Untouched;
}

/// A label taken at another address than @p z80: a draft name, an entry's, a finding's, a batch name
std::string LabelTaken(const std::string& label, uint16_t z80, const std::vector<Finding>& findings,
                       const L2Result& l2, const NamingResult& naming, const std::map<uint16_t, GivenName>& names)
{
    for (const auto& [at, name] : l2.entries)
        if (name == label && at != z80)
            return "label " + label + " is the name of " + Hex4(at) + " already";
    for (const Finding& other : findings)
        if (other.label == label && other.start != z80)
            return "label " + label + " was given to " + other.id + " (" + Hex4(other.start) + ") already";
    for (const auto& [at, name] : names)
        if (name.label == label && at != z80)
            return "label " + label + " was given to " + Hex4(at) + " already";
    for (const NamingBatch& b : naming.batches)
        for (const NamingSubject& s : b.subjects)
            if (s.label == label && s.z80 != z80)
                return "label " + label + " is the draft's name of " + Hex4(s.z80);
    return {};
}

/// review op=resolve id=N3 names=...: the names of a naming batch
Reply ResolveNames(const std::string& id, const NamingBatch& batch, const Options& options,
                   const std::vector<Finding>& findings, const L2Result& l2, const NamingResult& naming)
{
    L3Sessions& sessions = L3Sessions::Instance();
    const std::string verdict = Lower(Option(options, "verdict"));
    const std::string text = Option(options, "names");
    if (!verdict.empty() && verdict != "skip")
        return Refuse(Error::BadRequest, "a naming batch takes names (JSON) or verdict=skip");
    if (text.empty() && verdict.empty())
        return Refuse(Error::BadRequest, "resolve " + batch.id + " needs names: {\"$XXXX\": \"Name\", ...} (or "
                                         "verdict=skip)");
    std::vector<GivenName> names;
    std::string error;
    if (!text.empty() && !ParseGivenNames(text, names, error))
        return Refuse(Error::BadRequest, error);
    const std::map<uint16_t, GivenName> given = sessions.Names(id);
    std::set<std::string> seen;
    for (GivenName& n : names)
    {
        const auto subject = std::find_if(batch.subjects.begin(), batch.subjects.end(),
                                          [&](const NamingSubject& s) { return s.z80 == n.z80; });
        if (subject == batch.subjects.end())
            return Refuse(Error::BadRequest, Hex4(n.z80) + " is not a subject of " + batch.id);
        if (n.label.empty() && n.title.empty())
            return Refuse(Error::BadRequest, Hex4(n.z80) + ": a label, a title or both");
        if (!n.label.empty())
        {
            if (!ValidLabel(n.label))
                return Refuse(Error::BadRequest, Hex4(n.z80) + ": label " + n.label +
                                                     ": a letter or _, then letters, digits, _ (at most 32)");
            if (!seen.insert(n.label).second)
                return Refuse(Error::BadRequest, "label " + n.label + " twice in one batch");
            if (const std::string taken = LabelTaken(n.label, n.z80, findings, l2, naming, given); !taken.empty())
                return Refuse(Error::Conflict, taken);
        }
    }
    std::vector<uint16_t> skipped;
    for (const NamingSubject& s : batch.subjects)
        if (std::none_of(names.begin(), names.end(), [&](const GivenName& n) { return n.z80 == s.z80; }))
            skipped.push_back(s.z80);
    sessions.Name(id, names, skipped);

    Reply reply;
    StateNode list = StateNode::Array();
    std::string lines;
    for (const GivenName& n : names)
    {
        StateNode node = StateNode::Object();
        node["at"] = Hex4(n.z80);
        node["label"] = n.label;
        node["title"] = n.title;
        list.push(node);
        lines += "\n  " + Hex4(n.z80) + " " + (n.label.empty() ? "(title)" : n.label) +
                 (n.title.empty() ? "" : " \"" + n.title + "\"");
    }
    reply.body["batch"] = batch.id;
    reply.body["named"] = list;
    reply.body["skipped"] = static_cast<uint64_t>(skipped.size());
    reply.text = batch.id + " resolved: " + std::to_string(names.size()) + " named, " +
                 std::to_string(skipped.size()) + " skipped" + lines + "\nThe draft export carries them.";
    return reply;
}

Reply ReviewAction(Emulator& emulator, const Options& options)
{
    Built built;
    Reply refusal;
    if (!GetBuilt(emulator, built, refusal))
        return refusal;
    const std::string id = emulator.GetId();
    L3Sessions& sessions = L3Sessions::Instance();
    const std::string op = Lower(Option(options, "op").empty() ? "next" : Option(options, "op"));
    if (op != "next" && op != "list" && op != "show" && op != "resolve")
        return Refuse(Error::BadRequest, "review op: next, list, show or resolve");
    ReviewOptions reviewOptions;
    if (const std::string r = Option(options, "ranges"); !r.empty() && !ParseZ80Ranges(r, reviewOptions.ranges))
        return Refuse(Error::BadRequest, "ranges: hex ranges without overlap, comma separated (8000-FFFF)");
    if (const std::string l = Option(options, "limit"); !l.empty())
    {
        uint64_t limit = 0;
        if (!ParseCount(l, 1000, limit) || limit == 0)
            return Refuse(Error::BadRequest, "limit: the queue's cap, 1 to 1000 (default 40)");
        reviewOptions.limit = static_cast<uint32_t>(limit);
    }
    if (const std::string o = Option(options, "offset"); !o.empty())
    {
        uint64_t offset = 0;
        if (!ParseCount(o, 100000, offset))
            return Refuse(Error::BadRequest, "offset: items of the queue to skip, 0 to 100000");
        reviewOptions.offset = static_cast<uint32_t>(offset);
    }
    if (const std::string r = Lower(Option(options, "reason")); !r.empty() && r != "all")
    {
        reviewOptions.reason = ParseReviewReason(r);
        if (reviewOptions.reason < 0)
            return Refuse(Error::BadRequest,
                          "reason: all, bug, conflict, unnamed-hot, low-confidence, unknown, entry or name");
    }

    std::vector<Finding> findings = sessions.Findings(id, built.job, *built.l1, *built.l2);
    const NamingResult naming = sessions.Naming(id, built.job, *built.l1, *built.l2);
    reviewOptions.names = &naming.batches;
    reviewOptions.named = [&](const NamingBatch& b) { return sessions.Named(id, b); };
    const auto batchById = [&](const std::string& batchId) -> const NamingBatch* {
        for (const NamingBatch& b : naming.batches)
            if (Lower(b.id) == Lower(batchId))
                return &b;
        return nullptr;
    };
    Z80Disassembler disassembler(emulator.GetContext());
    const Z80Disassembler::NoLabelsScope plainAddresses;
    const ReviewTools tools = ToolsFor(emulator, *built.l1, disassembler);

    if (op == "resolve")
    {
        const std::string findingId = Option(options, "id");
        if (findingId.empty())
            return Refuse(Error::BadRequest, "review op=resolve needs id=<finding> (F12) or id=<naming batch> (N3)");
        if (const NamingBatch* batch = batchById(findingId))
        {
            Reply reply = ResolveNames(id, *batch, options, findings, *built.l2, naming);
            if (reply.error == Error::None)
            {
                const ReviewQueue queue = SelectReview(findings, *built.l2, reviewOptions);
                reply.body["open"] = static_cast<uint64_t>(queue.total);
                reply.text += " " + std::to_string(queue.total) + " items left (analysis review op=next)";
            }
            return reply;
        }
        if (!Option(options, "names").empty())
            return Refuse(Error::BadRequest, "names: for a naming batch (N3) only");
        const Finding* f = FindById(findings, findingId);
        if (f == nullptr)
            return Refuse(Error::NotFound, "no finding " + findingId + " (analysis findings lists them)");
        const std::string verdict = Lower(Option(options, "verdict"));
        if (!verdict.empty() && verdict != "ok" && verdict != "wrong" && verdict != "skip")
            return Refuse(Error::BadRequest, "verdict: ok (confirmed), wrong (refuted) or skip (out of the queue)");
        const std::string label = Option(options, "label");
        if (!label.empty() && !ValidLabel(label))
            return Refuse(Error::BadRequest, "label: a letter or _, then letters, digits, _ (at most 32): an assembler name");
        if (!label.empty())
            if (const std::string taken = LabelTaken(label, f->start, findings, *built.l2, naming, sessions.Names(id));
                !taken.empty())
                return Refuse(Error::Conflict, taken);
        const std::string kind = Lower(Option(options, "kind"));
        if (!kind.empty() && !ReviewKind(kind))
            return Refuse(Error::BadRequest, "kind: code, data, text, font, sprite, tile, screen, variable, "
                                             "pointer_table, handler_table, rng, music, packed, level_map or unused");
        if (!kind.empty() && (f->start >> 14) != (f->end >> 14))
            return Refuse(Error::BadRequest, "kind: the finding's range crosses a 16 KB page");
        const std::string specText = Option(options, "experiment");
        if (verdict.empty() && label.empty() && Option(options, "title").empty() && Option(options, "comment").empty() &&
            specText.empty() && kind.empty())
            return Refuse(Error::BadRequest, "resolve needs a verdict, a label, a title, a comment, a kind or an experiment");
        const Finding target = *f;

        Reply reply;
        std::string experimentLine;
        if (!specText.empty())
        {
            if (!built.bundle)
                return Refuse(Error::Conflict, "no recording kept for experiments: run 'analysis build' again");
            std::vector<ExperimentSpec> specs;
            std::string error;
            if (!ParseExperiments(specText, specs, error))
                return Refuse(Error::BadRequest, "experiment: " + error);
            for (ExperimentSpec& spec : specs)
                spec.finding = target.id;
            ExperimentRunOptions run;
            run.l1 = built.l1.get();
            const ExperimentBatch batch = RunExperiments(*built.bundle, specs, run);
            if (!batch.error.empty())
                return Refuse(Error::Conflict, batch.error);
            Record(id, findings, specs, batch);
            StateNode results = StateNode::Array();
            for (const ExperimentResult& r : batch.results)
            {
                results.push(ExperimentResultNode(r));
                experimentLine += "\n  " + ExperimentResultLine(r);
            }
            reply.body["experiments"] = results;
        }
        Resolution res;
        res.start = target.start;
        res.label = label;
        res.title = Option(options, "title");
        res.comment = Option(options, "comment");
        res.by = Option(options, "by").empty() ? "agent" : Option(options, "by");
        if (!kind.empty())
        {
            res.kind = kind;
            res.kindEnd = target.end;
        }
        if (!verdict.empty())
        {
            res.verdict = verdict;
            if (verdict != "skip")
            {
                res.statusSet = true;
                res.status = verdict == "ok" ? FindingStatus::Confirmed : FindingStatus::Refuted;
            }
            res.entry = target.kind == FindingKind::Entry && verdict == "ok";
        }
        sessions.Resolve(id, target.Key(), res);
        findings = sessions.Findings(id, built.job, *built.l1, *built.l2);
        const Finding* now = FindById(findings, target.id);
        const ReviewQueue queue = SelectReview(findings, *built.l2, reviewOptions);
        reply.body["finding"] = FindingNode(*now);
        reply.body["open"] = static_cast<uint64_t>(queue.total);
        reply.body["kind"] = kind;
        reply.text = target.id + " resolved: " + FindingStatusName(now->status) +
                     (now->label.empty() ? "" : ", label " + now->label) +
                     (now->title.empty() ? "" : ", title \"" + now->title + "\"") +
                     (res.comment.empty() ? "" : ", a comment") +
                     (kind.empty() ? "" : ", " + Hex4(target.start) + "-" + Hex4(target.end) + " made " + kind) +
                     (res.entry ? ", a routine starts at " + Hex4(target.start) : "") + experimentLine +
                     "\nThe draft export carries it. " + std::to_string(queue.total) +
                     " items left (analysis review op=next)";
        return reply;
    }

    const ReviewQueue queue = SelectReview(findings, *built.l2, reviewOptions);
    StateNode queueNode = StateNode::Object();
    queueNode["open"] = static_cast<uint64_t>(queue.total);
    queueNode["shown"] = static_cast<uint64_t>(queue.items.size());
    queueNode["offset"] = static_cast<uint64_t>(queue.offset);
    queueNode["resolved"] = static_cast<uint64_t>(queue.resolved);
    queueNode["summary"] = queue.summary;
    queueNode["reason"] = reviewOptions.reason >= 0 ? ReviewReasonName(reviewOptions.reason) : "all";
    StateNode reasons = StateNode::Object();
    for (int r = 0; r < kReviewReasons; ++r)
        reasons[ReviewReasonName(r)] = static_cast<uint64_t>(queue.byReason[r]);
    queueNode["by_reason"] = reasons;
    queueNode["name_subjects"] = static_cast<uint64_t>(naming.subjects);
    queueNode["name_capped"] = static_cast<uint64_t>(naming.capped);
    const auto keyOf = [&](const ReviewItem& item) {
        return item.batch ? "name:" + naming.batches[item.finding].id : findings[item.finding].Key();
    };
    const auto stateOf = [&](const ReviewItem& item) { return sessions.Shown(id, keyOf(item)) ? "shown" : "queued"; };
    const auto claimOf = [&](const ReviewItem& item) {
        return item.batch ? NamingItemClaim(naming.batches[item.finding]) : findings[item.finding].claim;
    };

    if (op == "list")
    {
        Reply reply;
        StateNode items = StateNode::Array();
        std::string text;
        for (const ReviewItem& item : queue.items)
        {
            items.push(item.batch ? NamingItemNode(naming.batches[item.finding], item, stateOf(item))
                                  : ReviewItemNode(findings[item.finding], item, stateOf(item)));
            char head[48];
            std::snprintf(head, sizeof(head), "%-5s %-14s %-6s ", item.id.c_str(), item.reason.c_str(), stateOf(item));
            text += head + claimOf(item).substr(0, 110) + "\n";
        }
        if (!queue.summary.empty())
        {
            StateNode more = StateNode::Object();
            more["id"] = "more";
            more["reason"] = "summary";
            more["state"] = "queued";
            more["claim"] = queue.summary;
            items.push(more);
            text += "more  " + queue.summary + "\n";
        }
        reply.body["items"] = items;
        reply.body["queue"] = queueNode;
        std::string counts;
        static const char* const kWords[kReviewReasons] = {"bugs",    "conflicts",        "unnamed hot routines",
                                                           "low confidence", "unknown", "entry candidates",
                                                           "naming batches"};
        for (int r = 0; r < kReviewReasons; ++r)
            counts += (r ? ", " : "") + std::to_string(queue.byReason[r]) + " " + kWords[r];
        reply.text = "Review queue: " + std::to_string(queue.total) + " open" +
                     (reviewOptions.reason >= 0 ? std::string(" (reason ") + ReviewReasonName(reviewOptions.reason) + ")"
                                                : std::string()) +
                     (queue.offset != 0 ? ", from " + std::to_string(queue.offset) : std::string()) + " (" + counts +
                     "), " + std::to_string(queue.resolved) + " resolved\n" + text;
        return reply;
    }

    const ReviewItem* item = nullptr;
    ReviewItem single;
    if (op == "show")
    {
        const std::string findingId = Option(options, "id");
        const NamingBatch* batch = batchById(findingId);
        const Finding* f = batch != nullptr ? nullptr : FindById(findings, findingId);
        if (f == nullptr && batch == nullptr)
            return Refuse(findingId.empty() ? Error::BadRequest : Error::NotFound,
                          findingId.empty() ? "review op=show needs id=<finding> or id=<naming batch>"
                                            : "no finding or naming batch " + findingId);
        for (const ReviewItem& i : queue.items)
            if (i.id == (batch != nullptr ? batch->id : f->id))
                item = &i;
        if (item == nullptr)
        {
            single.finding = batch != nullptr ? static_cast<size_t>(batch - naming.batches.data())
                                              : static_cast<size_t>(f - findings.data());
            single.id = batch != nullptr ? batch->id : f->id;
            single.reason = "not queued";
            single.batch = batch != nullptr;
            item = &single;
        }
    }
    else
    {
        for (const ReviewItem& i : queue.items)
            if (!sessions.Shown(id, keyOf(i)))
            {
                item = &i;
                break;
            }
        if (item == nullptr && !queue.items.empty())
            item = &queue.items.front();
    }
    Reply reply;
    reply.body["queue"] = queueNode;
    if (item == nullptr)
    {
        reply.body["item"] = StateNode();
        reply.body["sheet"] = "";
        reply.text = "The review queue is empty: " + std::to_string(queue.resolved) +
                     " findings resolved. Export the draft (analysis export) and run zx-reaper's make, make test and "
                     "the shift tests";
        return reply;
    }
    const std::string state = op == "next" ? "shown" : stateOf(*item);
    std::string sheet;
    if (item->batch)
    {
        const NamingBatch& batch = naming.batches[item->finding];
        const DraftPlan plan = MakePlan(*built.l1, *built.l2, ExportOptions{});
        sheet = NamingSheet(batch, *built.l1, *built.l2, plan, tools.disassemble);
        reply.body["item"] = NamingItemNode(batch, *item, state);
        reply.body["finding"] = StateNode();
    }
    else
    {
        const Finding& finding = findings[item->finding];
        sheet = ReviewSheet(finding, *item, *built.l1, *built.l2, tools);
        reply.body["item"] = ReviewItemNode(finding, *item, state);
        reply.body["finding"] = FindingNode(finding);
    }
    sessions.MarkShown(id, keyOf(*item));
    reply.body["sheet"] = sheet;
    reply.body["sheet_bytes"] = static_cast<uint64_t>(sheet.size());
    reply.text = sheet + "queue: " + std::to_string(queue.total) + " open" +
                 (queue.summary.empty() ? "" : "; " + queue.summary);
    return reply;
}

Reply ExperimentAction(Emulator& emulator, const Options& options)
{
    Built built;
    Reply refusal;
    if (!GetBuilt(emulator, built, refusal))
        return refusal;
    if (!built.bundle)
        return Refuse(Error::Conflict, "no recording kept for experiments: run 'analysis build' again");
    std::string text, error;
    if (!SpecText(options, text, error))
        return Refuse(Error::BadRequest, error);
    std::vector<ExperimentSpec> specs;
    if (!ParseExperiments(text, specs, error))
        return Refuse(Error::BadRequest, "experiment: " + error);
    const std::string id = emulator.GetId();
    const std::vector<Finding> findings = L3Sessions::Instance().Findings(id, built.job, *built.l1, *built.l2);
    if (const std::string finding = Option(options, "finding"); !finding.empty())
    {
        if (specs.size() != 1)
            return Refuse(Error::BadRequest, "finding: with one experiment only (an array names its findings inside)");
        specs[0].finding = finding;
    }
    for (const ExperimentSpec& spec : specs)
        if (!spec.finding.empty() && FindById(findings, spec.finding) == nullptr)
            return Refuse(Error::NotFound, "no finding " + spec.finding + " (experiment " + spec.id + ")");
    ExperimentRunOptions run;
    if (!ParseParallel(options, run.parallel, refusal))
        return refusal;
    run.l1 = built.l1.get();
    const ExperimentBatch batch = RunExperiments(*built.bundle, specs, run);
    if (!batch.error.empty())
        return Refuse(Error::Conflict, batch.error);
    Record(id, findings, specs, batch);

    Reply reply;
    StateNode results = StateNode::Array();
    uint64_t observed = 0, refuted = 0, failed = 0;
    std::string lines;
    for (const ExperimentResult& r : batch.results)
    {
        results.push(ExperimentResultNode(r));
        observed += r.verdict == "observed" ? 1 : 0;
        refuted += r.verdict == "refuted" ? 1 : 0;
        failed += r.verdict == "error" ? 1 : 0;
        lines += ExperimentResultLine(r) + "\n";
    }
    reply.body["results"] = results;
    reply.body["observed"] = observed;
    reply.body["refuted"] = refuted;
    reply.body["errors"] = failed;
    reply.body["workers"] = static_cast<uint64_t>(batch.workers);
    reply.body["peak_concurrency"] = static_cast<uint64_t>(batch.peakConcurrency);
    reply.body["seeks"] = static_cast<uint64_t>(batch.seeks);
    reply.body["micros"] = batch.micros;
    reply.text = "Experiments: " + std::to_string(specs.size()) + " on " + std::to_string(batch.workers) +
                 (batch.workers == 1 ? " worker" : " workers") + " (analysis instances; the source untouched): " +
                 std::to_string(observed) + " observed, " + std::to_string(refuted) + " refuted" +
                 (failed != 0 ? ", " + std::to_string(failed) + " failed" : "") + ", " +
                 std::to_string(batch.micros / 1000) + " ms\n" + lines;
    return reply;
}

Reply CoverAction(Emulator& emulator, const Options& options)
{
    Built built;
    Reply refusal;
    if (!GetBuilt(emulator, built, refusal))
        return refusal;
    if (!built.bundle)
        return Refuse(Error::Conflict, "no recording kept for coverage: run 'analysis build' again");
    CoverOptions cover;
    const auto count = [&](const char* name, uint64_t low, uint64_t high, uint32_t& out, const char* message) {
        const std::string text = Option(options, name);
        if (text.empty())
            return true;
        uint64_t value = 0;
        if (!ParseCount(text, high, value) || value < low)
        {
            refusal = Refuse(Error::BadRequest, message);
            return false;
        }
        out = static_cast<uint32_t>(value);
        return true;
    };
    if (!count("budget", 1, 4096, cover.budget, "budget: attempts in all, 1 to 4096 (default 64)") ||
        !count("per_branch", 1, 40, cover.perBranch, "per_branch: attempts per branch, 1 to 40 (default 8)") ||
        !count("frames", 1, 1000, cover.frames, "frames: frames an attempt runs, 1 to 1000 (default 50)") ||
        !count("limit", 1, 10000, cover.limit, "limit: branches tried at most, 1 to 10000 (default 200)") ||
        !ParseParallel(options, cover.parallel, refusal))
        return refusal;
    if (const std::string r = Option(options, "ranges"); !r.empty() && !ParseZ80Ranges(r, cover.ranges))
        return Refuse(Error::BadRequest, "ranges: hex ranges without overlap, comma separated (8000-FFFF)");
    const CoverResult result = RunCoverage(*built.bundle, *built.l1, cover);
    if (!result.error.empty())
        return Refuse(Error::Conflict, result.error);

    const std::string id = emulator.GetId();
    L3Sessions& sessions = L3Sessions::Instance();
    sessions.DropAdded(id, FindingKind::Coverage);
    std::map<uint16_t, ExportNote> notes;
    for (const CoverBranch& b : result.branches)
    {
        if (b.status != "flipped")
            continue;
        Finding f;
        f.kind = FindingKind::Coverage;
        f.status = FindingStatus::Observed;
        f.start = f.end = b.z80;
        f.subject = "branch";
        f.source = "coverage";
        f.confidence = 900;
        f.at = b.z80;
        f.claim = CoverBranchLine(b);
        f.evidence.push_back("an experiment from frame " + std::to_string(b.fromFrame) + " after the first keyframe, " +
                             b.how);
        for (const auto& [lo, hi] : b.newRanges)
            f.refs.push_back(Hex4(lo));
        f.by = "coverage";
        sessions.Add(id, f);
        const bool poke = b.how.rfind("poke", 0) == 0;
        const std::string need = poke ? "needs poke " + b.how.substr(5, 9) : "needs " + b.how;
        notes[b.z80].comments.push_back(std::string(b.wantTaken ? "Never taken in the session" : "Always taken in the session") +
                                        "; went the other way with " + b.how + " (" + need + ").");
        for (const auto& [lo, hi] : b.newRanges)
            notes[lo].comments.push_back("Reached only in an experiment: " + Hex4(lo) + "-" + Hex4(hi) +
                                         " ran once the branch at " + Hex4(b.z80) + " flipped (" + need + ").");
    }
    sessions.SetCoverageNotes(id, notes);

    Reply reply;
    StateNode branches = StateNode::Array();
    std::string flipped, other;
    size_t others = 0;
    for (const CoverBranch& b : result.branches)
    {
        branches.push(CoverBranchNode(b));
        if (b.status == "flipped")
            flipped += "  " + CoverBranchLine(b) + "\n";
        else if (others++ < 12)
            other += "  " + CoverBranchLine(b) + "\n";
    }
    reply.body["branches"] = branches;
    reply.body["candidates"] = static_cast<uint64_t>(result.candidates);
    reply.body["tried"] = static_cast<uint64_t>(result.tried);
    reply.body["flipped"] = static_cast<uint64_t>(result.flipped);
    reply.body["attempts"] = static_cast<uint64_t>(result.attempts);
    reply.body["new_code"] = static_cast<uint64_t>(result.newCode);
    reply.body["workers"] = static_cast<uint64_t>(result.workers);
    reply.body["peak_concurrency"] = static_cast<uint64_t>(result.peakConcurrency);
    reply.body["micros"] = result.micros;
    reply.text = "Coverage: " + std::to_string(result.candidates) + " branches went one way only, " +
                 std::to_string(result.tried) + " tried with " + std::to_string(result.attempts) + " attempts, " +
                 std::to_string(result.flipped) + " flipped, " + std::to_string(result.newCode) +
                 " new instructions reached; " + std::to_string(result.micros / 1000) +
                 " ms. Flips are coverage findings and draft notes\n" + flipped +
                 (other.empty() ? "" : "others:\n" + other) +
                 (others > 12 ? "  ... " + std::to_string(others - 12) + " more in branches\n" : "");
    return reply;
}

}  // namespace

L3Sessions& L3Sessions::Instance()
{
    static L3Sessions sessions;
    return sessions;
}

std::vector<Finding> L3Sessions::Findings(const std::string& emulatorId, uint64_t job, const L1Result& l1,
                                          const L2Result& l2)
{
    std::lock_guard<std::mutex> lock(_mutex);
    State& state = _states[emulatorId];
    if (state.job != job || state.base.empty())
    {
        state.base = BaseFindings(l1, l2);
        state.job = job;
    }
    std::vector<Finding> all = state.base;
    all.insert(all.end(), state.added.begin(), state.added.end());
    for (size_t i = state.base.size(); i < all.size(); ++i)
        all[i].id = "F" + std::to_string(i + 1);
    for (Finding& f : all)
    {
        const auto it = state.resolutions.find(f.Key());
        if (it == state.resolutions.end())
            continue;
        const Resolution& r = it->second;
        if (r.statusSet)
            f.status = r.status;
        if (!r.label.empty())
            f.label = r.label;
        if (!r.title.empty())
            f.title = r.title;
        if (!r.comment.empty())
            f.comment = r.comment;
        if (!r.verdict.empty())
            f.verdict = r.verdict;
        if (!r.by.empty())
            f.by = r.by;
        f.evidence.insert(f.evidence.end(), r.evidence.begin(), r.evidence.end());
    }
    return all;
}

void L3Sessions::Resolve(const std::string& emulatorId, const std::string& key, const Resolution& resolution)
{
    std::lock_guard<std::mutex> lock(_mutex);
    Resolution& r = _states[emulatorId].resolutions[key];
    r.start = resolution.start;
    if (resolution.statusSet)
    {
        r.statusSet = true;
        r.status = resolution.status;
    }
    if (!resolution.label.empty())
        r.label = resolution.label;
    if (!resolution.title.empty())
        r.title = resolution.title;
    if (!resolution.comment.empty())
        r.comment = resolution.comment;
    if (!resolution.verdict.empty())
        r.verdict = resolution.verdict;
    if (!resolution.by.empty())
        r.by = resolution.by;
    if (!resolution.kind.empty())
    {
        r.kind = resolution.kind;
        r.kindEnd = resolution.kindEnd;
    }
    if (!resolution.verdict.empty())
        r.entry = resolution.entry;
    r.evidence.insert(r.evidence.end(), resolution.evidence.begin(), resolution.evidence.end());
}

void L3Sessions::Add(const std::string& emulatorId, Finding finding)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _states[emulatorId].added.push_back(std::move(finding));
}

void L3Sessions::DropAdded(const std::string& emulatorId, FindingKind kind)
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Finding>& added = _states[emulatorId].added;
    added.erase(std::remove_if(added.begin(), added.end(), [kind](const Finding& f) { return f.kind == kind; }),
                added.end());
}

void L3Sessions::SetCoverageNotes(const std::string& emulatorId, std::map<uint16_t, ExportNote> notes)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _states[emulatorId].coverageNotes = std::move(notes);
}

void L3Sessions::MarkShown(const std::string& emulatorId, const std::string& key)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _states[emulatorId].shown.insert(key);
}

bool L3Sessions::Shown(const std::string& emulatorId, const std::string& key) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _states.find(emulatorId);
    return it != _states.end() && it->second.shown.count(key) != 0;
}

std::map<uint16_t, ExportNote> L3Sessions::Notes(const std::string& emulatorId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::map<uint16_t, ExportNote> notes;
    const auto it = _states.find(emulatorId);
    if (it == _states.end())
        return notes;
    for (const auto& [key, r] : it->second.resolutions)
    {
        (void)key;
        if (r.label.empty() && r.title.empty() && r.comment.empty() && r.kind.empty() && !r.entry)
            continue;
        ExportNote& note = notes[r.start];
        if (!r.label.empty())
            note.label = r.label;
        if (!r.title.empty())
            note.title = r.title;
        if (!r.comment.empty())
            note.comments.push_back(r.comment);
        if (!r.kind.empty())
        {
            note.kind = r.kind;
            note.kindEnd = r.kindEnd;
        }
        note.entry = note.entry || r.entry;
    }
    for (const auto& [address, name] : it->second.names)
    {
        ExportNote& note = notes[address];
        if (note.label.empty())
            note.label = name.label;
        if (note.title.empty())
            note.title = name.title;
    }
    for (const auto& [address, note] : it->second.coverageNotes)
    {
        ExportNote& target = notes[address];
        target.comments.insert(target.comments.end(), note.comments.begin(), note.comments.end());
    }
    return notes;
}

NamingResult L3Sessions::Naming(const std::string& emulatorId, uint64_t job, const L1Result& l1, const L2Result& l2)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const State& state = _states[emulatorId];
        if (state.namingMade && state.namingJob == job)
            return state.naming;
    }
    // The base plan: the draft without the review's notes (the same batches however the review went)
    const DraftPlan plan = MakePlan(l1, l2, ExportOptions{});
    NamingResult result = NamingBatches(l1, l2, plan);
    std::lock_guard<std::mutex> lock(_mutex);
    State& state = _states[emulatorId];
    state.naming = result;
    state.namingJob = job;
    state.namingMade = true;
    return result;
}

void L3Sessions::Name(const std::string& emulatorId, const std::vector<GivenName>& names,
                      const std::vector<uint16_t>& skipped)
{
    std::lock_guard<std::mutex> lock(_mutex);
    State& state = _states[emulatorId];
    for (const GivenName& n : names)
    {
        state.names[n.z80] = n;
        state.skippedNames.erase(n.z80);
    }
    for (const uint16_t z80 : skipped)
        if (state.names.count(z80) == 0)
            state.skippedNames.insert(z80);
}

std::map<uint16_t, GivenName> L3Sessions::Names(const std::string& emulatorId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _states.find(emulatorId);
    return it != _states.end() ? it->second.names : std::map<uint16_t, GivenName>();
}

bool L3Sessions::Named(const std::string& emulatorId, const NamingBatch& batch) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _states.find(emulatorId);
    if (it == _states.end())
        return false;
    return std::all_of(batch.subjects.begin(), batch.subjects.end(), [&](const NamingSubject& s) {
        return it->second.names.count(s.z80) != 0 || it->second.skippedNames.count(s.z80) != 0;
    });
}

void L3Sessions::Forget(const std::string& emulatorId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _states.erase(emulatorId);
}

Reply ExecuteReview(Emulator& emulator, const std::string& action, const Options& options)
{
    if (action == "findings")
        return FindingsAction(emulator, options);
    if (action == "review")
        return ReviewAction(emulator, options);
    if (action == "experiment")
        return ExperimentAction(emulator, options);
    return CoverAction(emulator, options);
}

}  // namespace analysis
