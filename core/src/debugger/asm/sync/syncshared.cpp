#include "syncshared.h"

#include <algorithm>
#include <fstream>

#include "common/base64.h"
#include "common/filehelper.h"
#include "debugger/asm/diskfiles.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace asmsync
{
using namespace unrealasm;

namespace
{
AsmReply Fail(AsmControlError error, std::string message, StateNode body = StateNode::Object())
{
    AsmReply reply;
    reply.error = error;
    reply.message = std::move(message);
    reply.body = std::move(body);
    return reply;
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

bool CopyMachine(EmulatorContext* context, MachineCopy& out, std::string& error, const std::vector<int>* only)
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
    Memory* memory = context->pMemory;
    const auto copy = [&]() {
        for (uint8_t w = 0; w < 4; w++)
            out.view.windows[w] = memory->GetMemoryBankMode(w) == BANK_RAM ? static_cast<int>(memory->GetRAMPageForBank(w)) : -1;
        std::vector<int> wanted;
        if (only)
        {
            wanted = *only;
            wanted.insert(wanted.end(), out.view.windows.begin(), out.view.windows.end());
        }
        else
            for (uint32_t i = 0; i < count; i++)
                wanted.push_back(static_cast<int>(i));
        std::sort(wanted.begin(), wanted.end());
        wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
        for (const int page : wanted)
            if (page >= 0 && static_cast<uint32_t>(page) < count)
                if (const uint8_t* bytes = memory->RAMPageAddress(static_cast<uint16_t>(page)))
                    out.pages.emplace_back(static_cast<uint16_t>(page), std::vector<uint8_t>(bytes, bytes + PAGE_SIZE));
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
    for (const auto& [page, bytes] : out.pages)
        out.view.ram.push_back({page, std::span<const uint8_t>(bytes)});
    return true;
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

AsmReply Render(EmulatorContext* context, const sync::SyncDescriptor& descriptor, const std::vector<uint8_t>& file, const std::string& name,
                const std::string& as, const std::string& to, const std::string& codepage, const std::string& output)
{
    if (as != "text" && as != "file" && as != "dialect")
        return Fail(AsmControlError::BadRequest, "'as' is text, file or dialect: '" + as + "'");
    if (as == "file")
    {
        AsmReply reply;
        if (!output.empty())
        {
            if (!WriteBytes(context, output, descriptor.extension.empty() ? 'C' : descriptor.extension[0], file, reply))
                return reply;
            reply.body["output"] = output;
        }
        else
            reply.body["data"] = base64::Encode(file);
        return reply;
    }
    // Text or another dialect: the live file through the source verbs, read with the descriptor's codec and version
    AsmRequest inner;
    inner.verb = as == "text" ? "decode" : "convert";
    inner.options["data"] = base64::Encode(file);
    inner.options["name"] = (name.empty() ? std::string("live") : name) + "." + descriptor.extension;
    inner.options["codec"] = descriptor.codec;
    inner.options["version"] = descriptor.version;
    if (as == "dialect")
    {
        if (to.empty())
            return Fail(AsmControlError::BadRequest, "'as: dialect' needs 'to' (a dialect; 'dialects' lists them)");
        inner.options["to"] = to;
    }
    else if (!codepage.empty())
        inner.options["codepage"] = codepage;
    if (!output.empty())
        inner.options["output"] = output;
    return AsmControl(context).Execute(inner);
}
}  // namespace asmsync
