#include "synccontrol.h"

#include <algorithm>
#include <fstream>

#include "common/base64.h"
#include "common/filehelper.h"
#include "debugger/asm/diskfiles.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "unrealasm/sync.h"

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

const char* FamilyName(sync::LayoutFamily family)
{
    switch (family)
    {
        case sync::LayoutFamily::FileImage: return "file-image";
        case sync::LayoutFamily::GapBuffer: return "gap-buffer";
        case sync::LayoutFamily::Linear: return "linear";
    }
    return "";
}

StateNode DiagnosticsValue(const Diagnostics& diagnostics)
{
    StateNode list = StateNode::Array();
    for (const Diagnostic& d : diagnostics)
    {
        StateNode item = StateNode::Object();
        item["severity"] = d.severity == Severity::Error ? "error" : d.severity == Severity::Warning ? "warning" : "info";
        item["line"] = static_cast<unsigned>(d.line);
        item["message"] = d.message;
        list.items.push_back(std::move(item));
    }
    return list;
}

/// Every RAM page and the window map, copied at one coherent moment
struct MachineCopy
{
    std::vector<std::vector<uint8_t>> pages;
    sync::MachineView view;
};

bool CopyMachine(EmulatorContext* context, MachineCopy& out, std::string& error)
{
    if (!context || !context->pMemory)
    {
        error = "the synchronizer needs a machine (no memory)";
        return false;
    }
    const uint32_t count = std::min<uint32_t>(context->config.ramsize / 16, MAX_RAM_PAGES);
    if (count == 0)
    {
        error = "the machine reports no RAM pages";
        return false;
    }
    out.pages.assign(count, std::vector<uint8_t>(PAGE_SIZE));
    Memory* memory = context->pMemory;
    const auto copy = [&]() {
        for (uint32_t i = 0; i < count; i++)
            if (const uint8_t* page = memory->RAMPageAddress(static_cast<uint16_t>(i)))
                std::copy(page, page + PAGE_SIZE, out.pages[i].begin());
        for (uint8_t w = 0; w < 4; w++)
            out.view.windows[w] = memory->GetMemoryBankMode(w) == BANK_RAM ? static_cast<int>(memory->GetRAMPageForBank(w)) : -1;
    };
    if (context->pEmulator)
    {
        if (context->pEmulator->RunAtCoherentMoment(copy, 2000) == Emulator::CoherentMoment::Busy)
        {
            error = "no coherent moment to copy the RAM within 2 s (another client is stepping the machine)";
            return false;
        }
    }
    else
        copy();
    out.view.ramPages = static_cast<int>(count);
    for (uint32_t i = 0; i < count; i++)
        out.view.ram.push_back({static_cast<uint16_t>(i), std::span<const uint8_t>(out.pages[i])});
    return true;
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
bool Choose(const sync::MachineView& machine, const AsmRequest& request, const sync::SyncDescriptor*& out, AsmReply& failure)
{
    const std::string wanted = Option(request, "assembler");
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

void Describe(const sync::SyncDescriptor& d, const sync::SyncText& text, StateNode& body)
{
    body["assembler"] = d.id;
    body["title"] = d.title;
    body["format"] = d.codec;
    body["version"] = d.version;
    body["family"] = FamilyName(d.family);
    body["state"] = text.state;
    body["name"] = text.name;
    body["page"] = text.page;
    body["bytes"] = static_cast<uint64_t>(text.file.size());
    body["lines"] = static_cast<uint64_t>(text.lines);
    body["current_line"] = static_cast<int64_t>(text.currentLine);
    body["editor"] = text.editor;
    body["typing"] = text.typing;
    body["changed"] = text.changed;
    body["diagnostics"] = DiagnosticsValue(text.diagnostics);
}

bool WriteBytes(EmulatorContext* context, const std::string& output, char type, const std::vector<uint8_t>& bytes, AsmReply& failure)
{
    std::string error;
    if (DiskFileRef ref; ParseDiskFileRef(output, ref))
    {
        if (ref.type != type)
            error = "the assembler saves type " + std::string(1, type) + ", not " + std::string(1, ref.type);
        else if (WriteDiskFile(context, ref, 0, bytes, error))
            return true;
        const bool refused = error.find("write-protected") != std::string::npos || error.find("free sectors") != std::string::npos ||
                             error.find("catalog is full") != std::string::npos;
        failure = Fail(refused ? AsmControlError::Refused : AsmControlError::BadRequest, error);
        return false;
    }
    std::ofstream file(FileHelper::ToFsPath(output), std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file.good())
    {
        failure = Fail(AsmControlError::NotFound, "cannot write " + output);
        return false;
    }
    return true;
}
}  // namespace

SyncControl::SyncControl(EmulatorContext* context) : _context(context) {}

const std::vector<std::string>& SyncControl::OptionsFor(const std::string& verb)
{
    static const std::map<std::string, std::vector<std::string>> options = {
        {"status", {"assembler"}},
        {"probe", {}},
        {"extract", {"assembler", "as", "to", "codepage", "output"}},
    };
    static const std::vector<std::string> none;
    const auto it = options.find(verb);
    return it != options.end() ? it->second : none;
}

AsmReply SyncControl::Execute(const std::string& verb, const AsmRequest& request)
{
    MachineCopy machine;
    std::string error;
    if (!CopyMachine(_context, machine, error))
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
    if (!Choose(machine.view, request, descriptor, failure))
        return failure;
    const sync::SyncText text = sync::ReadText(machine.view, *descriptor);
    StateNode body = StateNode::Object();
    Describe(*descriptor, text, body);
    if (verb == "status")
    {
        AsmReply reply;
        reply.body = std::move(body);
        return reply;
    }

    // extract
    if (!text.ok)
        return Fail(AsmControlError::Failed, "the text of " + descriptor->title + " does not read now (" + text.state + ")", std::move(body));
    std::string as = Option(request, "as");
    if (as.empty())
        as = "text";
    if (as != "text" && as != "file" && as != "dialect")
        return Fail(AsmControlError::BadRequest, "'as' is text, file or dialect: '" + as + "'");
    const std::string output = Option(request, "output");
    if (as == "file")
    {
        if (!output.empty())
        {
            if (!WriteBytes(_context, output, descriptor->extension.empty() ? 'C' : descriptor->extension[0], text.file, failure))
                return failure;
            body["output"] = output;
        }
        else
            body["data"] = base64::Encode(text.file);
        AsmReply reply;
        reply.body = std::move(body);
        return reply;
    }
    // Text or another dialect: the live file through the source verbs, read with the descriptor's codec and version
    AsmRequest inner;
    inner.verb = as == "text" ? "decode" : "convert";
    inner.options["data"] = base64::Encode(text.file);
    inner.options["name"] = (text.name.empty() ? std::string("live") : text.name) + "." + descriptor->extension;
    inner.options["codec"] = descriptor->codec;
    inner.options["version"] = descriptor->version;
    if (as == "dialect")
    {
        if (Option(request, "to").empty())
            return Fail(AsmControlError::BadRequest, "'as: dialect' needs 'to' (a dialect; 'dialects' lists them)");
        inner.options["to"] = Option(request, "to");
    }
    else if (!Option(request, "codepage").empty())
        inner.options["codepage"] = Option(request, "codepage");
    if (!output.empty())
        inner.options["output"] = output;
    AsmReply converted = AsmControl(_context).Execute(inner);
    for (const auto& [key, value] : body.members)
        if (!converted.body.find(key))   // the source verb's own fields (lines, diagnostics) stay
            converted.body[key] = value;
    return converted;
}
