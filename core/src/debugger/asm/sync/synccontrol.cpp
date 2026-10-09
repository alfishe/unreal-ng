#include "synccontrol.h"

#include <cstdlib>

#include "debugger/asm/sync/asmsyncservice.h"
#include "debugger/asm/sync/syncshared.h"
#include "debugger/debugmanager.h"
#include "emulator/emulatorcontext.h"
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

const std::vector<std::string>& SyncControl::OptionsFor(const std::string& verb)
{
    static const std::map<std::string, std::vector<std::string>> options = {
        {"status", {"assembler"}},
        {"probe", {}},
        {"extract", {"assembler", "as", "to", "codepage", "output"}},
        {"watch", {"assembler", "interval", "quiet", "as", "to", "output"}},
        {"unwatch", {}},
        {"hints", {}},
    };
    static const std::vector<std::string> none;
    const auto it = options.find(verb);
    return it != options.end() ? it->second : none;
}

AsmReply SyncControl::Execute(const std::string& verb, const AsmRequest& request)
{
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

    asmsync::MachineCopy machine;
    std::string error;
    if (!asmsync::CopyMachine(_context, machine, error))
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
    const sync::SyncText text = sync::ReadText(machine.view, *descriptor);
    StateNode body = StateNode::Object();
    asmsync::Describe(*descriptor, text, body);
    if (verb == "status")
    {
        if (AsmSyncService* service = Service(_context))
            body["watch"] = service->StatusValue();
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
