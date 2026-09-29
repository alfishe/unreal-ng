#include "loaders/snapshot/szx/loaderszx.h"

#include <vector>

#include "common/filehelper.h"
#include "emulator/buildinfo.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
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
    return Commit(_context, stage, _report, _error);
}

bool LoaderSZX::save()
{
    Stage stage;
    if (!Capture(_context, stage, _error))
        return false;
    std::vector<uint8_t> bytes = SzxWriter::Write(stage);
    if (!FileHelper::SaveBufferToFile(_path, bytes.data(), bytes.size()))
    {
        _error = "cannot write '" + _path + "'";
        return false;
    }
    return true;
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
        error = "the snapshot is for " + std::string(machine.model == MM_PENTAGON ? "a Pentagon " + std::to_string(machine.ramKb) + "K"
                                                                                 : "machine id " + std::to_string(stage.machineId)) +
                ", the running machine is another model: switch the model first";
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

    // AY: all registers, then the selected one
    if (stage.ay)
    {
        SoundChip_AY8910* ay = context->pSoundManager ? context->pSoundManager->getAYChip(0) : nullptr;
        if (ay)
        {
            for (uint8_t reg = 0; reg < 16; reg++)
                ay->writeRegister(reg, stage.ay->registers[reg]);
            ay->setRegister(stage.ay->currentRegister);
            report.Add("AY", Outcome::Applied, stage.ay->flags ? "Fuller / Melodik flags ignored" : "");
        }
        else
        {
            report.Add("AY", Outcome::Ignored, "this machine has no AY");
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

    for (const auto& [name, size] : stage.otherBlocks)
        report.Add(name, Outcome::Ignored, "not applied yet");

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

    // Frame position: counted from the INT in the file
    const FrameGeometry g = Geometry(context);
    const uint32_t fromInt = z.cyclesStart % g.frame;
    cpu.t = FramePositionFromIntCount(context, fromInt);
    // Inside the INT window with nothing left to accept: the INT was served
    cpu.int_acked_in_pulse = (fromInt < g.intLength && z.holdIntReqCycles == 0) ? 1 : 0;
    report.Add("Z80R", Outcome::Applied,
               "t " + std::to_string(cpu.t) + " (" + std::to_string(fromInt) + " after the INT)" +
                   ((stage.versionMajor << 8 | stage.versionMinor) < 0x0104 ? ", MEMPTR from chBitReg" : ""));
}

/// endregion </Commit>

/// region <Capture>

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
    stage = Stage{};
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
    return true;
}

/// endregion </Capture>
