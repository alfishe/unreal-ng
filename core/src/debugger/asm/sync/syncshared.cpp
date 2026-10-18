#include "syncshared.h"

#include <algorithm>
#include <fstream>

#include "common/base64.h"
#include "common/filehelper.h"
#include "debugger/asm/diskfiles.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
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
        if (!context)
        {
            failure = Fail(AsmControlError::BadRequest, "a disk file needs an emulator instance: " + output);
            return false;
        }
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

namespace
{
uint32_t RamPages(EmulatorContext* context)
{
    return std::min<uint32_t>(context->config.ramsize / 16, MAX_RAM_PAGES);
}

/// The copy itself, where nothing changes the machine meanwhile; the view is built after
void CopyNow(EmulatorContext* context, MachineCopy& out, const std::vector<int>* only)
{
    const uint32_t count = RamPages(context);
    Memory* memory = context->pMemory;
    {
        for (uint8_t w = 0; w < 4; w++)
            out.view.windows[w] = memory->GetMemoryBankMode(w) == BANK_RAM ? static_cast<int>(memory->GetRAMPageForBank(w)) : -1;
        std::vector<int> wanted;
        if (only)
            wanted = *only;
        else
            for (uint32_t i = 0; i < count; i++)
                wanted.push_back(static_cast<int>(i));
        wanted.insert(wanted.end(), out.view.windows.begin(), out.view.windows.end());   // a 48K machine maps 5, 2, 0 of 3 pages
        std::sort(wanted.begin(), wanted.end());
        wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
        for (const int page : wanted)
        {
            // a page mapped in a window is copied even beyond the model's count
            const bool mapped = std::find(out.view.windows.begin(), out.view.windows.end(), page) != out.view.windows.end();
            if (page >= 0 && (static_cast<uint32_t>(page) < count || (mapped && page < MAX_RAM_PAGES)))
                if (const uint8_t* bytes = memory->RAMPageAddress(static_cast<uint16_t>(page)))
                    out.pages.emplace_back(static_cast<uint16_t>(page), std::vector<uint8_t>(bytes, bytes + PAGE_SIZE));
        }
        if (Z80* z80 = context->pCore ? context->pCore->GetZ80() : nullptr)
            out.view.sp = z80->sp;
    }
    out.view.ramPages = static_cast<int>(count);
    out.view.ram.clear();
    for (const auto& [page, bytes] : out.pages)
        out.view.ram.push_back({page, std::span<const uint8_t>(bytes)});
}

bool CheckMachine(EmulatorContext* context, std::string& error)
{
    if (!context || !context->pMemory)
    {
        error = "the synchronizer needs a machine (no memory)";
        return false;
    }
    if (RamPages(context) == 0)
    {
        error = "the machine reports no RAM pages";
        return false;
    }
    return true;
}
}  // namespace

bool CopyMachine(EmulatorContext* context, MachineCopy& out, std::string& error, const std::vector<int>* only)
{
    if (!CheckMachine(context, error))
        return false;
    const auto copy = [&]() { CopyNow(context, out, only); };
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
    return true;
}

bool InjectProject(EmulatorContext* context, const sync::SyncDescriptor& descriptor, const std::vector<ProjectFile>& files,
                   const std::string& title, const sync::InjectOptions& options, sync::ProjectInjectPlan& plan, std::string& error,
                   uint8_t drive)
{
    if (!CheckMachine(context, error))
        return false;
    std::string refused;
    const auto work = [&]() {
        MachineCopy copy;
        CopyNow(context, copy, nullptr);
        plan = sync::PlanInjectProject(copy.view, descriptor, files, title, options);
        if (!plan.ok)
            return;
        // the INCLUDEd sources onto the disk first: a disk that takes none (no disk, write-protected) leaves the memory
        for (const containers::TrdosFile& file : plan.diskFiles)
            if (std::string diskError; !WriteDiskFileNow(context, drive, file, diskError))
            {
                plan.ok = false;
                plan.error = file.FullName() + ": " + diskError;
                return;
            }
        Memory* memory = context->pMemory;
        const auto write = [&]() {
            for (const sync::InjectPlan& page : plan.pages)
            {
                if (page.page >= 0)
                {
                    uint8_t* bytes = memory->RAMPageAddress(static_cast<uint16_t>(page.page));
                    std::copy(page.bytes.begin(), page.bytes.end(), bytes);
                    memory->MarkRamPageEdited(static_cast<uint16_t>(page.page));
                }
                // the editor variables the assembler's LOAD sets (XAS), or the whole text byte by byte (TASM 4.12)
                for (const sync::InjectPlan::Poke& poke : page.pokes)
                {
                    memory->RAMPageAddress(static_cast<uint16_t>(poke.page))[poke.offset] = poke.value;
                    memory->MarkRamPageEdited(static_cast<uint16_t>(poke.page));
                }
            }
        };
        if (context->pEmulator)
            refused = context->pEmulator->EditMemoryFromTool("asm sync inject", write);   // the recorded past
        else
            write();
    };
    if (context->pEmulator)
    {
        if (context->pEmulator->RunAtCoherentMoment(work, 2000) == Emulator::CoherentMoment::Busy)
        {
            error = "no coherent moment to write the RAM within 2 s (another client is stepping the machine)";
            return false;
        }
    }
    else
        work();
    if (!refused.empty())
    {
        error = refused;
        return false;
    }
    if (!plan.ok)
        error = plan.error;
    return plan.ok;
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
