#include "synccontrol.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "debugger/asm/sync/asmsyncservice.h"
#include "debugger/asm/sync/syncshared.h"
#include "debugger/debugmanager.h"
#include "emulator/emulatorcontext.h"
#include "unrealasm/dialect.h"
#include "unrealasm/registry.h"
#include "unrealasm/snapshot.h"
#include "unrealasm/sync/reader.h"

namespace
{
using namespace unrealasm;

AsmReply Fail(AsmControlError error, std::string message, StateNode body = StateNode::Object())
{
    AsmReply reply;
    reply.error = error;
    reply.message = std::move(message);
    reply.body = std::move(body);
    return reply;
}

std::string Option(const AsmRequest& request, const std::string& name)
{
    const auto it = request.options.find(name);
    return it != request.options.end() ? it->second : std::string();
}

StateNode CandidateValue(const sync::ProbeCandidate& c)
{
    StateNode item = StateNode::Object();
    item["assembler"] = c.descriptor->id;
    item["title"] = c.descriptor->title;
    item["score"] = c.score;
    item["reason"] = c.reason;
    return item;
}

/// The descriptor a request names, or the probe's single best; false with the failure reply (the candidates in it)
bool Choose(const sync::MachineView& machine, const std::string& wanted, const sync::SyncDescriptor*& out, AsmReply& failure)
{
    if (!wanted.empty())
    {
        out = sync::FindDescriptor(wanted);
        if (!out)
        {
            std::string known;
            for (const sync::SyncDescriptor& d : sync::Descriptors())
                known += (known.empty() ? "" : ", ") + d.id;
            failure = Fail(AsmControlError::NotFound, "no assembler '" + wanted + "' (known: " + known + ")");
            return false;
        }
        return true;
    }
    const std::vector<sync::ProbeCandidate> candidates = sync::Probe(machine);
    StateNode body = StateNode::Object();
    body["state"] = candidates.empty() ? "none" : "ambiguous";
    body["candidates"] = StateNode::Array();
    for (const sync::ProbeCandidate& c : candidates)
        body["candidates"].items.push_back(CandidateValue(c));
    if (candidates.empty())
    {
        failure = Fail(AsmControlError::NotFound, "no known assembler in RAM ('sync-probe' lists what identifies)", std::move(body));
        return false;
    }
    if (candidates.size() > 1 && candidates[1].score == candidates[0].score)
    {
        failure = Fail(AsmControlError::BadRequest, "two assemblers identify alike: pick one with 'assembler'", std::move(body));
        return false;
    }
    out = candidates[0].descriptor;
    return true;
}

bool ParseMs(const AsmRequest& request, const std::string& name, uint32_t least, uint32_t& out, AsmReply& failure)
{
    if (!request.options.count(name))
        return true;
    const std::string value = Option(request, name);
    char* end = nullptr;
    const long long ms = std::strtoll(value.c_str(), &end, 10);
    if (!end || *end || ms < least || ms > 60000)
    {
        failure = Fail(AsmControlError::BadRequest, "'" + name + "' is milliseconds, " + std::to_string(least) + "-60000: '" + value + "'");
        return false;
    }
    out = static_cast<uint32_t>(ms);
    return true;
}

AsmSyncService* Service(EmulatorContext* context)
{
    return context && context->pDebugManager ? context->pDebugManager->GetAsmSyncService() : nullptr;
}
}  // namespace

SyncControl::SyncControl(EmulatorContext* context) : _context(context) {}

AsmReply SyncControl::PushInject(const AsmRequest& request, std::vector<std::vector<uint8_t>>& written)
{
    asmsync::MachineCopy machine;
    std::string error;
    if (!asmsync::CopyMachine(_context, machine, error))
        return Fail(AsmControlError::BadRequest, error);
    const sync::SyncDescriptor* descriptor = nullptr;
    AsmReply failure;
    if (!Choose(machine.view, Option(request, "assembler"), descriptor, failure))
        return failure;
    return Inject(*descriptor, sync::ReadText(machine.view, *descriptor), request, nullptr, &written);
}

AsmReply SyncControl::Inject(const sync::SyncDescriptor& descriptor, const sync::SyncText& current, const AsmRequest& request,
                             const containers::Snapshot* snapshot, std::vector<std::vector<uint8_t>>* written)
{
    // The source, written as the assembler's own file (converted by its backend unless it is in its dialect already)
    SourceDocument document;
    Diagnostics diagnostics;
    std::string source;
    AsmReply failure;
    if (!AsmControl(_context).ReadDocument(request, document, diagnostics, source, failure))
        return failure;
    if (document.dialect.empty())
        document.dialect = "sjasmplus";   // a plain text file without 'from' (as 'text' and zxasm inject take it)
    StateNode body = StateNode::Object();
    body["assembler"] = descriptor.id;
    body["title"] = descriptor.title;
    body["source"] = source;
    body["from"] = document.dialect;
    // The name in the text's header: given, else the text's in memory, else the source's
    std::string title = Option(request, "title");
    if (title.empty())
        title = current.ok && !current.name.empty() ? current.name : document.name;
    if (title.empty())
        title = "TEXT";
    // A source file on the host brings the sources it INCLUDEs: each into a text of its own (PlanInjectProject)
    std::vector<ProjectFile> project = {{document.name, document}};
    const std::string path = Option(request, "path");
    std::error_code ec;
    const std::filesystem::path hostPath(std::u8string(path.begin(), path.end()));
    if (!path.empty() && !request.options.count("file") && std::filesystem::is_regular_file(hostPath, ec))
        project = HostProject(document, [folder = hostPath.parent_path()](const std::string& name) {
            const std::u8string relative(name.begin(), name.end());
            std::ifstream in(folder / std::filesystem::path(relative), std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }, diagnostics);
    sync::InjectOptions options;
    std::string force = Option(request, "force");
    std::transform(force.begin(), force.end(), force.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    options.force = request.options.count("force") && (force.empty() || force == "true" || force == "1" || force == "on");
    if (written)
        options.written = *written;
    // the drive whose disk takes the sources an assembler INCLUDEs from its disk (XAS)
    uint8_t drive = 0;
    if (const std::string letter = Option(request, "drive"); !letter.empty())
    {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(letter[0])));
        if (letter.size() != 1 || upper < 'A' || upper > 'D')
            return Fail(AsmControlError::BadRequest, "'drive' is A, B, C or D", std::move(body));
        drive = static_cast<uint8_t>(upper - 'A');
    }
    sync::ProjectInjectPlan plan;
    std::string error;
    if (snapshot)
    {
        // Into a snapshot file: the same plan on its memory, the file written with those pages replaced
        const std::string output = Option(request, "output");
        if (output.empty())
            return Fail(AsmControlError::BadRequest, "an inject into a snapshot needs 'output' (the snapshot file to write)", std::move(body));
        std::vector<uint8_t> file;
        plan = sync::InjectProjectIntoSnapshot(*snapshot, descriptor, project, title, options, file);
        if (plan.ok && !plan.diskFiles.empty())
            return Fail(AsmControlError::Refused,
                        descriptor.title + " reads the " + std::to_string(plan.diskFiles.size()) +
                            " source(s) the main one INCLUDEs from its disk, which a snapshot does not hold: inject into the running "
                            "machine (its disk takes them) or use zxasm inject --disk",
                        std::move(body));
        if (plan.ok)
        {
            std::ofstream out(output, std::ios::binary);
            out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
            if (!out)
                return Fail(AsmControlError::Failed, "cannot write " + output, std::move(body));
            body["output"] = output;
            body["format"] = containers::SnapshotFormatName(snapshot->format);
        }
    }
    else
        asmsync::InjectProject(_context, descriptor, project, title, options, plan, error, drive);
    diagnostics.insert(diagnostics.end(), plan.diagnostics.begin(), plan.diagnostics.end());
    body["diagnostics"] = asmsync::DiagnosticsValue(diagnostics);
    if (!plan.ok)
    {
        // Not written (no codec or backend, the conversion failed, too many sources), else refused by the machine
        if (plan.error.empty())
            return Fail(AsmControlError::Refused, error, std::move(body));
        if (plan.texts.empty())
            return Fail(plan.error.rfind("no ", 0) == 0 ? AsmControlError::NotFound : AsmControlError::Failed, plan.error, std::move(body));
        return Fail(AsmControlError::Refused, plan.error, std::move(body));
    }
    if (written)
    {
        written->clear();
        for (const WriteAsResult& text : plan.texts)
            written->push_back(text.bytes);
    }
    const auto previousValue = [&](const sync::SyncText& previous) {
        StateNode value = StateNode::Object();
        value["name"] = previous.name;
        value["bytes"] = static_cast<uint64_t>(previous.file.size());
        value["changed"] = previous.changed;
        return value;
    };
    body["name"] = plan.texts[0].document.name;
    body["page"] = static_cast<int64_t>(plan.pages[0].page);
    body["bytes"] = static_cast<uint64_t>(plan.texts[0].bytes.size());
    body["lines"] = static_cast<uint64_t>(plan.texts[0].document.lines.size());
    if (plan.pages[0].previous.ok)
    {
        StateNode previous = previousValue(plan.pages[0].previous);
        const std::string backup = Option(request, "backup");
        if (!backup.empty())
        {
            std::ofstream out(backup, std::ios::binary);
            const std::vector<uint8_t>& old = plan.pages[0].previous.file;
            out.write(reinterpret_cast<const char*>(old.data()), static_cast<std::streamsize>(old.size()));
            previous["backup"] = out ? backup : std::string();
        }
        body["previous"] = std::move(previous);
    }
    if (plan.pages.size() > 1)
    {
        StateNode includes = StateNode::Array();
        for (size_t k = 1; k < plan.pages.size(); ++k)
        {
            StateNode item = StateNode::Object();
            item["source"] = project[k].name;
            item["name"] = plan.texts[k].document.name;
            item["page"] = static_cast<int64_t>(plan.pages[k].page);
            item["bytes"] = static_cast<uint64_t>(plan.texts[k].bytes.size());
            if (plan.pages[k].previous.ok)
                item["previous"] = previousValue(plan.pages[k].previous);
            includes.items.push_back(std::move(item));
        }
        body["includes"] = std::move(includes);
    }
    if (!plan.diskFiles.empty())
    {
        // onto the disk the assembler reads them from
        StateNode includes = StateNode::Array();
        for (size_t k = 0; k < plan.diskFiles.size(); ++k)
        {
            StateNode item = StateNode::Object();
            item["source"] = project[k + 1].name;
            item["name"] = plan.texts[k + 1].document.name;
            item["disk"] = std::string("disk:") + static_cast<char>('A' + drive) + "/" + plan.diskFiles[k].FullName();
            item["bytes"] = static_cast<uint64_t>(plan.diskFiles[k].data.size());
            includes.items.push_back(std::move(item));
        }
        body["includes"] = std::move(includes);
    }
    AsmReply reply;
    reply.body = std::move(body);
    return reply;
}

const std::vector<std::string>& SyncControl::OptionsFor(const std::string& verb)
{
    static const std::map<std::string, std::vector<std::string>> options = {
        {"status", {"assembler", "snapshot"}},
        {"probe", {"snapshot"}},
        {"extract", {"assembler", "as", "to", "codepage", "output", "snapshot"}},
        {"watch", {"assembler", "interval", "quiet", "as", "to", "output", "project"}},
        {"unwatch", {}},
        {"hints", {}},
        {"inject", {"assembler", "path", "data", "name", "file", "codec", "version", "from", "text", "title", "force", "backup", "snapshot", "output", "drive"}},
        {"push-watch", {"path", "from", "assembler", "title", "force", "interval", "quiet", "drive"}},
        {"push-unwatch", {}},
    };
    static const std::vector<std::string> none;
    const auto it = options.find(verb);
    return it != options.end() ? it->second : none;
}

AsmReply SyncControl::Execute(const std::string& verb, const AsmRequest& request)
{
    if (verb == "push-watch" || verb == "push-unwatch")
    {
        AsmSyncService* service = Service(_context);
        if (!service)
            return Fail(AsmControlError::BadRequest, "the push watch needs an emulator instance with a debugger");
        AsmReply reply;
        if (verb == "push-unwatch")
        {
            service->StopPush();
            reply.body = service->PushStatusValue();
            return reply;
        }
        AsmSyncService::PushOptions options;
        options.path = Option(request, "path");
        std::error_code ec;
        if (options.path.empty())
            return Fail(AsmControlError::BadRequest, "'push-watch' needs 'path' (the host source to watch)");
        if (!std::filesystem::is_regular_file(std::filesystem::path(std::u8string(options.path.begin(), options.path.end())), ec))
            return Fail(AsmControlError::NotFound, "no file " + options.path);
        if (!Option(request, "from").empty())
            options.from = Option(request, "from");
        options.assembler = Option(request, "assembler");
        if (!options.assembler.empty() && !sync::FindDescriptor(options.assembler))
            return Fail(AsmControlError::NotFound, "no assembler '" + options.assembler + "'");
        options.title = Option(request, "title");
        options.drive = Option(request, "drive");
        std::string force = Option(request, "force");
        std::transform(force.begin(), force.end(), force.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        options.force = request.options.count("force") && (force.empty() || force == "true" || force == "1" || force == "on");
        AsmReply failure;
        if (!ParseMs(request, "interval", 50, options.intervalMs, failure) || !ParseMs(request, "quiet", 0, options.quietMs, failure))
            return failure;
        service->StartPush(options);
        reply.body = service->PushStatusValue();
        return reply;
    }

    if (verb == "watch" || verb == "unwatch" || verb == "hints")
    {
        AsmSyncService* service = Service(_context);
        if (!service)
            return Fail(AsmControlError::BadRequest, "the watch needs an emulator instance with a debugger");
        AsmReply reply;
        if (verb == "hints")
        {
            reply.body = service->HintsValue();
            if (!reply.body.find("generation"))
                return Fail(AsmControlError::NotFound, "no build yet ('sync-watch' starts the watch)", service->StatusValue());
            return reply;
        }
        if (verb == "unwatch")
        {
            service->Stop();
            reply.body = service->StatusValue();
            return reply;
        }
        AsmSyncService::WatchOptions options;
        options.assembler = Option(request, "assembler");
        if (!options.assembler.empty() && !sync::FindDescriptor(options.assembler))
            return Fail(AsmControlError::NotFound, "no assembler '" + options.assembler + "'");
        AsmReply failure;
        if (!ParseMs(request, "interval", 50, options.intervalMs, failure) || !ParseMs(request, "quiet", 0, options.quietMs, failure))
            return failure;
        options.output = Option(request, "output");
        if (request.options.count("project"))
        {
            // project: disk:A..disk:D, none, or a host folder; comma-separated (the disk in drive A by default)
            options.projectDrive = -1;
            std::string list = Option(request, "project");
            size_t from = 0;
            while (from <= list.size())
            {
                size_t comma = list.find(',', from);
                if (comma == std::string::npos)
                    comma = list.size();
                const std::string item = list.substr(from, comma - from);
                if (item.size() == 6 && item.compare(0, 5, "disk:") == 0 && std::toupper(static_cast<unsigned char>(item[5])) >= 'A' &&
                    std::toupper(static_cast<unsigned char>(item[5])) <= 'D')
                    options.projectDrive = std::toupper(static_cast<unsigned char>(item[5])) - 'A';
                else if (item == "none" || item.empty())
                    ;
                else if (std::filesystem::is_directory(item))
                    options.projectDir = item;
                else
                    return Fail(AsmControlError::BadRequest, "'project' takes disk:A-D, none or a folder: '" + item + "'");
                from = comma + 1;
            }
        }
        if (request.options.count("as"))
            options.as = Option(request, "as");
        options.to = Option(request, "to");
        if (options.as != "text" && options.as != "file" && options.as != "dialect")
            return Fail(AsmControlError::BadRequest, "'as' is text, file or dialect: '" + options.as + "'");
        if (options.as == "dialect" && options.to.empty())
            return Fail(AsmControlError::BadRequest, "'as: dialect' needs 'to' (a dialect; 'dialects' lists them)");
        service->Start(options);
        reply.body = service->StatusValue();
        return reply;
    }

    // The memory: a copy of the running machine's, or a snapshot file's (option `snapshot`, Z1)
    asmsync::MachineCopy machine;
    containers::Snapshot snapshot;
    const bool fromSnapshot = request.options.count("snapshot") > 0;
    std::string error;
    if (fromSnapshot)
    {
        const std::string path = Option(request, "snapshot");
        std::ifstream in(path, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t dot = path.find_last_of('.');
        if (!in && bytes.empty())
            return Fail(AsmControlError::NotFound, "cannot read " + path);
        if (!containers::ReadSnapshot(bytes, dot == std::string::npos ? std::string() : path.substr(dot + 1), snapshot, error))
            return Fail(AsmControlError::BadRequest, path + ": " + error);
        machine.view = sync::ViewOf(snapshot);
    }
    else if (!asmsync::CopyMachine(_context, machine, error))
        return Fail(AsmControlError::BadRequest, error);

    if (verb == "probe")
    {
        AsmReply reply;
        reply.body["candidates"] = StateNode::Array();
        for (const sync::ProbeCandidate& c : sync::Probe(machine.view))
            reply.body["candidates"].items.push_back(CandidateValue(c));
        reply.body["descriptors"] = static_cast<uint64_t>(sync::Descriptors().size());
        return reply;
    }

    const sync::SyncDescriptor* descriptor = nullptr;
    AsmReply failure;
    if (!Choose(machine.view, Option(request, "assembler"), descriptor, failure))
        return failure;
    if (verb == "inject")
        return Inject(*descriptor, sync::ReadText(machine.view, *descriptor), request, fromSnapshot ? &snapshot : nullptr);
    const sync::SyncText text = sync::ReadText(machine.view, *descriptor);
    StateNode body = StateNode::Object();
    asmsync::Describe(*descriptor, text, body);
    if (fromSnapshot)
        body["snapshot"] = Option(request, "snapshot");
    if (verb == "status")
    {
        if (AsmSyncService* service = fromSnapshot ? nullptr : Service(_context))
        {
            body["watch"] = service->StatusValue();
            body["push"] = service->PushStatusValue();
        }
        AsmReply reply;
        reply.body = std::move(body);
        return reply;
    }

    // extract
    if (!text.ok)
        return Fail(AsmControlError::Failed, "the text of " + descriptor->title + " does not read now (" + text.state + ")", std::move(body));
    const std::string as = Option(request, "as").empty() ? std::string("text") : Option(request, "as");
    AsmReply rendered = asmsync::Render(_context, *descriptor, text.file, text.name, as, Option(request, "to"), Option(request, "codepage"),
                                        Option(request, "output"));
    for (const auto& [key, value] : body.members)
        if (!rendered.body.find(key))   // the source verb's own fields (lines, diagnostics) stay
            rendered.body[key] = value;
    return rendered;
}
