#include "snapshotcapture.h"

#include <algorithm>

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "loaders/snapshot/loader_sna.h"
#include "loaders/snapshot/loader_z80.h"
#include "loaders/snapshot/szx/loaderszx.h"

namespace snapshot
{
const char* ToText(SaveFormat format)
{
    switch (format)
    {
        case SaveFormat::Sna: return "sna";
        case SaveFormat::Z80: return "z80";
        case SaveFormat::Szx: return "szx";
    }
    return "?";
}

std::optional<SaveFormat> SaveFormatFromExtension(const std::string& extension)
{
    const std::string ext = StringHelper::ToLower(extension);
    if (ext == "sna")
        return SaveFormat::Sna;
    if (ext == "z80")
        return SaveFormat::Z80;
    if (ext == "szx")
        return SaveFormat::Szx;
    return std::nullopt;
}

namespace
{
std::string Hex(unsigned value, int digits = 4) { return StringHelper::Format("#%0*X", digits, value); }

/// What the snapshot formats call the machine, for messages
std::string MachineName(const MachineView& view)
{
    const std::string& h = view.machineHint;
    if (h == "48k") return "ZX Spectrum 48K";
    if (h == "128k") return "ZX Spectrum 128K";
    if (h == "plus2") return "ZX Spectrum +2";
    if (h == "plus2a") return "ZX Spectrum +2A";
    if (h == "plus3") return "ZX Spectrum +3";
    if (h == "pentagon128") return "Pentagon 128";
    if (h == "pentagon512") return "Pentagon 512";
    if (h == "pentagon1024") return "Pentagon 1024";
    if (h == "scorpion256") return "Scorpion ZS 256";
    return h;
}

MachineView Unavailable(std::string reason, std::string needs)
{
    MachineView view;
    view.available = false;
    view.reason = std::move(reason);
    view.needs = std::move(needs);
    return view;
}

void AddBanks(MachineView& view, Memory& memory, const std::vector<uint16_t>& banks)
{
    for (uint16_t bank : banks)
    {
        const uint8_t* bytes = memory.RAMPageAddress(bank);
        if (bytes)
            view.banks[bank] = bytes;
    }
}

/// The window map of a Spectrum 128K: window 0 ROM, windows 1 and 2 RAM 5 and 2, window 3 the page #7FFD names.
/// Empty = it is; else what differs
std::string WindowMapDiffers(Memory& memory, uint8_t p7ffd)
{
    std::string diffs;
    auto add = [&](const std::string& text) { diffs += (diffs.empty() ? "" : ", ") + text; };
    if (!memory.IsBank0ROM())
        add("window 0 (#0000) is RAM, not ROM");
    const uint16_t expected[3] = {5, 2, static_cast<uint16_t>(p7ffd & 0x07)};
    for (uint8_t window = 1; window < 4; window++)
    {
        const uint16_t page = memory.GetRAMPageForBank(window);
        if (page != expected[window - 1])
        {
            add("window " + std::to_string(window) + " shows " +
                (page == MEMORY_UNMAPPABLE ? std::string("ROM") : "RAM page " + std::to_string(page)) + ", a Spectrum 128K shows RAM page " +
                std::to_string(expected[window - 1]));
        }
    }
    return diffs;
}

MachineView DefaultView(EmulatorContext& context)
{
    if (!context.pMemory)
        return Unavailable("there is no machine to save", "machine");
    Memory& memory = *context.pMemory;
    const CONFIG& config = context.config;
    const EmulatorState& state = context.emulatorState;

    MachineView view;
    view.model = config.mem_model;
    view.ramKb = config.ramsize;
    view.p7FFD = state.p7FFD;

    switch (config.mem_model)
    {
        case MM_SPECTRUM48:
            view.machineHint = "48k";
            view.timingHint = "48k";
            view.layout48 = true;
            AddBanks(view, memory, {5, 2, 0});
            break;
        case MM_SPECTRUM128:
            view.machineHint = "128k";
            view.timingHint = "128k";
            AddBanks(view, memory, {0, 1, 2, 3, 4, 5, 6, 7});
            break;
        case MM_PLUS2:
            view.machineHint = "plus2";
            view.timingHint = "128k";
            AddBanks(view, memory, {0, 1, 2, 3, 4, 5, 6, 7});
            break;
        case MM_PLUS2A:
        case MM_PLUS3:
            view.machineHint = config.mem_model == MM_PLUS3 ? "plus3" : "plus2a";
            view.timingHint = "128k";
            view.p1FFD = state.p1FFD;
            AddBanks(view, memory, {0, 1, 2, 3, 4, 5, 6, 7});
            break;
        case MM_PENTAGON:
        {
            const uint16_t pages = static_cast<uint16_t>(std::max<uint32_t>(8, config.ramsize / 16));
            view.machineHint = config.ramsize > 512 ? "pentagon1024" : (config.ramsize > 128 ? "pentagon512" : "pentagon128");
            view.timingHint = "pentagon";
            if (config.ramsize > 128)
                view.pEFF7 = state.pEFF7;
            for (uint16_t page = 0; page < pages; page++)
                AddBanks(view, memory, {page});
            break;
        }
        case MM_SCORP:
        case MM_PROFSCORP:
            view.machineHint = "scorpion256";
            view.timingHint = "128k";
            view.p1FFD = state.p1FFD;
            for (uint16_t page = 0; page < 16; page++)
                AddBanks(view, memory, {page});
            break;
        default:
            return Unavailable("this machine has no snapshot view yet (its memory is not the Spectrum 128K's, and no capture "
                               "rule exists for it)",
                               "capture_unsupported");
    }

    const size_t wanted = view.layout48 ? 3 : (view.machineHint == "scorpion256" ? 16 : (view.machineHint.rfind("pentagon", 0) == 0 && config.ramsize > 128 ? config.ramsize / 16 : 8));
    if (view.banks.size() < wanted)
        return Unavailable("the machine's RAM is not all there (" + std::to_string(view.banks.size()) + " of " + std::to_string(wanted) + " banks)",
                           "ram");
    return view;
}
}  // namespace

bool SavesAs48K(bool layout48, size_t bankCount, std::optional<uint8_t> p7ffd, std::optional<uint8_t> p1ffd)
{
    if (layout48)
        return true;
    return bankCount == 8 && p7ffd && (*p7ffd & 0x2F) == 0x20 && !(p1ffd && (*p1ffd & 0x01));
}

bool SavesAs48K(const Image& image)
{
    return SavesAs48K(image.memoryModel == MemoryModel::Mem48k, image.banks.size(), image.paging.p7FFD, image.paging.p1FFD);
}

WindowMapCapture& WindowMapCapture::Instance()
{
    static WindowMapCapture instance;
    return instance;
}

MachineView WindowMapCapture::Examine(EmulatorContext& context) const
{
    if (!context.pMemory)
        return Unavailable("there is no machine to save", "machine");
    Memory& memory = *context.pMemory;
    const std::string diffs = WindowMapDiffers(memory, context.emulatorState.p7FFD);
    if (!diffs.empty())
        return Unavailable("the memory is not laid out as a Spectrum 128K now (" + diffs +
                               "): the machine is in its own mode, and a snapshot holds the 128K view only. "
                               "Run a Spectrum 128K program first (a snapshot load puts the machine into that layout)",
                           "mode:128k");
    MachineView view;
    view.model = MM_SPECTRUM128;
    view.ramKb = 128;
    view.p7FFD = context.emulatorState.p7FFD;
    view.machineHint = "128k";
    view.timingHint = "128k";
    view.note = "the window map is a Spectrum 128K: RAM pages 0-7 are banks 0-7";
    AddBanks(view, memory, {0, 1, 2, 3, 4, 5, 6, 7});
    if (view.banks.size() < 8)
        return Unavailable("the machine's RAM is not all there (" + std::to_string(view.banks.size()) + " of 8 banks)", "ram");
    return view;
}

MachineView ExamineView(EmulatorContext& context)
{
    if (context.pPortDecoder)
    {
        if (ISnapshotCapturePolicy* policy = context.pPortDecoder->GetSnapshotCapturePolicy())
            return policy->Examine(context);
    }
    return DefaultView(context);
}

const FormatStatus& SaveFormats::For(SaveFormat format) const
{
    for (const FormatStatus& status : formats)
    {
        if (status.format == format)
            return status;
    }
    return formats.front();
}

StateNode SaveFormats::ToStateNode() const
{
    StateNode root = StateNode::Object();
    root["view_available"] = viewAvailable;
    root["machine"] = machine;
    root["view"] = view;
    StateNode list = StateNode::Array();
    for (const FormatStatus& status : formats)
    {
        StateNode item = StateNode::Object();
        item["format"] = ToText(status.format);
        item["available"] = status.available;
        if (!status.available)
        {
            item["reason"] = status.reason;
            if (!status.needs.empty())
                item["needs"] = status.needs;
        }
        else if (!status.note.empty())
            item["note"] = status.note;
        list.push(item);
    }
    root["formats"] = list;
    return root;
}

SaveFormats QuerySaveFormats(EmulatorContext& context)
{
    SaveFormats out;
    const SaveFormat all[] = {SaveFormat::Sna, SaveFormat::Z80, SaveFormat::Szx};
    for (SaveFormat format : all)
    {
        FormatStatus status;
        status.format = format;
        out.formats.push_back(status);
    }
    auto setAll = [&](const std::string& reason, const std::string& needs) {
        for (FormatStatus& status : out.formats)
        {
            status.available = false;
            status.reason = reason;
            status.needs = needs;
        }
    };

    const MachineView view = ExamineView(context);
    if (!view.available)
    {
        out.view = view.reason;
        setAll(view.reason, view.needs);
        return out;
    }
    out.viewAvailable = true;
    out.machine = MachineName(view);
    out.view = view.note.empty() ? "the machine's own memory" : view.note;

    FormatStatus& sna = out.formats[0];
    FormatStatus& z80 = out.formats[1];
    FormatStatus& szx = out.formats[2];
    const bool pentagonBig = view.machineHint == "pentagon512" || view.machineHint == "pentagon1024";

    // SNA: 48K or 128K, banks 0-7, no #1FFD, no AY
    if (view.banks.size() > 8)
    {
        sna.reason = "the .sna format holds 128 KB (banks 0-7) and this machine has " + std::to_string(view.banks.size() * 16) +
                     " KB of RAM in use; save as ." + (pentagonBig ? "szx" : "z80");
        sna.needs = pentagonBig ? "format:szx" : "format:z80";
    }
    else if (view.p1FFD && (*view.p1FFD & 0x01))
    {
        sna.reason = "the machine runs in #1FFD special paging, which a .sna cannot hold (it has no #1FFD); save as .z80 or .szx";
        sna.needs = "format:z80";
    }
    else
    {
        sna.available = true;
        if (view.p1FFD && *view.p1FFD != 0)
            sna.note = "#1FFD " + Hex(*view.p1FFD, 2) + " (ROM selection) is not in a .sna";
        else if (view.machineHint != "48k")
            sna.note = "the AY registers are not in a .sna";
        if (SavesAs48K(view.layout48, view.banks.size(), view.layout48 ? std::nullopt : std::optional<uint8_t>(view.p7FFD), view.p1FFD))
        {
            // The 48K SNA keeps the PC on the stack: the two bytes below SP are overwritten in the file's copy of RAM
            const Z80* z = context.pCore ? context.pCore->GetZ80() : nullptr;
            if (z && z->sp >= 1 && z->sp < 0x4002)
            {
                sna.available = false;
                sna.reason = "a 48K .sna keeps the PC on the stack, and SP is " + Hex(z->sp) +
                             ": the stack is in ROM (or wraps through it), so the PC has nowhere to go; save as .z80 or .szx";
                sna.needs = "format:z80";
                sna.note.clear();
            }
        }
    }

    // Z80: v3 with the models it names
    if (pentagonBig)
    {
        z80.reason = "the .z80 format has no Pentagon 512 / 1024 model (its Pentagon is the 128K one); save as .szx";
        z80.needs = "format:szx";
    }
    else
    {
        z80.available = true;
        if (view.layout48 && view.machineHint != "48k")
            z80.note = "";
    }

    // SZX: the model needs a machine id
    if (!szx::IdFor(view.model, view.ramKb))
    {
        szx.reason = "this model has no SZX machine id; save as .z80 or .sna";
        szx.needs = "format:z80";
    }
    else
        szx.available = true;
    return out;
}

bool CaptureImage(EmulatorContext& context, const MachineView& view, Image& out)
{
    if (!view.available || !context.pCore || !context.pCore->GetZ80())
        return false;
    const Z80& z = *context.pCore->GetZ80();
    const EmulatorState& state = context.emulatorState;

    out = Image{};
    out.format = "capture";
    out.machineHint = view.machineHint;
    out.timingHint = view.timingHint;
    out.memoryModel = view.layout48 ? MemoryModel::Mem48k : (view.banks.size() > 8 ? MemoryModel::Extended : MemoryModel::Mem128k);
    for (const auto& bank : view.banks)
        out.banks[bank.first] = std::vector<uint8_t>(bank.second, bank.second + 16384);

    if (!view.layout48)
        out.paging.p7FFD = view.p7FFD;
    out.paging.p1FFD = view.p1FFD;
    out.paging.pEFF7 = view.pEFF7;
    out.trdosPaged = (state.flags & CF_TRDOS) != 0;

    Cpu& cpu = out.cpu;
    cpu.af = z.af;
    cpu.bc = z.bc;
    cpu.de = z.de;
    cpu.hl = z.hl;
    cpu.ix = z.ix;
    cpu.iy = z.iy;
    cpu.sp = z.sp;
    cpu.pc = z.pc;
    cpu.af2 = z.alt.af;
    cpu.bc2 = z.alt.bc;
    cpu.de2 = z.alt.de;
    cpu.hl2 = z.alt.hl;
    cpu.i = z.i;
    cpu.r = static_cast<uint8_t>((z.r_low & 0x7F) | (z.r_hi & 0x80));
    cpu.iff1 = z.iff1 != 0;
    cpu.iff2 = z.iff2 != 0;
    cpu.im = static_cast<uint8_t>(z.im & 0x03);
    cpu.memptr = z.memptr;
    cpu.halted = z.halted != 0;
    cpu.eiShadow = z.boundary == Z80_BOUNDARY_INT_SHADOW;
    out.framePosition = LoaderSZX::IntCountFromFramePosition(&context, z.t);

    out.border = static_cast<uint8_t>((context.pScreen ? context.pScreen->GetBorderColor() : state.border_attr) & 0x07);
    out.portFE = state.pFE;

    // The AY: every machine of the 128K family has one; a 48K machine has none, a 48K mode of a bigger machine does
    if (view.machineHint != "48k" && context.pSoundManager)
    {
        if (SoundChip_AY8910* psg = context.pSoundManager->getAYChip(0))
        {
            Ay ay;
            const uint8_t* registers = psg->getRegisters();
            for (size_t i = 0; i < 16; i++)
                ay.registers[i] = registers[i];
            ay.selected = static_cast<uint8_t>(psg->getCurrentRegisterIndex() & 0x0F);
            out.ay.push_back(ay);
        }
    }
    return true;
}

StateNode SaveResult::ToStateNode() const
{
    StateNode root = StateNode::Object();
    root["ok"] = ok;
    root["message"] = text;
    root["format"] = format;
    root["path"] = path;
    root["machine"] = machine;
    if (!ok)
    {
        root["reason"] = reason;
        if (!needs.empty())
            root["needs"] = needs;
    }
    StateNode list = StateNode::Array();
    for (const std::string& warning : warnings)
        list.push(warning);
    root["warnings"] = list;
    return root;
}

SaveResult SaveSnapshotFile(EmulatorContext& context, SaveFormat format, const std::string& path)
{
    SaveResult result;
    result.format = ToText(format);
    result.path = path;

    const SaveFormats formats = QuerySaveFormats(context);
    const FormatStatus& status = formats.For(format);
    result.machine = formats.machine;
    if (!status.available)
    {
        result.reason = status.reason;
        result.needs = status.needs;
        result.text = "cannot save ." + result.format + ": " + status.reason;
        return result;
    }
    if (!status.note.empty())
        result.warnings.push_back(status.note);

    std::string error;
    if (format == SaveFormat::Szx)
    {
        LoaderSZX loader(&context, path);
        result.ok = loader.save();
        error = loader.GetError();
        for (const std::string& warning : loader.GetReport().warnings)
            result.warnings.push_back(warning);
    }
    else
    {
        Image image;
        if (!CaptureImage(context, ExamineView(context), image))
        {
            result.reason = "the machine state could not be captured";
        }
        else if (format == SaveFormat::Sna)
        {
            LoaderSNA loader(&context, path);
            result.ok = loader.WriteImage(image, error);
        }
        else
        {
            LoaderZ80 loader(&context, path);
            result.ok = loader.WriteImage(image, error, result.warnings);
        }
    }

    if (result.ok)
        result.text = "saved " + result.format + " (" + result.machine + ") to " + path;
    else
    {
        if (result.reason.empty())
            result.reason = error.empty() ? "the file could not be written" : error;
        result.text = "cannot save ." + result.format + ": " + result.reason;
    }
    return result;
}
}  // namespace snapshot
