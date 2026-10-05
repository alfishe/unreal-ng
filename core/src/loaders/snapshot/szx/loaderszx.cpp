#include "loaders/snapshot/szx/loaderszx.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <vector>

#include "common/filehelper.h"
#include "emulator/buildinfo.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/corestate.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/upd765.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/tape/tape.h"
#include "emulator/media/mediamanager.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/covox.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "loaders/snapshot/szx/szxreader.h"
#include "loaders/snapshot/szx/szxwriter.h"

using namespace szx;

namespace
{
    std::string PathToUtf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    /// The machine's frame geometry in base T-states
    struct FrameGeometry
    {
        uint32_t frame = 0;
        uint32_t intFirst = 0;  ///< the first T-state that sees the INT (intstart + 1)
        uint32_t intLength = 0; ///< T-states that see it (intlen - 1: the window is open at both ends)
        uint32_t multiplier = 1;
    };

    FrameGeometry Geometry(EmulatorContext* context)
    {
        FrameGeometry g;
        g.frame = context->config.frame ? context->config.frame : 1;
        g.intFirst = (context->config.intstart + 1) % g.frame;
        g.intLength = context->config.intlen > 1 ? context->config.intlen - 1 : 0;
        g.multiplier = context->emulatorState.current_z80_frequency_multiplier
                           ? context->emulatorState.current_z80_frequency_multiplier
                           : 1;
        return g;
    }

    /// The running machine can hold the snapshot's machine
    bool MachineFits(EmulatorContext* context, const Machine& machine, Report& report)
    {
        const MEM_MODEL running = context->config.mem_model;
        const uint32_t ramKb = context->config.ramsize;
        if (machine.model == MM_SCORP && running == MM_PROFSCORP)
        {
            report.warnings.push_back("Scorpion ZS-256 snapshot loaded on a Scorpion with ProfROM (the configured ROM stays)");
            return ramKb >= machine.ramKb;
        }
        if (running != machine.model)
            return false;
        if (machine.model == MM_PENTAGON && ramKb != machine.ramKb)
        {
            if (ramKb < machine.ramKb)
                return false;
            report.warnings.push_back("Pentagon " + std::to_string(machine.ramKb) + "K snapshot loaded on a Pentagon " +
                                      std::to_string(ramKb) + "K");
        }
        return true;
    }
}  // namespace

LoaderSZX::LoaderSZX(EmulatorContext* context, const std::string& path) : _context(context), _path(path) {}

bool LoaderSZX::load()
{
    _report = Report{};
    const size_t size = FileHelper::FileExists(_path) ? FileHelper::GetFileSize(_path) : 0;
    if (size == 0)
    {
        _error = "cannot read '" + _path + "'";
        return false;
    }
    std::vector<uint8_t> data(size);
    if (FileHelper::ReadFileToBuffer(_path, data.data(), size) != size)
    {
        _error = "cannot read '" + _path + "'";
        return false;
    }
    Stage stage;
    if (!SzxReader::Parse(data.data(), data.size(), stage, _error))
        return false;
    stage.folder = PathToUtf8(FileHelper::ToFsPath(_path).parent_path());

    // Snapshot pipeline: the image of the file and the plan step, then today's commit
    _image = BuildImage(stage, _path);
    _snapshotReport = snapshot::Report();
    if (!snapshot::Pipeline::Plan(_image, _context, _options, _snapshotReport))
    {
        _error = _snapshotReport.reason;
        return false;
    }
    const bool committed = Commit(_context, stage, _report, _error);
    // SZX's own per-block outcomes ride on the pipeline's report
    AppendReport(_report, _snapshotReport);
    if (!committed)
        _snapshotReport.Refuse(_error);
    return committed;
}

void LoaderSZX::AppendReport(const Report& from, snapshot::Report& to)
{
    for (const ReportEntry& entry : from.entries)
    {
        snapshot::Outcome outcome = snapshot::Outcome::Applied;
        switch (entry.outcome)
        {
            case Outcome::Applied: outcome = snapshot::Outcome::Applied; break;
            case Outcome::Approximated: outcome = snapshot::Outcome::Approximated; break;
            case Outcome::Ignored: outcome = snapshot::Outcome::Ignored; break;
            case Outcome::Unknown: outcome = snapshot::Outcome::Unknown; break;
        }
        to.Add(entry.block, outcome, entry.note);
    }
    to.warnings.insert(to.warnings.end(), from.warnings.begin(), from.warnings.end());
}

snapshot::Image LoaderSZX::BuildImage(const Stage& stage, const std::string& path)
{
    snapshot::Image image;
    image.format = "szx";
    image.sourcePath = path;
    image.formatVersion = std::to_string(stage.versionMajor) + "." + std::to_string(stage.versionMinor);
    image.rawMachineId = "szx " + std::to_string(stage.machineId);
    image.warnings = stage.warnings;

    const uint8_t id = stage.machineId;
    switch (id)
    {
        case Mid16K:
        case Mid48K:
        case MidNtsc48K: image.machineHint = "48k"; break;
        case Mid128K:
        case Mid128Ke: image.machineHint = "128k"; break;
        case MidPlus2: image.machineHint = "plus2"; break;
        case MidPlus2A: image.machineHint = "plus2a"; break;
        case MidPlus3:
        case MidPlus3E: image.machineHint = "plus3"; break;
        case MidPentagon128: image.machineHint = "pentagon128"; break;
        case MidPentagon512: image.machineHint = "pentagon512"; break;
        case MidPentagon1024: image.machineHint = "pentagon1024"; break;
        case MidScorpion: image.machineHint = "scorpion256"; break;
        case MidTc2048:
        case MidTc2068:
        case MidTs2068:
        case MidSe: image.machineHint = "timex"; break;
        default: image.machineHint = "unknown"; break;
    }
    const bool is48 = id == Mid16K || id == Mid48K || id == MidNtsc48K;
    image.memoryModel = is48 ? snapshot::MemoryModel::Mem48k
                        : (id == MidPentagon512 || id == MidPentagon1024 || id == MidScorpion)
                            ? snapshot::MemoryModel::Extended
                            : snapshot::MemoryModel::Mem128k;
    image.timingHint = is48 ? "48k" : (id == MidPentagon128 || id == MidPentagon512 || id == MidPentagon1024) ? "pentagon" : "128k";

    for (const auto& page : stage.pages)
        image.banks[page.first] = page.second;

    if (stage.z80)
    {
        const Z80Regs& z = *stage.z80;
        snapshot::Cpu& cpu = image.cpu;
        cpu.af = z.af;
        cpu.bc = z.bc;
        cpu.de = z.de;
        cpu.hl = z.hl;
        cpu.af2 = z.af1;
        cpu.bc2 = z.bc1;
        cpu.de2 = z.de1;
        cpu.hl2 = z.hl1;
        cpu.ix = z.ix;
        cpu.iy = z.iy;
        cpu.sp = z.sp;
        cpu.pc = z.pc;
        cpu.i = z.i;
        cpu.r = z.r;
        cpu.iff1 = z.iff1 != 0;
        cpu.iff2 = z.iff2 != 0;
        cpu.im = z.im;
        cpu.memptr = z.memptr;
        cpu.halted = (z.flags & kHalted) != 0;
        cpu.eiShadow = (z.flags & kSuppressInts) != 0;
        image.framePosition = z.cyclesStart;
    }
    if (stage.spec)
    {
        image.border = stage.spec->border & 7u;
        image.paging.p7FFD = stage.spec->port7FFD;
        if (HasPort1FFD(id))
            image.paging.p1FFD = stage.spec->port1FFDorEFF7;
        if (HasPortEFF7(id))
            image.paging.pEFF7 = stage.spec->port1FFDorEFF7;
    }
    if (stage.beta && (stage.beta->flags & kBetaPaged))
        image.trdosPaged = true;
    if (stage.ay)
    {
        snapshot::Ay ay;
        ay.registers = stage.ay->registers;
        ay.selected = stage.ay->currentRegister;
        image.ay.push_back(ay);
    }

    // What else the file carries (descriptors; the payloads stay with the stage until a commit reads the image)
    auto add = [&](const char* origin, const char* kind, size_t size, std::string note = {}) {
        image.extensions.push_back({origin, kind, size, std::move(note), {}});
    };
    if (stage.beta)
        add("szx:B128", "beta128", kBeta128Size, "Beta 128 interface and its WD1793");
    for (const BetaDisk& d : stage.betaDisks)
        add("szx:BDSK", "disk", d.image.size(), (d.image.empty() ? "linked " + d.fileName : std::string("embedded")) +
                                                    ", drive " + std::to_string(d.drive));
    if (stage.plus3)
        add("szx:+3", "plus3-fdc", 2);
    for (const DskFile& d : stage.dskFiles)
        add("szx:DSK", "disk", 0, "linked " + d.fileName + ", drive " + std::to_string(d.drive));
    if (stage.tape)
        add("szx:TAPE", "tape", stage.tape->image.size(),
            stage.tape->image.empty() ? "linked " + stage.tape->fileName : std::string("embedded ") + stage.tape->extension);
    if (stage.gs)
        add("szx:GS", "general-sound", kGsSize, "General Sound, " + std::to_string(stage.gsPages.size()) + " RAM pages");
    if (stage.keyboard)
        add("szx:KEYB", "keyboard", 5);
    if (stage.joysticks)
        add("szx:JOY", "joystick", 6);
    if (stage.mouse)
        add("szx:AMXM", "mouse", 7);
    if (stage.covox)
        add("szx:COVX", "covox", 1);
    if (stage.specDrum)
        add("szx:DRUM", "specdrum", 1);
    for (const auto& other : stage.otherBlocks)
        add(("szx:" + other.first).c_str(), "unhandled", other.second, "a block this emulator does not take");
    return image;
}

bool LoaderSZX::save()
{
    _report = Report{};
    Stage stage;
    stage.folder = PathToUtf8(FileHelper::ToFsPath(_path).parent_path());
    if (!Capture(_context, stage, _error))
        return false;
    _report.warnings = stage.warnings;
    std::vector<uint8_t> bytes = SzxWriter::Write(stage);
    if (!FileHelper::SaveBufferToFile(_path, bytes.data(), bytes.size()))
    {
        _error = "cannot write '" + _path + "'";
        return false;
    }
    return true;
}

bool LoaderSZX::ProbeMachine(const std::string& path, Machine& machine, std::string& error)
{
    uint8_t header[kHeaderSize] = {};
    if (!FileHelper::FileExists(path) || FileHelper::ReadFileToBuffer(path, header, kHeaderSize) != kHeaderSize ||
        Get32(header) != kMagic)
    {
        error = "not an SZX file: '" + path + "'";
        return false;
    }
    return MachineFor(header[6], machine, error);
}

uint32_t LoaderSZX::FramePositionFromIntCount(EmulatorContext* context, uint32_t cyclesFromInt)
{
    const FrameGeometry g = Geometry(context);
    return ((g.intFirst + cyclesFromInt % g.frame) % g.frame) * g.multiplier;
}

uint32_t LoaderSZX::IntCountFromFramePosition(EmulatorContext* context, uint32_t framePosition)
{
    const FrameGeometry g = Geometry(context);
    const uint32_t base = (framePosition / g.multiplier) % g.frame;
    return (base + g.frame - g.intFirst) % g.frame;
}

/// region <Commit>

bool LoaderSZX::Commit(EmulatorContext* context, const Stage& stage, Report& report, std::string& error)
{
    if (!context || !context->pCore || !context->pMemory || !context->pPortDecoder || !stage.z80 || !stage.spec)
    {
        error = "no machine to load into";
        return false;
    }

    Machine machine;
    if (!MachineFor(stage.machineId, machine, error))
        return false;
    if (!machine.note.empty())
        report.warnings.push_back(machine.note);
    if (!MachineFits(context, machine, report))
    {
        error = "the snapshot was saved on a " + DescribeModel(machine.model, machine.ramKb) +
                ", the running machine is a " + DescribeModel(context->config.mem_model, context->config.ramsize) +
                ": create a " + DescribeModel(machine.model, machine.ramKb) + " to load it";
        return false;
    }
    for (const std::string& warning : stage.warnings)
        report.warnings.push_back(warning);
    if (stage.headerFlags & kAlternateTimings)
        report.warnings.push_back("alternate (late) timings requested: the configured timing stays");
    if (stage.creator)
        report.Add("CRTR", Outcome::Applied, stage.creator->name + " " + std::to_string(stage.creator->major) + "." +
                                                 std::to_string(stage.creator->minor));

    Core& core = *context->pCore;
    Memory& memory = *context->pMemory;
    core.Reset();

    // RAM: the machine's pages; a page outside its RAM is reported. A 48K
    // keeps its three pages under their 128K numbers 5, 2 and 0
    const uint32_t ramPages = context->config.ramsize / 16;
    for (const auto& [page, bytes] : stage.pages)
    {
        const bool fits = machine.model == MM_SPECTRUM48 ? (page == 0 || page == 2 || page == 5) : page < ramPages;
        if (!fits)
        {
            report.Add("RAMP " + std::to_string(page), Outcome::Ignored, "not a RAM page of this machine");
            continue;
        }
        memory.LoadRAMPageData(page, const_cast<uint8_t*>(bytes.data()), bytes.size());
        report.Add("RAMP " + std::to_string(page), Outcome::Applied);
    }

    ApplyPaging(context, stage, report);
    ApplyCpu(context, stage, report);

    // AY: all registers, then the selected one.
    //
    // TurboSound FM REPLACES TurboSound: on a TSFM machine (TurboSound=FM,
    // the shipped default on most models) the AY block goes into the SSG
    // half of YM2203 chip 1 - the same SoundChip_AY8910 class, reached through
    // getAYChip(0) - and applies fully. This is not an approximation and must
    // never be reported as an error. Only TurboSound=None has no AY.
    if (stage.ay)
    {
        SoundChip_AY8910* ay = context->pSoundManager ? context->pSoundManager->getAYChip(0) : nullptr;
        if (ay)
        {
            for (uint8_t reg = 0; reg < 16; reg++)
                ay->writeRegister(reg, stage.ay->registers[reg]);
            // Select the register through the TurboSound device's own #FFFD,
            // so every address latch agrees: the AY's, and on TurboSound FM
            // the YM2203's and ymfm's (the SSG half of chip 0 holds the block)
            ITurboSoundDevice* device = context->pSoundManager->getTurboSound();
            if (device)
                device->portDeviceOutMethod(0xFFFD, stage.ay->currentRegister);
            else
                ay->setRegister(stage.ay->currentRegister);
            const bool fm = device && device->hasFm();
            std::string note = fm ? "into the SSG half of YM2203 chip 1 (TurboSound FM)" : "";
            if (stage.ay->flags)
                note += std::string(note.empty() ? "" : "; ") + "Fuller / Melodik flags ignored";
            report.Add("AY", Outcome::Applied, note);
        }
        else
        {
            report.Add("AY", Outcome::Ignored, "no AY fitted (TurboSound=None)");
        }
    }

    // Beta 128: the WD1793 registers and the #FF system register
    if (stage.beta)
    {
        if (context->pBetaDisk)
        {
            const Beta128& b = *stage.beta;
            context->pBetaDisk->RestoreSnapshotRegisters(b.system, b.track, b.sector, b.data, b.status,
                                                         !(b.flags & kBetaSeekLower));
            report.Add("B128", Outcome::Approximated, "registers and paging; a command in flight is not in the format");
        }
        else
        {
            report.Add("B128", Outcome::Ignored, "this machine has no Beta 128 interface");
        }
    }

    ApplyMedia(context, stage, report);
    ApplyDevices(context, stage, report);

    for (const auto& [name, size] : stage.otherBlocks)
        report.Add(name, Outcome::Ignored, "hardware this machine does not have");

    // Border: the picture and the port latch every consumer reads back
    const uint8_t border = static_cast<uint8_t>(stage.spec->border & 0x07);
    EmulatorState& state = context->emulatorState;
    const unsigned version = (static_cast<unsigned>(stage.versionMajor) << 8) | stage.versionMinor;
    state.pFE = version >= 0x0101 ? static_cast<uint8_t>((stage.spec->portFE & 0xF8) | border)
                                  : static_cast<uint8_t>((state.pFE & 0xF8) | border);
    state.border_attr = border;
    if (context->pScreen)
    {
        context->pScreen->FillBorderWithColor(border);
        context->pScreen->RenderOnlyMainScreen();
    }
    report.Add("SPCR", Outcome::Applied);
    return true;
}

void LoaderSZX::ApplyPaging(EmulatorContext* context, const Stage& stage, Report& report)
{
    Memory& memory = *context->pMemory;
    PortDecoder& ports = *context->pPortDecoder;
    EmulatorState& state = context->emulatorState;
    const SpecRegs& spec = *stage.spec;
    const uint16_t pc = stage.z80->pc;
    const bool betaPaged = stage.beta && (stage.beta->flags & kBetaPaged) && context->pBetaDisk;

    // The TR-DOS session comes from the snapshot, not from what the machine
    // was doing before the load (the SNA loader's rule)
    state.flags &= ~CF_TRDOS;
    memory.UpdateZ80Banks();

    if (!HasAy(stage.machineId))
    {
        memory.SetRAMPageToBank1(5);
        memory.SetRAMPageToBank2(2);
        memory.SetRAMPageToBank3(0);
        memory.SetROM48k();
        return;
    }

    // Each model's decoder applies its own rules: #1FFD / #EFF7 first, #7FFD
    // last (its lock bit would block the others), then the stored values win
    ports.UnlockPaging();
    memory.SetRAMPageToBank1(5);
    memory.SetRAMPageToBank2(2);
    if (HasPortEFF7(stage.machineId))
    {
        ports.DecodePortOut(0xEFF7, spec.port1FFDorEFF7, pc);
        state.pEFF7 = spec.port1FFDorEFF7;
    }
    if (HasPort1FFD(stage.machineId))
    {
        ports.DecodePortOut(0x1FFD, spec.port1FFDorEFF7, pc);
        state.p1FFD = spec.port1FFDorEFF7;
    }
    ports.DecodePortOut(0x7FFD, spec.port7FFD, pc);
    state.p7FFD = spec.port7FFD;

    if (betaPaged)
    {
        state.flags |= CF_TRDOS;
        memory.SetROMDOS();
        report.warnings.push_back("TR-DOS ROM paged in (B128 PAGED)");
    }
}

void LoaderSZX::ApplyCpu(EmulatorContext* context, const Stage& stage, Report& report)
{
    Z80& cpu = *context->pCore->GetZ80();
    const Z80Regs& z = *stage.z80;

    cpu.af = z.af;
    cpu.bc = z.bc;
    cpu.de = z.de;
    cpu.hl = z.hl;
    cpu.alt.af = z.af1;
    cpu.alt.bc = z.bc1;
    cpu.alt.de = z.de1;
    cpu.alt.hl = z.hl1;
    cpu.ix = z.ix;
    cpu.iy = z.iy;
    cpu.sp = z.sp;
    cpu.pc = z.pc;
    cpu.i = z.i;
    cpu.r_low = z.r;
    cpu.r_hi = static_cast<uint8_t>(z.r & 0x80);
    cpu.iff1 = z.iff1;
    cpu.iff2 = z.iff2;
    cpu.im = z.im;
    cpu.memptr = z.memptr;
    // Q is F after an instruction that set F, else 0
    cpu.q = (z.flags & kFset) ? static_cast<uint8_t>(z.af & 0xFF) : 0;
    cpu.boundary = (z.flags & kSuppressInts) ? Z80_BOUNDARY_INT_SHADOW : Z80_BOUNDARY_NONE;
    cpu.halted = (z.flags & kHalted) ? 1 : 0;
    cpu.halt_cycle = 0;
    cpu.haltpos = 0;
    // While halted, PC points at the HALT here and in Fuse (the INT / NMI
    // acknowledge steps past it). A writer that stored the address after the
    // HALT is recognized by the opcodes: nothing at PC, the HALT at PC - 1
    std::string haltNote;
    if (cpu.halted)
    {
        Memory& memory = *context->pMemory;
        if (memory.DirectReadFromZ80Memory(cpu.pc) != 0x76 &&
            memory.DirectReadFromZ80Memory(static_cast<uint16_t>(cpu.pc - 1)) == 0x76)
        {
            cpu.pc = static_cast<uint16_t>(cpu.pc - 1);
            haltNote = ", PC moved back onto the HALT (the writer stored the address after it)";
        }
    }

    // Frame position: counted from the INT in the file
    const FrameGeometry g = Geometry(context);
    const uint32_t fromInt = z.cyclesStart % g.frame;
    cpu.t = FramePositionFromIntCount(context, fromInt);
    // Inside the INT window with nothing left to accept: the INT was served
    cpu.int_acked_in_pulse = (fromInt < g.intLength && z.holdIntReqCycles == 0) ? 1 : 0;
    report.Add("Z80R", Outcome::Applied,
               "t " + std::to_string(cpu.t) + " (" + std::to_string(fromInt) + " after the INT)" +
                   ((stage.versionMajor << 8 | stage.versionMinor) < 0x0104 ? ", MEMPTR from chBitReg" : "") + haltNote);
}

/// endregion </Commit>

/// region <Capture>

/// region <Media and devices>

namespace
{
    /// UTF-8 path helpers over FileHelper (non-ASCII paths on Windows)
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    /// A linked image: next to the snapshot (as stored, then by name), then the stored path
    std::string ResolveLink(const std::string& folder, const std::string& stored)
    {
        if (stored.empty())
            return {};
        std::string normalized = stored;
        for (char& c : normalized)
            if (c == '\\')
                c = '/';
        const std::filesystem::path storedPath = FileHelper::ToFsPath(normalized);
        std::vector<std::string> candidates;
        if (!folder.empty())
        {
            const std::filesystem::path base = FileHelper::ToFsPath(folder);
            if (storedPath.is_relative())
                candidates.push_back(Utf8(base / storedPath));
            candidates.push_back(Utf8(base / storedPath.filename()));
        }
        candidates.push_back(normalized);
        for (const std::string& candidate : candidates)
            if (FileHelper::FileExists(candidate))
                return candidate;
        return {};
    }

    /// An embedded image in a temporary file, deleted with its medium (MediaSourceType::Upload)
    std::string StageImage(const std::vector<uint8_t>& bytes, const std::string& extension)
    {
        static std::atomic<uint32_t> sequence{0};
        std::error_code ec;
        const std::filesystem::path folder = std::filesystem::temp_directory_path(ec) / "unreal-ng-szx";
        std::filesystem::create_directories(folder, ec);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::string path = Utf8(folder / ("image-" + std::to_string(stamp) + "-" + std::to_string(++sequence) + "." + extension));
        if (!FileHelper::SaveBufferToFile(path, const_cast<uint8_t*>(bytes.data()), bytes.size()))
            return {};
        return path;
    }

    MediaResult InsertImage(EmulatorContext* context, const std::string& slot, const std::string& path, bool staged,
                            bool writeProtect)
    {
        MediaSource source;
        source.type = staged ? MediaSourceType::Upload : MediaSourceType::File;
        source.path = path;
        InsertOptions options;
        options.immediate = true;
        options.access = AccessMode::Session;  // never write into a linked file
        options.writeProtect = writeProtect;
        return context->pMediaManager->Insert(slot, source, options);
    }

    const char* BetaDiskExtension(uint8_t type)
    {
        switch (type)
        {
            case DiskTrd: return "trd";
            case DiskScl: return "scl";
            case DiskFdi: return "fdi";
            case DiskUdi: return "udi";
            default: return nullptr;
        }
    }

    std::optional<uint8_t> DiskTypeOf(const std::string& format)
    {
        if (format == "trd")
            return DiskTrd;
        if (format == "scl")
            return DiskScl;
        if (format == "fdi")
            return DiskFdi;
        if (format == "udi")
            return DiskUdi;
        return std::nullopt;
    }

    /// Saving: the path relative to the snapshot's folder when inside it
    std::string LinkFor(const std::string& folder, const std::string& path)
    {
        if (!folder.empty())
        {
            std::error_code ec;
            const std::filesystem::path relative =
                std::filesystem::relative(FileHelper::ToFsPath(path), FileHelper::ToFsPath(folder), ec);
            if (!ec && !relative.empty() && Utf8(relative).rfind("..", 0) != 0)
                return Utf8(relative);
        }
        return path;
    }

    /// A slot's medium worth a link: present and backed by a file
    std::optional<SlotInfo> LinkableMedium(EmulatorContext* context, const std::string& slot, Stage& stage)
    {
        if (!context->pMediaManager)
            return std::nullopt;
        std::optional<SlotInfo> info = context->pMediaManager->Info(slot);
        if (!info || !info->present)
            return std::nullopt;
        if (!FileHelper::FileExists(info->source))
        {
            stage.warnings.push_back(slot + ": not a file (" + info->source + "), not saved");
            return std::nullopt;
        }
        if (info->dirty)
            stage.warnings.push_back(slot + ": unsaved writes are not in the snapshot, only a link to " + info->source);
        return info;
    }

    /// The GS card's TTD blob layout (SoundChip_GeneralSound::serializeFixedState)
    constexpr size_t kGsMpag = 4, kGsVolume = 5, kGsData = 9, kGsCpu = 24;
}  // namespace

void LoaderSZX::ApplyMedia(EmulatorContext* context, const Stage& stage, Report& report)
{
    MediaManager* media = context->pMediaManager;
    for (const BetaDisk& disk : stage.betaDisks)
    {
        const std::string block = "BDSK " + std::string(1, static_cast<char>('A' + (disk.drive & 3)));
        const char* extension = BetaDiskExtension(disk.type);
        if (!context->pBetaDisk || !media || disk.drive > 3 || !extension)
        {
            report.Add(block, Outcome::Ignored, !context->pBetaDisk ? "this machine has no Beta 128 interface" : "unknown drive or disk type");
            continue;
        }
        const bool embedded = !disk.image.empty();
        const std::string path = embedded ? StageImage(disk.image, extension) : ResolveLink(stage.folder, disk.fileName);
        if (path.empty())
        {
            report.Add(block, Outcome::Ignored, embedded ? "cannot stage the embedded image" : "linked image not found: " + disk.fileName);
            continue;
        }
        const std::string slot = std::string("fdd.") + static_cast<char>('a' + disk.drive);
        const MediaResult result = InsertImage(context, slot, path, embedded, disk.flags & kDiskWriteProtect);
        if (!result.Ok())
        {
            report.Add(block, Outcome::Ignored, result.message);
            continue;
        }
        if (FDD* drive = context->coreState.diskDrives[disk.drive])
            drive->setTrack(static_cast<int8_t>(disk.cylinder));
        report.Add(block, Outcome::Applied, (embedded ? "embedded " : "linked " + path + ", ") + std::string(extension) +
                                                 ", cylinder " + std::to_string(disk.cylinder));
    }

    if (stage.plus3)
    {
        if (context->pUPD765)
        {
            context->pUPD765->setMotor(stage.plus3->motorOn != 0);
            report.Add("+3", Outcome::Applied, stage.plus3->motorOn ? "motor on" : "motor off");
        }
        else
        {
            report.Add("+3", Outcome::Ignored, "this machine has no +3 disk controller");
        }
    }
    for (const DskFile& file : stage.dskFiles)
    {
        const std::string block = "DSK " + std::string(1, static_cast<char>('A' + (file.drive & 1)));
        if (!context->pUPD765 || !media || file.drive > 1)
        {
            report.Add(block, Outcome::Ignored, "this machine has no +3 disk controller");
            continue;
        }
        const std::string path = ResolveLink(stage.folder, file.fileName);
        if (path.empty())
        {
            report.Add(block, Outcome::Ignored, "linked image not found: " + file.fileName);
            continue;
        }
        const MediaResult result = InsertImage(context, std::string("fdd.") + static_cast<char>('a' + file.drive), path, false, false);
        report.Add(block, result.Ok() ? Outcome::Applied : Outcome::Ignored, result.Ok() ? "linked " + path : result.message);
    }

    if (stage.tape)
    {
        const szx::Tape& tape = *stage.tape;
        const bool embedded = !tape.image.empty();
        std::string extension = tape.extension.empty() ? "tzx" : tape.extension;
        for (char& c : extension)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string path;
        if (embedded && extension != "tapw")  // "tapw" = Warajevo .tap, not a format we read
            path = StageImage(tape.image, extension);
        else if (!embedded)
            path = ResolveLink(stage.folder, tape.fileName);
        MediaResult result = MediaResult::Fail(MediaError::BadRequest, "linked image not found: " + tape.fileName);
        if (!path.empty() && media && context->pTape)
            result = InsertImage(context, "tape", path, embedded, false);
        // The deck takes the medium's blocks lazily: load them, then seek
        if (result.Ok() && context->pTape->EnsureImageLoaded() && !context->pTape->SeekToBlock(tape.block))
            report.warnings.push_back("TAPE: block " + std::to_string(tape.block) + " is past the tape's end, left at block 0");
        if (result.Ok())
        {
            report.Add("TAPE", Outcome::Applied, (embedded ? "embedded " + extension : "linked " + path) + ", block " +
                                                     std::to_string(tape.block) + ", stopped");
        }
        else
        {
            report.Add("TAPE", Outcome::Ignored, extension == "tapw" ? "Warajevo .tap is not supported" : result.message);
        }
    }
}

void LoaderSZX::ApplyDevices(EmulatorContext* context, const Stage& stage, Report& report)
{
    SoundManager* sound = context->pSoundManager;

    // NeoGS REPLACES the General Sound: a GS block on a NeoGS machine (the
    // shipped default, GSType=NGS) is accepted, never an error. The block is
    // the classic card's internal state (GS ROM 1.04 CPU registers, its
    // 32 KB page map, its RAM); NeoGS runs its own firmware with its own
    // memory map, so that state cannot be transplanted - the card keeps
    // running and the report says why. With the classic card (GSType=Z80)
    // the block is restored as below
    if (stage.gs)
    {
        GeneralSoundCard* card = (sound && sound->hasGeneralSound()) ? sound->getGeneralSound() : nullptr;
        if (card && card->implementation() == GSCardImplementation::NGS)
        {
            report.Add("GS", Outcome::Approximated,
                       "NeoGS replaces the GS: it keeps running its own firmware; the classic card's CPU and RAM "
                       "state in the block does not map onto it");
        }
        else if (!card || card->implementation() != GSCardImplementation::LLE)
        {
            report.Add("GS", Outcome::Ignored, card ? "the lightweight GS player has no CPU state" : "no General Sound card fitted");
        }
        else
        {
            const GeneralSound& gs = *stage.gs;
            // The card's own state with the snapshot's fields over it: the
            // timing bases the format does not carry stay as they are
            std::vector<uint8_t> blob(card->TTDStateSize());
            card->TTDSaveState(blob.data());
            blob[kGsMpag] = gs.upperPage;
            std::copy(gs.volume.begin(), gs.volume.end(), blob.begin() + kGsVolume);
            std::copy(gs.output.begin(), gs.output.end(), blob.begin() + kGsData);
            uint8_t* z80 = blob.data() + kGsCpu;
            const Z80Regs& z = gs.cpu;
            const uint16_t words[] = {z.af, z.bc, z.de, z.hl, z.af1, z.bc1, z.de1, z.hl1, z.ix, z.iy, z.sp, z.pc, z.memptr};
            for (size_t i = 0; i < 13; i++)
            {
                z80[i * 2] = static_cast<uint8_t>(words[i]);
                z80[i * 2 + 1] = static_cast<uint8_t>(words[i] >> 8);
            }
            z80[26] = z.i;
            z80[27] = z.r;
            z80[28] = 0;  // Q: not in the GS block
            z80[29] = (z.flags & kSuppressInts) ? Z80_BOUNDARY_INT_SHADOW : Z80_BOUNDARY_NONE;
            z80[30] = z.iff1;
            z80[31] = z.iff2;
            z80[32] = z.im;
            z80[33] = (z.flags & kHalted) ? 1 : 0;
            z80[34] = 0;
            const size_t ramBytes = blob.size() - SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE;
            size_t pages = 0;
            for (const auto& [page, bytes] : stage.gsPages)
            {
                const size_t offset = static_cast<size_t>(page) * kGsPageSize;
                if (offset + kGsPageSize > ramBytes)
                {
                    report.Add("GSRP " + std::to_string(page), Outcome::Ignored, "beyond this card's RAM");
                    continue;
                }
                std::copy(bytes.begin(), bytes.end(), blob.begin() + SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE + offset);
                pages++;
            }
            card->TTDLoadState(blob.data());
            report.Add("GS", Outcome::Approximated,
                       "CPU, page, volumes, DAC levels and " + std::to_string(pages) +
                           " RAM pages; the card's timing and mailbox are not in the format");
        }
    }
    else if (!stage.gsPages.empty())
    {
        report.Add("GSRP", Outcome::Ignored, "no GS block");
    }
    if (stage.gs && !stage.gsPages.empty() && sound && sound->hasGeneralSound() &&
        sound->getGeneralSound()->implementation() == GSCardImplementation::NGS)
        report.Add("GSRP", Outcome::Approximated, "NeoGS keeps its own RAM, as for the GS block");

    if (stage.covox)
    {
        Covox* covox = (sound && sound->hasCovox()) ? sound->getCovox() : nullptr;
        if (covox)
        {
            covox->portDeviceOutMethod(0x00FB, *stage.covox);
            report.Add("COVX", Outcome::Applied, "level " + std::to_string(*stage.covox) + " on #FB");
        }
        else
        {
            report.Add("COVX", Outcome::Ignored, "no Covox fitted");
        }
    }
    if (stage.specDrum)
        report.Add("DRUM", Outcome::Ignored, "SpecDrum is not emulated");

    if (stage.mouse)
    {
        const uint8_t type = stage.mouse->type;
        const bool kempston = context->pMouse && context->pMouse->IsPresent();
        if (type == kMouseKempston && kempston)
            report.Add("AMXM", Outcome::Applied, "Kempston mouse");
        else if (type == kMouseNone)
            report.Add("AMXM", Outcome::Applied, "no mouse");
        else
            report.Add("AMXM", Outcome::Ignored, type == kMouseAmx ? "the AMX mouse is not emulated" : "no Kempston mouse fitted");
    }
    if (stage.keyboard)
    {
        if (stage.keyboard->flags & kKeyboardIssue2)
            report.Add("KEYB", Outcome::Ignored, "the Issue 2 keyboard is not emulated");
        else if (stage.keyboard->joystick != kKeyboardJoystickNone)
            report.Add("KEYB", Outcome::Approximated, "keyboard joysticks are not emulated");
        else
            report.Add("KEYB", Outcome::Applied);
    }
    if (stage.joysticks)
        report.Add("JOY", Outcome::Ignored, "joysticks are not emulated");
}

void LoaderSZX::CaptureMedia(EmulatorContext* context, Stage& stage)
{
    if (context->pBetaDisk && context->config.trdos_present)
    {
        for (uint8_t drive = 0; drive < 4; drive++)
        {
            const std::string slot = std::string("fdd.") + static_cast<char>('a' + drive);
            const std::optional<SlotInfo> info = LinkableMedium(context, slot, stage);
            if (!info)
                continue;
            const std::optional<uint8_t> type = DiskTypeOf(info->format);
            if (!type)
            {
                stage.warnings.push_back(slot + ": a " + info->format + " image cannot be linked in SZX (TRD, SCL, FDI, UDI only)");
                continue;
            }
            BetaDisk disk;
            disk.drive = drive;
            disk.type = *type;
            disk.flags = info->writeProtect ? kDiskWriteProtect : 0;
            disk.fileName = LinkFor(stage.folder, info->source);
            if (FDD* fdd = context->coreState.diskDrives[drive])
                disk.cylinder = static_cast<uint8_t>(std::max<int>(0, fdd->getTrack()));
            stage.betaDisks.push_back(std::move(disk));
        }
    }

    if (context->pUPD765)
    {
        stage.plus3 = Plus3{2, static_cast<uint8_t>(context->pUPD765->getMotor() ? 1 : 0)};
        for (uint8_t drive = 0; drive < 2; drive++)
        {
            const std::string slot = std::string("fdd.") + static_cast<char>('a' + drive);
            if (const std::optional<SlotInfo> info = LinkableMedium(context, slot, stage))
                stage.dskFiles.push_back(DskFile{0, drive, LinkFor(stage.folder, info->source)});
        }
    }

    if (context->pTape)
        if (const std::optional<SlotInfo> info = LinkableMedium(context, "tape", stage))
        {
            szx::Tape tape;
            tape.fileName = LinkFor(stage.folder, info->source);
            context->pTape->EnsureImageLoaded();
            if (const std::optional<TapePosition> position = context->pTape->GetPosition())
                tape.block = static_cast<uint16_t>(position->blockIndex);
            stage.tape = std::move(tape);
        }
}

void LoaderSZX::CaptureDevices(EmulatorContext* context, Stage& stage)
{
    SoundManager* sound = context->pSoundManager;
    GeneralSoundCard* card = (sound && sound->hasGeneralSound()) ? sound->getGeneralSound() : nullptr;
    // NeoGS replaces the GS, but the SZX GS block describes the classic card
    // (GS ROM 1.04, 32 KB pages): NeoGS state written there would load as a
    // wrong classic card in other emulators, so it is left out, with a note
    if (card && card->implementation() == GSCardImplementation::NGS)
        stage.warnings.push_back("NeoGS: the SZX GS block describes the classic GS card, so the NeoGS state is not saved");
    if (card && card->implementation() == GSCardImplementation::LLE)
    {
        std::vector<uint8_t> blob(card->TTDStateSize());
        card->TTDSaveState(blob.data());
        GeneralSound gs;
        const size_t ramBytes = blob.size() - SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE;
        gs.model = ramBytes >= 512 * 1024 ? 1 : 0;
        gs.upperPage = blob[kGsMpag];
        std::copy_n(blob.begin() + kGsVolume, 4, gs.volume.begin());
        std::copy_n(blob.begin() + kGsData, 4, gs.output.begin());
        const uint8_t* z80 = blob.data() + kGsCpu;
        auto word = [&](size_t i) { return static_cast<uint16_t>(z80[i * 2] | (z80[i * 2 + 1] << 8)); };
        Z80Regs& z = gs.cpu;
        z.af = word(0);
        z.bc = word(1);
        z.de = word(2);
        z.hl = word(3);
        z.af1 = word(4);
        z.bc1 = word(5);
        z.de1 = word(6);
        z.hl1 = word(7);
        z.ix = word(8);
        z.iy = word(9);
        z.sp = word(10);
        z.pc = word(11);
        z.memptr = word(12);
        z.i = z80[26];
        z.r = z80[27];
        z.iff1 = z80[30];
        z.iff2 = z80[31];
        z.im = z80[32];
        z.flags = static_cast<uint8_t>((z80[33] ? kHalted : 0) | (z80[29] == Z80_BOUNDARY_INT_SHADOW ? kSuppressInts : 0));
        gs.flags = z.flags;
        stage.gs = gs;
        // GS128: pages 0-3; GS512: 0-14 (the format's limit)
        const size_t pages = std::min<size_t>(ramBytes / kGsPageSize, gs.model ? 15 : 4);
        for (size_t page = 0; page < pages; page++)
        {
            const uint8_t* bytes = blob.data() + SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE + page * kGsPageSize;
            stage.gsPages[static_cast<uint8_t>(page)] = std::vector<uint8_t>(bytes, bytes + kGsPageSize);
        }
    }

    if (sound && sound->hasCovox())
    {
        uint8_t latches[4] = {};
        sound->getCovox()->getDacLatches(latches);
        stage.covox = latches[3];  // #FB
    }
    stage.keyboard = szx::Keyboard{};
    if (context->pMouse && context->pMouse->IsPresent())
        stage.mouse = szx::Mouse{kMouseKempston, {}, {}};
}

/// endregion </Media and devices>

bool LoaderSZX::Capture(EmulatorContext* context, Stage& stage, std::string& error)
{
    if (!context || !context->pCore || !context->pMemory)
    {
        error = "no machine to save";
        return false;
    }
    const std::optional<uint8_t> id = IdFor(context->config.mem_model, context->config.ramsize);
    if (!id)
    {
        error = "this model has no SZX machine id; save it as .z80 or .sna";
        return false;
    }
    const std::string folder = stage.folder;
    stage = Stage{};
    stage.folder = folder;
    stage.machineId = *id;

    Creator creator;
    creator.name = "unreal-ng";
    creator.data.assign(buildinfo::kGitCommit, buildinfo::kGitCommit + std::char_traits<char>::length(buildinfo::kGitCommit));
    stage.creator = creator;

    const Z80& cpu = *context->pCore->GetZ80();
    Z80Regs z;
    z.af = cpu.af;
    z.bc = cpu.bc;
    z.de = cpu.de;
    z.hl = cpu.hl;
    z.af1 = cpu.alt.af;
    z.bc1 = cpu.alt.bc;
    z.de1 = cpu.alt.de;
    z.hl1 = cpu.alt.hl;
    z.ix = cpu.ix;
    z.iy = cpu.iy;
    z.sp = cpu.sp;
    z.pc = cpu.pc;
    z.i = cpu.i;
    z.r = static_cast<uint8_t>((cpu.r_low & 0x7F) | (cpu.r_hi & 0x80));
    z.iff1 = cpu.iff1 ? 1 : 0;
    z.iff2 = cpu.iff2 ? 1 : 0;
    z.im = static_cast<uint8_t>(cpu.im & 0x03);
    z.memptr = cpu.memptr;
    const bool shadow = cpu.boundary == Z80_BOUNDARY_INT_SHADOW || cpu.boundary == Z80_BOUNDARY_PREFIX_DD ||
                        cpu.boundary == Z80_BOUNDARY_PREFIX_FD;
    z.flags = static_cast<uint8_t>((cpu.halted ? kHalted : (shadow ? kSuppressInts : 0)) | (cpu.q ? kFset : 0));

    const FrameGeometry g = Geometry(context);
    z.cyclesStart = IntCountFromFramePosition(context, cpu.t);
    z.holdIntReqCycles = (z.cyclesStart < g.intLength && !cpu.int_acked_in_pulse)
                             ? static_cast<uint8_t>(g.intLength - z.cyclesStart)
                             : 0;
    stage.z80 = z;

    const EmulatorState& state = context->emulatorState;
    SpecRegs spec;
    spec.border = static_cast<uint8_t>(state.border_attr & 0x07);
    spec.port7FFD = HasAy(*id) ? state.p7FFD : 0;
    spec.port1FFDorEFF7 = HasPort1FFD(*id) ? state.p1FFD : (HasPortEFF7(*id) ? state.pEFF7 : 0);
    spec.portFE = state.pFE;
    stage.spec = spec;

    Memory& memory = *context->pMemory;
    for (uint8_t page : PagesOf(*id))
    {
        const uint8_t* bytes = memory.RAMPageAddress(page);
        if (bytes)
            stage.pages[page] = std::vector<uint8_t>(bytes, bytes + kPageSize);
    }

    SoundChip_AY8910* ay = (HasAy(*id) && context->pSoundManager) ? context->pSoundManager->getAYChip(0) : nullptr;
    if (ay)
    {
        Ay block;
        block.currentRegister = static_cast<uint8_t>(ay->getCurrentRegisterIndex() & 0x0F);
        const uint8_t* registers = ay->getRegisters();
        for (size_t i = 0; i < 16; i++)
            block.registers[i] = registers[i];
        stage.ay = block;
    }

    // B128 when the machine has the interface fitted ([BETA128] Beta128=1)
    if (context->pBetaDisk && context->config.trdos_present)
    {
        const WD1793& fdc = *context->pBetaDisk;
        Beta128 beta;
        beta.flags = kBetaConnected | ((state.flags & CF_TRDOS) ? kBetaPaged : 0) |
                     (fdc.isStepDirectionIn() ? 0 : kBetaSeekLower);
        beta.drives = 4;
        beta.system = fdc.getBeta128Register();
        beta.track = fdc.getTrackRegister();
        beta.sector = fdc.getSectorRegister();
        beta.data = fdc.getDataRegister();
        beta.status = fdc.getStatusRegister();
        stage.beta = beta;
    }

    CaptureMedia(context, stage);
    CaptureDevices(context, stage);
    return true;
}

/// endregion </Capture>
