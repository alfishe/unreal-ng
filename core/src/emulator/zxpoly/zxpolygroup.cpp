#include "stdafx.h"

#include "zxpolygroup.h"

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "emulator/zxpoly/zxpolyportinterceptor.h"
#include "emulator/zxpoly/zxpolyscreencomposer.h"
#include "emulator/zxpoly/zxpolyworkers.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/tape/tape.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/iturbosounddevice.h"
#include "emulator/sound/covox.h"
#include "loaders/snapshot/loaderzxp.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

namespace
{
    constexpr const char* TTD_UNAVAILABLE =
        "ZX-Poly machines do not support time travel yet: the four modules need one shared timeline";

    constexpr uint16_t PORT_ZXPOLY_MAIN = 0x3D00;
    constexpr uint8_t MAIN_NWAIT = 0x01;
    constexpr uint8_t MAIN_RESET = 0x02;
    constexpr uint8_t MAIN_LOCK = 0x80;

    constexpr size_t OVERLAY_SIZE = 8 * PAGE_SIZE;   // one 128K module

    /// The devices whose runtime state makes up a machine besides CPU and RAM:
    /// the same set a TTD checkpoint carries (TimeTravelManager::
    /// RegisterModelPeripherals), keyed by peripheral id. Model latches come
    /// from serializers the port decoder creates; they are kept in `owned`
    std::map<ttd::PeripheralId, ttd::TTDSerializable*> CollectDevices(
        EmulatorContext* context, std::vector<std::unique_ptr<ttd::TTDSerializable>>& owned)
    {
        std::map<ttd::PeripheralId, ttd::TTDSerializable*> devices;
        auto add = [&](ttd::PeripheralId id, ttd::TTDSerializable* device) {
            if (device)
                devices[id] = device;
        };

        if (SoundManager* sound = context->pSoundManager)
        {
            if (ITurboSoundDevice* turboSound = sound->getTurboSound())
                add(turboSound->TTDPeripheralId(), turboSound);
            add(ttd::PeripheralId::Covox, sound->getCovox());
            if (GeneralSoundCard* gs = sound->getGeneralSound())
                add(gs->TTDPeripheralId(), gs);
        }
        add(ttd::PeripheralId::Tape, context->pTape);
        add(ttd::PeripheralId::KempstonMouse, context->pMouse);
        add(ttd::PeripheralId::BetaDisk, context->pBetaDisk);

        if (context->pPortDecoder)
        {
            for (auto& serializer : context->pPortDecoder->CreateTTDSerializers())
            {
                if (!serializer)
                    continue;
                add(serializer->TTDPeripheralId(), serializer.get());
                owned.push_back(std::move(serializer));
            }
        }
        return devices;
    }

    /// Module register port: (module << 12) | (register << 8) | #FF
    bool IsModuleRegisterPort(uint16_t port, size_t& module, size_t& reg)
    {
        if ((port & 0xFFu) != 0xFFu)
            return false;
        const uint8_t high = static_cast<uint8_t>(port >> 8);
        if ((high & 0xCCu) != 0)
            return false;
        module = (high >> 4) & 0x03u;
        reg = high & 0x03u;
        return true;
    }
}

/// region <Constructors / destructors>

ZXPolyGroup::ZXPolyGroup(std::string symbolicPrefix)
    : _prefix(std::move(symbolicPrefix)), _workers(std::make_unique<ZXPolyWorkers>(MODULES - 1))
{
}

ZXPolyGroup::~ZXPolyGroup()
{
    Destroy();
}

/// endregion </Constructors / destructors>

/// region <Lifecycle>

const std::vector<ZXPolyGroup::Configuration>& ZXPolyGroup::Configurations()
{
    // 48K runs the synchronized quad and replicated 48K software; ZX-Poly
    // editions (.zxp, the Test ROM, multiloader disks) need a 128K-class model
    static const std::vector<Configuration> configurations = {
        {"ZXPOLY-48K", "ZXPoly-48k", "48K"},
        {"ZXPOLY-128K", "ZXPoly-128k", "128K"},
        {"ZXPOLY-PENTAGON", "ZXPoly-Pentagon", "PENTAGON"},
    };
    return configurations;
}

const ZXPolyGroup::Configuration* ZXPolyGroup::FindConfiguration(const std::string& name)
{
    const std::string upper = StringHelper::ToUpper(name);
    for (const Configuration& configuration : Configurations())
    {
        if (upper == configuration.name)
            return &configuration;
    }
    return nullptr;
}

bool ZXPolyGroup::Create(const std::string& modelOrConfiguration, std::string* error,
                         const std::function<void(CONFIG&)>& configOverride)
{
    const Configuration* configuration = FindConfiguration(modelOrConfiguration);
    const std::string model = configuration ? configuration->baseModel : modelOrConfiguration;

    Destroy();

    EmulatorManager* manager = EmulatorManager::GetInstance();
    for (size_t m = 0; m < MODULES; m++)
    {
        std::string createError;
        // The master carries the group's name; the slaves are named after it
        const std::string id = m == 0 ? _prefix : StringHelper::Format("%s-cpu%zu", _prefix.c_str(), m);
        _instances[m] = manager->CreateEmulatorWithModel(id, model, LoggerLevel::LogError, &createError, configOverride);
        if (!_instances[m])
        {
            if (error)
                *error = StringHelper::Format("module %zu: cannot create model '%s': %s", m, model.c_str(),
                                              createError.c_str());
            Destroy();
            return false;
        }
    }

    for (size_t m = 0; m < MODULES; m++)
    {
        EmulatorContext* context = GetContext(m);

        // Only the master is heard and shown: slaves skip audio and their own
        // RGBA rendering (turbo mode; the composer reads their video RAM),
        // and are not listed as machines of their own
        if (m > 0)
        {
            _instances[m]->SetHiddenGroupMember(true);
            _instances[m]->EnableTurboMode();
            if (context->pSoundManager)
                context->pSoundManager->mute();
        }

        // Keys and the mouse reach the members only through the group
        // (frame-boundary aligned)
        if (context->pKeyboard)
            context->pKeyboard->SetHostInputGated(true);
        if (context->pMouse)
            context->pMouse->SetHostInputGated(true);

        // The ZX-Poly platform ports sit in front of the model's port decoder
        _interceptors[m] = std::make_unique<ZXPolyPortInterceptor>(*this, m);
        context->pCore->GetZ80()->portInterceptor = _interceptors[m].get();
    }

    // Time travel of one member would split it from the others: ZX-Poly
    // machines have none yet (prototype-results.md §9). The group's own
    // timeline (StartRecording) lifts it while it records
    for (size_t m = 0; m < MODULES; m++)
    {
        if (ttd::TimeTravelManager* ttd = GetContext(m)->pTimeTravelManager)
            ttd->SetUnavailableReason(TTD_UNAVAILABLE);
    }

    // Automation input of the machine (WebAPI, MCP, CLI, Lua, Python: keys,
    // the Kempston mouse, a keyboard reset) enters through the master's TTD
    // live-input gateway. Handed to one member it would split the machine, so
    // the group queues it and gives it to all four at one frame boundary.
    // Other events (General Sound stimuli) stay the master's own
    if (ttd::TimeTravelManager* ttd = GetContext(0)->pTimeTravelManager)
    {
        ttd->SetLiveInputInterceptor([this](const ttd::TTDInputEvent& ev) {
            switch (ev.kind)
            {
                case ttd::TTDInputKind::Key:
                    QueueInput({ev.pressed ? InputOp::KeyDown : InputOp::KeyUp, static_cast<ZXKeysEnum>(ev.key), 0, 0});
                    return true;
                case ttd::TTDInputKind::MouseMove:
                    QueueInput({InputOp::MouseMove, ZXKEY_NONE, ev.dx, ev.dy});
                    return true;
                case ttd::TTDInputKind::MouseButtons:
                    QueueInput({InputOp::MouseButtons, ZXKEY_NONE, ev.buttonMask, 0});
                    return true;
                case ttd::TTDInputKind::MouseWheel:
                    QueueInput({InputOp::MouseWheel, ZXKEY_NONE, ev.wheelSteps, 0});
                    return true;
                case ttd::TTDInputKind::MouseCounters:
                    QueueInput({InputOp::MouseCounters, ZXKEY_NONE, ev.dx, ev.dy});
                    return true;
                case ttd::TTDInputKind::KeyboardReset:
                    QueueInput({InputOp::KeyboardReset, ZXKEY_NONE, 0, 0});
                    return true;
                default:
                    return false;
            }
        });
    }

    // Host speed changes of the machine go through the input queue: all four
    // take them at one frame boundary
    _instances[0]->SetSpeedChangeInterceptor([this](uint8_t multiplier) {
        QueueInput({InputOp::Speed, ZXKEY_NONE, multiplier, 0});
        return true;
    });

    InstallMasterM1Hook();
    InstallSlaveM1Hooks();
    ResetPlatformState();
    return true;
}

void ZXPolyGroup::Destroy()
{
    DetachFromMaster();
    WaitForSlaves();

    EmulatorManager* manager = EmulatorManager::GetInstance();
    for (size_t m = 0; m < MODULES; m++)
    {
        if (_instances[m])
        {
            _instances[m]->SetSpeedChangeInterceptor(nullptr);
            if (EmulatorContext* context = GetContext(m); context && context->pTimeTravelManager)
                context->pTimeTravelManager->SetLiveInputInterceptor(nullptr);
            if (EmulatorContext* context = GetContext(m); context && context->pCore)
            {
                context->pCore->GetZ80()->portInterceptor = nullptr;
                context->pCore->GetZ80()->busTraceHook = nullptr;
                context->pCore->GetZ80()->m1TraceHook = nullptr;
            }
            manager->RemoveEmulator(_instances[m]->GetUUID());
        }
        _instances[m].reset();
        _interceptors[m].reset();
    }
}

EmulatorContext* ZXPolyGroup::GetContext(size_t module) const
{
    return _instances[module] ? _instances[module]->GetContext() : nullptr;
}

void ZXPolyGroup::ResetPlatformState()
{
    // Hardware reset values: #3D00 = 0 (slaves in WAIT, unlocked), module i's
    // R0 = i << 1 (disjoint 128K heap windows), R1-R3 = 0
    _port3D00 = 0;
    for (size_t m = 0; m < MODULES; m++)
    {
        _regs[m] = {static_cast<uint8_t>(m << 1), 0, 0, 0};
        _overlay[m].assign(m == 0 ? 0 : OVERLAY_SIZE, -1);
        _stopWait[m] = false;
        _wasHalted[m] = false;
    }
    _locked = false;
    _slavesRunning = false;
    UpdateIntGates();
    _lockedThisFrame = false;
    _boundaryChecked = false;
}

/// endregion </Lifecycle>

/// region <Loading and replication>

bool ZXPolyGroup::LoadZXP(const std::string& path, std::string* error)
{
    WaitForSlaves();
    if (!IsCreated())
    {
        if (error)
            *error = "group not created";
        return false;
    }
    if (!HasPaging128(error))
        return false;

    LoaderZXP loader(GetContext(0)->pModuleLogger, path);
    if (!loader.Parse())
    {
        if (error)
            *error = loader.GetError();
        return false;
    }

    std::array<EmulatorContext*, ZXPSnapshot::MODULE_COUNT> contexts{};
    for (size_t m = 0; m < MODULES; m++)
        contexts[m] = GetContext(m);

    if (!loader.Apply(contexts))
    {
        if (error)
            *error = loader.GetError();
        return false;
    }

    const ZXPSnapshot& snapshot = loader.GetSnapshot();
    ResetPlatformState();
    _port3D00 = snapshot.port3D00;
    for (size_t m = 0; m < MODULES; m++)
        _regs[m] = snapshot.modules[m].reg;
    _locked = true;
    UpdateIntGates();

    // The loader replaced every instance's state: start the frame again from
    // it, as Emulator::LoadSnapshot does
    for (auto& instance : _instances)
        instance->RestartFrame();

    _lastMasterFrame = GetContext(0)->emulatorState.frame_counter;
    return true;
}

bool ZXPolyGroup::HasPaging128(std::string* error) const
{
    // The ZX-Poly board is 128K-class: editions page through #7FFD, and the
    // heap window addresses the modules' 128K pages
    if (GetContext(0)->config.mem_model != MM_SPECTRUM48)
        return true;
    if (error)
        *error = "ZX-Poly editions need a 128K-class model (128K, Pentagon); a 48K group runs replicated 48K "
                 "software only";
    return false;
}

bool ZXPolyGroup::LoadMedia(const std::string& path, std::string* error)
{
    WaitForSlaves();
    if (path.empty())
        return true;

    std::string extension;
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos)
        extension = StringHelper::ToLower(path.substr(dot + 1));

    if (extension == "zxp")
        return LoadZXP(path, error);
    if (extension == "prom")
        return LoadPROM(path, error);
    return BootDisk(path, error);
}

ZXPolyGroup::Status ZXPolyGroup::GetStatus() const
{
    Status status;
    for (size_t m = 0; m < MODULES; m++)
        status.memberIds[m] = _instances[m] ? _instances[m]->GetId() : std::string();
    status.locked = _locked;
    status.slavesRunning = _slavesRunning;
    status.parallelSlaves = _parallelSlaves;
    status.pipelinedSlaves = _pipelining;
    status.port3D00 = _port3D00;
    status.videoMode = GetVideoMode();
    status.registers = _regs;
    status.divergence = CheckLockstep();
    return status;
}

bool ZXPolyGroup::LoadPROM(const std::string& path, std::string* error)
{
    WaitForSlaves();
    if (!IsCreated())
    {
        if (error)
            *error = "group not created";
        return false;
    }
    if (!HasPaging128(error))
        return false;

    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.empty() || data.size() > 4 * PAGE_SIZE)
    {
        if (error)
            *error = StringHelper::Format("'%s': a ZX-Poly ROM image is 1 to 65536 bytes", path.c_str());
        return false;
    }

    // Up to four 16K parts; module i gets part (i mod parts) as its only ROM,
    // so every ROM page the model selects holds it (unused tail = #FF)
    const size_t parts = (data.size() + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t m = 0; m < MODULES; m++)
    {
        const size_t part = m % parts;
        const size_t length = std::min<size_t>(PAGE_SIZE, data.size() - part * PAGE_SIZE);
        Memory& memory = *GetContext(m)->pMemory;
        for (uint8_t romPage = 0; romPage < 4; romPage++)
        {
            uint8_t* rom = memory.ROMPageHostAddress(romPage);
            if (rom == nullptr)
                continue;
            std::memset(rom, 0xFF, PAGE_SIZE);
            std::memcpy(rom, data.data() + part * PAGE_SIZE, length);
        }
    }

    // Power on: every CPU from #0000, the platform in its reset state
    for (auto& instance : _instances)
        instance->Reset();
    ResetPlatformState();
    _lastMasterFrame = GetContext(0)->emulatorState.frame_counter;
    return true;
}

bool ZXPolyGroup::BootDisk(const std::string& path, std::string* error)
{
    WaitForSlaves();
    if (!IsCreated())
    {
        if (error)
            *error = "group not created";
        return false;
    }

    ResetPlatformState();

    // Slaves get the same disk: after the lock they run the game code too and
    // must read the same sectors if it loads more
    for (size_t m = 1; m < MODULES; m++)
    {
        std::string diskError;
        if (!_instances[m]->LoadDisk(path, 0, &diskError))
        {
            if (error)
                *error = StringHelper::Format("module %zu: %s", m, diskError.c_str());
            return false;
        }
    }

    const Emulator::DiskAutostartResult started = _instances[0]->AutostartDisk(path, 0);
    if (!started.mounted || !started.started)
    {
        if (error)
            *error = started.message;
        return false;
    }

    ResetSlaveMachines();
    _lastMasterFrame = GetContext(0)->emulatorState.frame_counter;
    return true;
}

void ZXPolyGroup::CopyMasterState()
{
    _boundaryChecked = false;    // copies of the master: compared as they stand until the next boundary
    EmulatorContext* master = GetContext(0);
    Z80& masterCpu = *master->pCore->GetZ80();
    const size_t ramPages = std::max<size_t>(8, master->config.ramsize / 16);

    for (size_t m = 1; m < MODULES; m++)
    {
        EmulatorContext* slave = GetContext(m);
        Z80& slaveCpu = *slave->pCore->GetZ80();

        // RAM: every page the model has (this also replaces the power-on noise)
        for (size_t page = 0; page < ramPages; page++)
        {
            std::memcpy(slave->pMemory->RAMPageAddress(static_cast<uint16_t>(page)),
                        master->pMemory->RAMPageAddress(static_cast<uint16_t>(page)), PAGE_SIZE);
        }

        // CPU: the architectural registers, frame position and interrupt state
        static_cast<Z80Registers&>(slaveCpu) = static_cast<const Z80Registers&>(masterCpu);
        slaveCpu.prev_pc = masterCpu.prev_pc;
        slaveCpu.m1_pc = masterCpu.m1_pc;
        slaveCpu.int_pending = masterCpu.int_pending;
        slaveCpu.int_acked_in_pulse = masterCpu.int_acked_in_pulse;
        slaveCpu.int_gate = masterCpu.int_gate;
        slaveCpu.halt_cycle = masterCpu.halt_cycle;

        // Devices: disk controller and drives, tape, sound chips, mouse, model
        // latches - so an IN after the lock reads what the master's would
        std::vector<std::unique_ptr<ttd::TTDSerializable>> ownedMaster;
        std::vector<std::unique_ptr<ttd::TTDSerializable>> ownedSlave;
        const auto masterDevices = CollectDevices(master, ownedMaster);
        const auto slaveDevices = CollectDevices(slave, ownedSlave);
        std::vector<uint8_t> blob;
        for (const auto& [id, source] : masterDevices)
        {
            const auto target = slaveDevices.find(id);
            if (target == slaveDevices.end() || source->TTDStateSize() != target->second->TTDStateSize())
                continue;
            blob.resize(source->TTDStateSize());
            source->TTDSaveState(blob.data());
            target->second->TTDLoadState(blob.data());
        }

        // Paging latches and the TR-DOS session, then remap the banks
        EmulatorState& to = slave->emulatorState;
        const EmulatorState& from = master->emulatorState;
        to.p1FFD = from.p1FFD;
        to.pFE = from.pFE;
        to.border_attr = from.border_attr;
        to.flags = from.flags;
        to.frame_counter = from.frame_counter;
        to.t_states = from.t_states;   // the clock devices (WD1793, tape) time themselves by
        slave->pPortDecoder->UnlockPaging();
        slave->pPortDecoder->DecodePortOut(0x7FFD, from.p7FFD, slaveCpu.pc);
        to.p7FFD = from.p7FFD;
        slave->pMemory->UpdateZ80Banks();
    }

}

void ZXPolyGroup::ReplicateFromMaster()
{
    WaitForSlaves();
    // A stock program mirrored into the slaves: they get no IO writes of
    // their own (R0 D4), as a ZX-Poly edition's loader sets for its slaves
    CopyMasterState();
    for (size_t m = 1; m < MODULES; m++)
        _regs[m][0] |= 0x10;
    _locked = true;
    UpdateIntGates();
}

void ZXPolyGroup::PerformLock()
{
    // Runs inside the master's locking OUT (#3D00), at its IORQ T-state. The
    // rest of that instruction only charges T-states (OUT (C),r and OUT (n),A:
    // 3 T after the port write), so the master's state here is its state
    // after the instruction, less those 3 T - the slaves get them added below.
    constexpr unsigned OUT_TAIL_T = 3;
    const bool resetRequested = (_port3D00 & MAIN_RESET) != 0;
    const bool slavesWereParked = !_slavesRunning;
    _slavesRunning = false;

    if (slavesWereParked)
    {
        // Multiloader lock: the slaves waited since reset and hold only what
        // the IO window streamed into them. Every byte they will use came
        // through the window, so the master's state plus that overlay is
        // exactly their state (quad-instance-architecture.md §4.3)
        std::array<uint8_t, MODULES> own7FFD{};
        for (size_t m = 1; m < MODULES; m++)
            own7FFD[m] = GetContext(m)->emulatorState.p7FFD;

        if (resetRequested)
            LocalReset(0);    // the master; its reset command JP included

        MountMasterDisksOnSlaves();
        CopyMasterState();

        for (size_t m = 1; m < MODULES; m++)
        {
            EmulatorContext* slave = GetContext(m);
            Z80& slaveCpu = *slave->pCore->GetZ80();
            slaveCpu.tt += OUT_TAIL_T * slaveCpu.rate;
            if (resetRequested)
            {
                slaveCpu.pc = ResetCommandTarget(m);
                slaveCpu.memptr = slaveCpu.pc;
            }

            // The slave's own paging latch
            slave->pPortDecoder->UnlockPaging();
            slave->pPortDecoder->DecodePortOut(0x7FFD, own7FFD[m], slaveCpu.pc);
            slave->emulatorState.p7FFD = own7FFD[m];
            slave->pMemory->UpdateZ80Banks();

            // Plane data the loader streamed through the IO window
            const std::vector<int16_t>& overlay = _overlay[m];
            for (size_t offset = 0; offset < overlay.size(); offset++)
            {
                if (overlay[offset] >= 0)
                {
                    uint8_t* page = slave->pMemory->RAMPageAddress(static_cast<uint16_t>(offset / PAGE_SIZE));
                    page[offset % PAGE_SIZE] = static_cast<uint8_t>(overlay[offset]);
                }
            }
        }
    }
    else
    {
        // The slaves already ran their own code (MIMD before the lock): they
        // keep their state; a reset restarts every CPU from its own command
        if (resetRequested)
        {
            for (size_t m = 0; m < MODULES; m++)
                LocalReset(m);
        }
    }

    _locked = true;
    _lockedThisFrame = true;
    UpdateIntGates();

    // The reset command registers are consumed by the reset
    if (resetRequested)
    {
        for (auto& regs : _regs)
            regs[1] = regs[2] = regs[3] = 0;
    }
}

void ZXPolyGroup::LocalReset(size_t module)
{
    // A CPU-only reset (memory and devices untouched). The module then fetches
    // its first three opcode bytes from R1, R2, R3: every known program puts
    // JP nn there, executed as a 10 T-state instruction
    constexpr unsigned OUT_TAIL_T = 3;
    constexpr unsigned INJECTED_JP_T = 10;

    Z80& cpu = *GetContext(module)->pCore->GetZ80();
    const bool jump = _regs[module][1] == 0xC3;
    cpu.pc = ResetCommandTarget(module);
    cpu.memptr = cpu.pc;
    cpu.sp = 0xFFFF;
    cpu.af = 0xFFFF;
    cpu.i = 0;
    cpu.r_low = jump ? 1 : 0;
    cpu.r_hi = 0;
    cpu.im = 0;
    cpu.iff1 = 0;
    cpu.iff2 = 0;
    cpu.halted = 0;
    cpu.ClearInterruptRequests();
    _stopWait[module] = false;
    _wasHalted[module] = false;

    if (module == 0)
    {
        // Inside the master's own OUT: the rest of that instruction follows
        if (jump)
            cpu.tt += INJECTED_JP_T * cpu.rate;
    }
    else
    {
        // A slave restarts at the moment of the master's write
        AlignSlaveClock(module, OUT_TAIL_T + (jump ? INJECTED_JP_T : 0));
    }

    _regs[module][1] = _regs[module][2] = _regs[module][3] = 0;
}

void ZXPolyGroup::AlignSlaveClock(size_t module, unsigned extraT)
{
    // One frame clock for the machine (zxpoly clocks frames by CPU0 alone): a
    // slave that starts or restarts takes the master's frame position
    EmulatorContext* master = GetContext(0);
    EmulatorContext* slave = GetContext(module);
    const Z80& masterCpu = *master->pCore->GetZ80();
    Z80& slaveCpu = *slave->pCore->GetZ80();

    slave->emulatorState.frame_counter = master->emulatorState.frame_counter;
    slave->emulatorState.t_states = master->emulatorState.t_states;
    slaveCpu.tt = masterCpu.tt + extraT * slaveCpu.rate;
}

void ZXPolyGroup::OnMainPortWrite(uint8_t value)
{
    // Master only, unlocked (the caller checked)
    constexpr unsigned OUT_TAIL_T = 3;
    const bool wasRunning = _slavesRunning;
    _port3D00 = value;

    if (value & MAIN_LOCK)
    {
        PerformLock();
        return;
    }

    // D1: local reset of every CPU module (memory and devices untouched)
    if (value & MAIN_RESET)
    {
        for (size_t m = 0; m < MODULES; m++)
            LocalReset(m);
    }

    // D0 (nWAIT): the slaves run from the end of this instruction
    _slavesRunning = (value & MAIN_NWAIT) != 0;
    if (_slavesRunning && !wasRunning)
    {
        for (size_t m = 1; m < MODULES; m++)
            AlignSlaveClock(m, OUT_TAIL_T);
    }
}

uint16_t ZXPolyGroup::ResetCommandTarget(size_t module) const
{
    // After a local reset the first three opcode fetches at #0000 return R1,
    // R2, R3. Every known program puts JP nn there (#C3, lo, hi); three zero
    // registers are three NOPs from #0000
    const std::array<uint8_t, 4>& r = _regs[module];
    return r[1] == 0xC3 ? static_cast<uint16_t>(r[2] | (r[3] << 8)) : 0x0000;
}

void ZXPolyGroup::UpdateIntGates()
{
    // Common frame INT: the slaves see it only while the machine is locked;
    // the master, while #3D00 and #7FFD are unlocked, only with #7FFD D7 = 0
    // (a 128K-class latch - on bigger Pentagons D7 is a memory bit)
    if (!IsCreated())
        return;

    const EmulatorContext* master = GetContext(0);
    const uint8_t p7FFD = master->emulatorState.p7FFD;
    const bool latchIsPlain128 = master->config.ramsize <= 128;
    GetContext(0)->pCore->GetZ80()->frameIntMasked =
        !_locked && latchIsPlain128 && (p7FFD & 0x20u) == 0 && (p7FFD & 0x80u) != 0;
    for (size_t m = 1; m < MODULES; m++)
        GetContext(m)->pCore->GetZ80()->frameIntMasked = !_locked;
}

void ZXPolyGroup::RaiseModuleInt(size_t module)
{
    // A local INT pulse, as long as the frame INT
    Z80& cpu = *GetContext(module)->pCore->GetZ80();
    cpu.RaiseLocalInt(GetContext(module)->config.intlen);
}

void ZXPolyGroup::RaiseModuleNmi(size_t module)
{
    // R1 D4 of the target masks local NMIs (COPY2CPU sets it: every byte it
    // streams would pulse one). A module in WAIT misses the pulse: it lasts
    // 16 T of the module's own clock, which WAIT does not advance through an
    // instruction boundary (zxpoly; the Test ROM streams its CPU test into a
    // waiting CPU1 with NMI unmasked and relies on it)
    const bool waiting = _stopWait[module] || (module != 0 && !_locked && !_slavesRunning);
    if (!waiting && (_regs[module][1] & 0x10u) == 0)
        GetContext(module)->pCore->GetZ80()->RequestNonMaskedInterrupt();
}

void ZXPolyGroup::OnModuleHalted(size_t module)
{
    // Halt notification (unlocked only): the halting module's R1 selects the
    // targets (D0-D3 = CPU0-CPU3) and the signal (D6 INT, D7 NMI)
    const uint8_t r1 = _regs[module][1];
    for (size_t target = 0; target < MODULES; target++)
    {
        if ((r1 & (1u << target)) == 0)
            continue;
        if (r1 & 0x40u)
            RaiseModuleInt(target);
        if (r1 & 0x80u)
            RaiseModuleNmi(target);
    }
}

uint16_t ZXPolyGroup::StopAddress(size_t module) const
{
    // R2/R3. Zero disables it: zxpoly would stop any M1 at #0000 (an RST 0
    // after a reset cleared the registers) - no program relies on that
    return static_cast<uint16_t>(_regs[module][2] | (_regs[module][3] << 8));
}

size_t ZXPolyGroup::GetOverlayBytes(size_t module) const
{
    return static_cast<size_t>(
        std::count_if(_overlay[module].begin(), _overlay[module].end(), [](int16_t v) { return v >= 0; }));
}

/// endregion </Loading and replication>

/// region <Platform ports>

bool ZXPolyGroup::IsTRDOSActive(size_t module) const
{
    return (GetContext(module)->emulatorState.flags & CF_TRDOS) != 0;
}

uint8_t ZXPolyGroup::ModuleIdentity(size_t module) const
{
    // #3D00 read: module index, heap window, IO-mapped flag, write-disable flags
    const uint8_t r0 = _regs[module][0];
    const size_t mapped = (_port3D00 >> 5) & 0x03u;
    return static_cast<uint8_t>(module | ((r0 & 0x07u) << 5) | (mapped == module && module != 0 ? 0x10u : 0u) |
                                ((r0 & 0x08u) ? 0x08u : 0u) | ((r0 & 0x10u) ? 0x04u : 0u));
}

uint8_t ZXPolyGroup::ModuleStatus(size_t module) const
{
    // R0 read: D0 HALT, D1 WAIT, D2-D7 the packed address of the last M1
    // (zxpoly ZxPolyModule.packAddress: A1, A2, A8, A12, A14, A15)
    const Z80& cpu = *GetContext(module)->pCore->GetZ80();
    const uint16_t a = cpu.m1_pc;
    const uint8_t packed = static_cast<uint8_t>(((a >> 1) & 0x01u) | ((a >> 1) & 0x02u) | ((a >> 6) & 0x04u) |
                                                ((a >> 9) & 0x08u) | ((a >> 10) & 0x10u) | ((a >> 10) & 0x20u));
    const bool waiting = _stopWait[module] || (module != 0 && !_locked && !_slavesRunning);
    return static_cast<uint8_t>((cpu.halted ? 0x01u : 0u) | (waiting ? 0x02u : 0u) | (packed << 2));
}

size_t ZXPolyGroup::WindowOffset(size_t module, uint16_t address) const
{
    // The target module's own mapping; page 0 is always RAM0 through the window
    size_t page;
    switch (address >> 14)
    {
        case 0: page = 0; break;
        case 1: page = 5; break;
        case 2: page = 2; break;
        default: page = GetContext(module)->emulatorState.p7FFD & 0x07u; break;
    }
    return page * PAGE_SIZE + (address & 0x3FFFu);
}

uint8_t* ZXPolyGroup::ModuleRam(size_t module, uint16_t address) const
{
    const size_t offset = WindowOffset(module, address);
    return GetContext(module)->pMemory->RAMPageAddress(static_cast<uint16_t>(offset / PAGE_SIZE)) +
           offset % PAGE_SIZE;
}

bool ZXPolyGroup::OnPort7FFDWrite(size_t module, uint8_t value)
{
    // The model pages as usual; on ZX-Poly, while #3D00 is unlocked, #7FFD D6
    // also puts RAM0 at #0000 in place of the ROM
    EmulatorContext* context = GetContext(module);
    context->pPortDecoder->DecodePortOut(0x7FFD, value, context->pCore->GetZ80()->m1_pc);
    if (!_locked && (context->emulatorState.p7FFD & 0x40u))
        context->pMemory->SetRAMPageToBank0(0);
    if (module == 0)
        UpdateIntGates();    // #7FFD D7 gates the master's INT while unlocked
    return true;
}

bool ZXPolyGroup::OnPortOut(size_t module, uint16_t port, uint8_t value)
{
    // #3D00 is never a ULA #FE write on ZX-Poly. Writes: master only, unlocked
    if (port == PORT_ZXPOLY_MAIN)
    {
        if (module == 0 && !_locked)
            OnMainPortWrite(value);
        return true;
    }

    const size_t mapped = (_port3D00 >> 5) & 0x03u;
    if (module == 0 && !_locked && mapped != 0)
    {
        // IO-mapped window: master OUTs land in module `mapped` as memory
        // writes at address = port. #7FFD is the exception: it still pages the
        // master itself unless the master's R1 bit 5 routes it through the
        // window too (COPY2CPU sets it, so its copy loop can pass #7FFD)
        if (port == 0x7FFD && (_regs[0][1] & 0x20u) == 0)
            return OnPort7FFDWrite(0, value);

        *ModuleRam(mapped, port) = value;                     // the live slave
        _overlay[mapped][WindowOffset(mapped, port)] = value; // kept for a later loader lock
        RaiseModuleNmi(mapped);                               // unless its R1 D4 masks it
        return true;
    }

    if (port == 0x7FFD)
        return OnPort7FFDWrite(module, value);

    size_t target = 0;
    size_t reg = 0;
    if (IsModuleRegisterPort(port, target, reg))
    {
        // #xxFF is the Beta 128 system port while TR-DOS is active
        if (IsTRDOSActive(module))
            return false;

        // Writes: unlocked only, and only to a module with index >= writer.
        // R0 D5 is a local reset of that module
        if (!_locked && module <= target)
        {
            _regs[target][reg] = value;
            if (reg == 0)
            {
                if (value & 0x20u)
                    LocalReset(target);
                if (value & 0x40u)
                    RaiseModuleNmi(target);
                if (value & 0x80u)
                    RaiseModuleInt(target);
            }
            else if (_stopWait[target] && GetContext(target)->pCore->GetZ80()->pc != StopAddress(target))
            {
                // A new stop address releases a module waiting at the old one
                _stopWait[target] = false;
                if (target != 0)
                    AlignSlaveClock(target, 3);
            }
        }
        return true;
    }

    // A slave's device writes reach the shared devices unless its R0 D4
    // disables them (every ZX-Poly edition sets it for the slaves)
    if (module != 0 && (_regs[module][0] & 0x10u) == 0)
    {
        EmulatorContext* master = GetContext(0);
        master->pPortDecoder->DecodePortOut(port, value, GetContext(module)->pCore->GetZ80()->m1_pc);
    }

    return false;
}

bool ZXPolyGroup::OnPortIn(size_t module, uint16_t port, uint8_t& value)
{
    const size_t mapped = (_port3D00 >> 5) & 0x03u;
    if (!_locked && mapped != 0 && module != mapped)
    {
        // IO-mapped window: the mapped module's memory at address = port,
        // with an INT to it. Any other module's reads go there too (zxpoly
        // ZxPolyModule.readIo); only the master's writes do
        value = *ModuleRam(mapped, port);
        RaiseModuleInt(mapped);
        return true;
    }

    if (port == PORT_ZXPOLY_MAIN)
    {
        value = ModuleIdentity(module);
        return true;
    }

    size_t target = 0;
    size_t reg = 0;
    if (IsModuleRegisterPort(port, target, reg))
    {
        if (IsTRDOSActive(module))
            return false;

        if (reg != 0)
        {
            value = 0xFFu;
            return true;
        }

        // Locked, the slaves step from frame boundary to frame boundary: a
        // master reading a slave sees it at the last boundary, a slave reading
        // the master sees the master there too - in every schedule (the slaves
        // may still be on their way there, or run while the master goes on)
        if (module == 0 && target != 0)
            WaitForSlaves();
        value = (_locked && module != 0 && target == 0) ? _masterStatusAtBoundary : ModuleStatus(target);
        return true;
    }

    return false;
}

void ZXPolyGroup::OnPortInResult(size_t module, uint16_t port, uint8_t& value, bool fromFloatingBus)
{
    (void)port;
    if (!fromFloatingBus)
        return;

    // The floating bus carries the byte the video logic fetches. On ZX-Poly
    // that is CPU0's video memory (zxpoly Motherboard, module 0 only); a slave
    // fetching from its own plane would read a different byte and branch
    // differently (floating-bus-synced games)
    const EmulatorContext* context = GetContext(module);
    const uint64_t key = (context->emulatorState.frame_counter << 32) | context->pCore->GetZ80()->t;
    std::lock_guard<std::mutex> lock(_floatingBusMutex);
    if (module == 0)
    {
        if (_locked || _slavesRunning)    // parked slaves read nothing
            _masterFloatingBus[key] = value;
        return;
    }

    const auto found = _masterFloatingBus.find(key);
    if (found != _masterFloatingBus.end())
        value = found->second;
}

/// endregion </Platform ports>

/// region <Execution>

void ZXPolyGroup::QueueInput(const InputOp& op)
{
    std::lock_guard<std::mutex> lock(_keysMutex);
    _pendingInput.push_back(op);
}

void ZXPolyGroup::PressKey(ZXKeysEnum key)
{
    QueueInput({InputOp::KeyDown, key, 0, 0});
}

void ZXPolyGroup::ReleaseKey(ZXKeysEnum key)
{
    QueueInput({InputOp::KeyUp, key, 0, 0});
}

std::vector<ZXPolyGroup::InputOp> ZXPolyGroup::TakePendingInput()
{
    std::lock_guard<std::mutex> lock(_keysMutex);
    std::vector<InputOp> input;
    input.swap(_pendingInput);
    return input;
}

void ZXPolyGroup::ApplyInput(size_t module, const std::vector<InputOp>& input)
{
    EmulatorContext* context = GetContext(module);
    for (const InputOp& op : input)
    {
        // A host speed change: the master queues it for its next frame start,
        // and AdvanceSlaves hands it to the slaves before they cross that start
        if (op.kind == InputOp::Speed)
        {
            if (module == 0)
                context->pCore->SetSpeedMultiplier(static_cast<uint8_t>(op.a));
            continue;
        }

        // Journal it in the module's TTD session first (the contract of
        // TimeTravelManager::RecordInputEvent: before applying), so a replay
        // feeds every module the same input at the same T
        ttd::TimeTravelManager* ttd = context->pTimeTravelManager;
        if (ttd && ttd->IsRecording() && !ttd->IsReplayActive())
        {
            switch (op.kind)
            {
                case InputOp::KeyDown: ttd->RecordInputEvent(op.key, true); break;
                case InputOp::KeyUp: ttd->RecordInputEvent(op.key, false); break;
                case InputOp::MouseMove: ttd->RecordMouseMove(op.a, op.b); break;
                case InputOp::MouseButtons: ttd->RecordMouseButtons(static_cast<uint8_t>(op.a)); break;
                case InputOp::MouseWheel: ttd->RecordMouseWheel(op.a); break;
                case InputOp::MouseCounters:
                case InputOp::KeyboardReset:
                case InputOp::Speed: break;
            }
        }

        switch (op.kind)
        {
            case InputOp::KeyDown: context->pKeyboard->PressKey(op.key); break;
            case InputOp::KeyUp: context->pKeyboard->ReleaseKey(op.key); break;
            case InputOp::MouseMove:
                if (context->pMouse)
                    context->pMouse->Move(op.a, op.b);
                break;
            case InputOp::MouseButtons:
                if (context->pMouse)
                    context->pMouse->SetButtons(static_cast<uint8_t>(op.a));
                break;
            case InputOp::MouseWheel:
                if (context->pMouse)
                    context->pMouse->SetWheel(op.a);
                break;
            case InputOp::MouseCounters:
                if (context->pMouse)
                    context->pMouse->SetCounters(static_cast<uint8_t>(op.a), static_cast<uint8_t>(op.b));
                break;
            case InputOp::KeyboardReset:
                context->pKeyboard->Reset();
                break;
            case InputOp::Speed: break;
        }
    }
}

void ZXPolyGroup::EnablePortReadCheck(bool enable)
{
    WaitForSlaves();
    _portReadCheck = enable;
    for (size_t m = 0; m < MODULES; m++)
    {
        Z80* cpu = GetContext(m)->pCore->GetZ80();
        _portReads[m].clear();
        if (!enable)
        {
            cpu->busTraceHook = nullptr;
            continue;
        }

        std::vector<PortRead>* log = &_portReads[m];
        cpu->busTraceHook = [cpu, log](char type, uint16_t addr, uint8_t value) {
            if (type == 'I')
                log->push_back(PortRead{cpu->t, addr, cpu->m1_pc, value});
        };
    }
}

void ZXPolyGroup::EnableInstructionTrace(bool enable)
{
    WaitForSlaves();
    // The per-instruction hooks are the group's own (InstallMasterM1Hook,
    // InstallSlaveM1Hooks); they log while this is on
    _instructionTrace = enable;
    for (auto& log : _instructions)
        log.clear();
}

void ZXPolyGroup::RunSlaveToMasterPosition(size_t module)
{
    // The master may have been driven any way (a test's RunFrame, its own
    // frame loop, a debugger pause): whatever instruction it stopped after,
    // the slave runs the same instruction stream to exactly that position
    RunSlaveToPosition(module, GetContext(0)->emulatorState.frame_counter, GetContext(0)->pCore->GetZ80()->t);
}

void ZXPolyGroup::RunSlaveToPosition(size_t module, uint64_t masterFrame, uint32_t masterT)
{
    EmulatorContext* context = GetContext(module);
    auto reached = [&](uint32_t t) {
        const uint64_t frame = context->emulatorState.frame_counter;
        return frame > masterFrame || (frame == masterFrame && t >= masterT);
    };
    if (_stopWait[module] || reached(context->pCore->GetZ80()->t))
        return;

    const bool unlocked = !_locked;
    const uint16_t stop = StopAddress(module);

    // Safety limit: two frames at the larger of the current and the queued
    // clock multiplier (the host speed control stretches the frame x2..x16)
    const EmulatorState& state = context->emulatorState;
    const unsigned multiplier = std::max<unsigned>(
        state.current_z80_frequency_multiplier, static_cast<unsigned>(state.next_z80_frequency_multiplier) << state.hw_turbo_shift);
    const unsigned limit = context->config.frame * 2u * std::max(multiplier, 1u);
    _instances[module]->RunUntilCondition(
        [&](const Z80State& state) {
            if (unlocked)
            {
                // Halt notification on the HALT edge; a stop address parks
                if (state.halted && !_wasHalted[module])
                    OnModuleHalted(module);
                _wasHalted[module] = state.halted != 0;
                if (stop != 0 && state.pc == stop)
                {
                    _stopWait[module] = true;
                    return true;
                }
            }
            return reached(state.t);
        },
        limit, false);
}

void ZXPolyGroup::InstallMasterM1Hook()
{
    // Before every master instruction: the instruction trace (debug aid), and
    // while the slaves run unlocked (their own code, talking to the master
    // through the platform ports) they catch up with the master instruction
    // by instruction - the zxpoly board steps all four once per round
    Z80* master = GetContext(0)->pCore->GetZ80();
    master->m1TraceHook = [this, master](uint16_t pc) {
        if (_instructionTrace)
            _instructions[0].push_back(pc);
        CaptureLines(0);
        if (!_locked)
        {
            if (master->halted && !_wasHalted[0])
                OnModuleHalted(0);
            _wasHalted[0] = master->halted != 0;
        }
        if (_slavesRunning)
            CatchUpSlaves();
    };
}

void ZXPolyGroup::InstallSlaveM1Hooks()
{
    for (size_t m = 1; m < MODULES; m++)
    {
        Z80* cpu = GetContext(m)->pCore->GetZ80();
        cpu->m1TraceHook = [this, m](uint16_t pc) {
            if (_instructionTrace)
                _instructions[m].push_back(pc);
            CaptureLines(m);
        };
    }
}

void ZXPolyGroup::CopyScreenLine(size_t module, unsigned line, ZXPolyScreenComposer::Lines& lines) const
{
    const uint8_t* screen = GetScreenMemory(module);
    uint8_t* out = lines.data() + line * ZXPolyScreenComposer::LINE_BYTES;
    std::memcpy(out, screen + ZXPolyScreenComposer::BitmapOffset(0, line), 32);
    std::memcpy(out + 32, screen + ZXPolyScreenComposer::AttributeOffset(0, line), 32);
}

void ZXPolyGroup::FillMissingLines(size_t module, size_t slot)
{
    // Lines no instruction boundary passed (a CPU in HALT, whose video memory
    // does not change meanwhile) are taken from the screen as it is now
    LineCapture& capture = _capture[module];
    if (capture.captured[slot].all())
        return;
    for (unsigned line = 0; line < 192; line++)
    {
        if (!capture.captured[slot][line])
        {
            CopyScreenLine(module, line, capture.lines[slot]);
            capture.captured[slot].set(line);
        }
    }
}

void ZXPolyGroup::CaptureLines(size_t module)
{
    EmulatorContext* context = GetContext(module);
    Screen* screen = context->pScreen;
    const RasterDescriptor& raster = screen->rasterDescriptors[screen->GetVideoMode()];
    if (raster.screenWidth != 256 || raster.screenHeight != 192)
        return;    // not a ZX-classic paper area

    LineCapture& capture = _capture[module];
    const uint64_t frame = context->emulatorState.frame_counter;
    if (frame != capture.currentFrame)
    {
        // A new frame: complete the previous one, start filling this one
        if (capture.currentFrame != ~0ull)
            FillMissingLines(module, capture.currentFrame & 1u);
        capture.currentFrame = frame;
        const size_t slot = frame & 1u;
        capture.frame[slot] = frame;
        capture.captured[slot].reset();
        capture.nextLine[slot] = 0;
    }

    // The beam runs in base-clock T-states: the host speed control stretches
    // the frame x2..x16 (and a hardware turbo doubles the CPU's T), so the
    // CPU's T is descaled first, as Screen::GetCurrentTstate does
    const size_t slot = frame & 1u;
    const uint32_t multiplier = context->emulatorState.current_z80_frequency_multiplier;
    const uint32_t cpuT = context->pCore->GetZ80()->t;
    const uint32_t t = multiplier > 1 ? cpuT / multiplier : cpuT;
    const uint32_t paperStart = screen->GetPaperStartTstate();
    const uint32_t perLine = screen->GetTstatesPerLine();
    constexpr uint32_t PAPER_FETCH_T = 128;    // 256 pixels at 2 per T
    unsigned& next = capture.nextLine[slot];
    while (next < 192 && t >= paperStart + next * perLine + PAPER_FETCH_T)
    {
        CopyScreenLine(module, next, capture.lines[slot]);
        capture.captured[slot].set(next);
        next++;
    }
}

const ZXPolyScreenComposer::Lines* ZXPolyGroup::CapturedFrame(size_t module, uint64_t frame)
{
    LineCapture& capture = _capture[module];
    const size_t slot = frame & 1u;
    if (capture.frame[slot] != frame)
        return nullptr;
    FillMissingLines(module, slot);
    return &capture.lines[slot];
}

void ZXPolyGroup::CatchUpSlaves()
{
    for (size_t m = 1; m < MODULES; m++)
        RunSlaveToMasterPosition(m);
}

void ZXPolyGroup::ResetSlaveMachines()
{
    // A system reset resets every module: the slaves restart at #0000 and wait
    for (size_t m = 1; m < MODULES; m++)
        _instances[m]->Reset();
}

bool ZXPolyGroup::CanRunSlavesInParallel() const
{
    // Locked, with every slave's device writes disabled: the slaves share
    // nothing (registers frozen, the master's floating-bus log and R0 status
    // only read), so they run their frame at the same time
    bool parallel = _parallelSlaves && _locked;
    for (size_t m = 1; m < MODULES && parallel; m++)
        parallel = (_regs[m][0] & 0x10u) != 0;
    return parallel;
}

bool ZXPolyGroup::CanOverlapSlaves() const
{
    // Unlimited speed only: at normal speed the frame budget has room for the
    // slaves, and a shown picture needs them finished. The host speed control
    // (x2..x16) renders every frame, so it stays synchronous as well: up to x8
    // the parallel schedule fits the budget, and turbo is the faster way to
    // run ahead (prototype-results.md §8). The debug logs are per frame and
    // cleared at its start, so they keep the slaves in step
    return _pipelinedSlaves && CanRunSlavesInParallel() && !_portReadCheck && !_instructionTrace &&
           _instances[0]->IsTurboMode();
}

void ZXPolyGroup::PruneFloatingBus()
{
    // The slaves are at the previous boundary or later: they still need this
    // frame's and the previous frame's reads, nothing older
    const uint64_t frame = GetContext(0)->emulatorState.frame_counter;
    std::lock_guard<std::mutex> lock(_floatingBusMutex);
    for (auto it = _masterFloatingBus.begin(); it != _masterFloatingBus.end();)
        it = (it->first >> 32) + 1 < frame ? _masterFloatingBus.erase(it) : std::next(it);
}

void ZXPolyGroup::AdvanceSlaves(const std::vector<InputOp>& input, bool mayOverlap)
{
    _pipelining = false;

    // The host speed control (x2..x16) stretches the frame: a queued change
    // takes effect at the next frame start (Z80::BeginFrame). The slaves take
    // the master's before they cross that frame start too. Only the master's
    // thread writes it (Emulator::SetSpeedMultiplier comes through the input
    // queue), so the master cannot take a change the slaves missed
    const uint8_t speed = GetContext(0)->emulatorState.next_z80_frequency_multiplier;
    for (size_t m = 1; m < MODULES; m++)
        GetContext(m)->emulatorState.next_z80_frequency_multiplier = speed;
    if (!_locked && !_slavesRunning)
    {
        // Unlocked with nWAIT = 0: the slaves are parked (they still get the input)
        for (size_t m = 1; m < MODULES; m++)
            ApplyInput(m, input);
        return;
    }

    // Where the master is: every slave runs to exactly there, then takes the
    // input of this boundary, as the master just did
    const uint64_t frame = GetContext(0)->emulatorState.frame_counter;
    const uint32_t t = GetContext(0)->pCore->GetZ80()->t;
    _masterStatusAtBoundary = ModuleStatus(0);
    _masterAtBoundary = ReadControlState(0);
    _boundaryChecked = _locked;
    _lockedThisFrame = false;
    PruneFloatingBus();

    // Each slave: to the boundary, the lockstep check there, then the input
    auto advance = [this, frame, t](size_t m, const std::vector<InputOp>& slaveInput) {
        RunSlaveToPosition(m, frame, t);
        if (_locked)
        {
            std::string what = CompareControlState(_masterAtBoundary, ReadControlState(m));
            std::lock_guard<std::mutex> lock(_lockstepMutex);
            _boundaryDivergence[m] = std::move(what);
        }
        ApplyInput(m, slaveInput);
    };

    if (!CanRunSlavesInParallel())
    {
        for (size_t m = 1; m < MODULES; m++)
            advance(m, input);
        return;
    }

    auto shared = std::make_shared<const std::vector<InputOp>>(input);
    for (size_t m = 1; m < MODULES; m++)
        _workers->Post(m - 1, [advance, m, shared]() { advance(m, *shared); });

    if (mayOverlap && CanOverlapSlaves())
    {
        _pipelining = true;    // the next boundary (or whatever needs the slaves) waits
        return;
    }
    WaitForSlaves();
}

void ZXPolyGroup::WaitForSlaves() const
{
    _workers->WaitAll();
}

void ZXPolyGroup::SetParallelSlaves(bool parallel)
{
    WaitForSlaves();
    _parallelSlaves = parallel;
}

void ZXPolyGroup::SetPipelinedSlaves(bool pipelined)
{
    WaitForSlaves();
    _pipelinedSlaves = pipelined;
}

void ZXPolyGroup::AtFrameBoundary(bool pictureNeeded, bool mayOverlap)
{
    // A pipelined schedule left the slaves running to the previous boundary
    WaitForSlaves();
    DetectMasterReset();

    // Input queued meanwhile: the master takes it now, each slave once it is here
    const std::vector<InputOp> input = TakePendingInput();
    ApplyInput(0, input);
    AdvanceSlaves(input, mayOverlap && !pictureNeeded);
    SnapshotPlatform();
}

void ZXPolyGroup::DetectMasterReset()
{
    // A system reset of the master (menu, automation, a disk autostart or a
    // plain snapshot load all run Core::Reset, which restarts the frame
    // counter) is a ZX-Poly system RESET: #3D00 = 0, the platform ports
    // unlock, the slaves park in WAIT and only CPU0 is shown (mode 0), until
    // a multiloader locks the machine again
    const uint64_t frame = GetContext(0)->emulatorState.frame_counter;
    if (frame < _lastMasterFrame)
    {
        ResetPlatformState();
        ResetSlaveMachines();
    }
    _lastMasterFrame = frame;
}

void ZXPolyGroup::MountMasterDisksOnSlaves()
{
    // After the lock the slaves run the program too: a program that loads
    // more from disk must find the same images in their drives
    for (size_t m = 1; m < MODULES; m++)
    {
        for (uint8_t drive = 0; drive < 4; drive++)
        {
            const std::string& path = GetContext(0)->coreState.diskFilePaths[drive];
            if (!path.empty() && GetContext(m)->coreState.diskFilePaths[drive] != path)
                _instances[m]->LoadDisk(path, drive);
        }
    }
}

void ZXPolyGroup::RunFrame()
{
    RunFrames(1);
}

void ZXPolyGroup::RunFrames(unsigned frames)
{
    // Input queued before the call reaches all four where they are now
    WaitForSlaves();
    const std::vector<InputOp> input = TakePendingInput();
    for (size_t m = 0; m < MODULES; m++)
        ApplyInput(m, input);

    EmulatorContext* master = GetContext(0);
    for (unsigned i = 0; i < frames; i++)
    {
        for (auto& log : _portReads)
            log.clear();
        for (auto& log : _instructions)
            log.clear();

        // One machine frame: the master runs to the frame boundary. (Emulator::RunFrame
        // is the debugger's frame step - it returns to an anchored T position that a
        // TTD seek re-anchors, so replayed frames would not end where recorded ones did)
        const uint64_t startFrame = master->emulatorState.frame_counter;
        _instances[0]->RunUntilCondition(
            [master, startFrame](const Z80State&) { return master->emulatorState.frame_counter != startFrame; },
            0, false);

        // The last boundary completes before returning: callers inspect the slaves
        AtFrameBoundary(false, i + 1 < frames);
    }
}

/// region <Group TTD>

bool ZXPolyGroup::StartRecording(std::string* error)
{
    WaitForSlaves();
    for (size_t m = 0; m < MODULES; m++)
    {
        ttd::TimeTravelManager* ttd = GetContext(m)->pTimeTravelManager;
        if (ttd)
            ttd->SetUnavailableReason("");
        if (ttd == nullptr || !ttd->StartRecording())
        {
            if (error)
                *error = StringHelper::Format("module %zu: TTD recording could not start", m);
            for (size_t started = 0; started < MODULES; started++)
            {
                if (ttd::TimeTravelManager* session = GetContext(started)->pTimeTravelManager)
                {
                    session->StopRecording();
                    session->SetUnavailableReason(TTD_UNAVAILABLE);
                }
            }
            return false;
        }
    }
    _recording = true;
    _platformHistory.clear();
    SnapshotPlatform();
    return true;
}

void ZXPolyGroup::StopRecording()
{
    WaitForSlaves();
    for (size_t m = 0; m < MODULES; m++)
    {
        if (ttd::TimeTravelManager* ttd = GetContext(m)->pTimeTravelManager)
        {
            ttd->StopRecording();
            ttd->SetUnavailableReason(TTD_UNAVAILABLE);    // the group timeline ends here
        }
    }
    _recording = false;
}

uint64_t ZXPolyGroup::GetRecordedPosition() const
{
    const ttd::TimeTravelManager* ttd = GetContext(0)->pTimeTravelManager;
    return ttd ? ttd->CurrentPosition().frame : 0;
}

void ZXPolyGroup::SnapshotPlatform()
{
    if (!_recording)
        return;
    PlatformSnapshot& snapshot = _platformHistory[GetRecordedPosition()];
    snapshot.port3D00 = _port3D00;
    snapshot.regs = _regs;
    snapshot.locked = _locked;
    snapshot.slavesRunning = _slavesRunning;
    snapshot.stopWait = _stopWait;
    snapshot.wasHalted = _wasHalted;
}

bool ZXPolyGroup::SeekToFrame(uint64_t frame, std::string* error)
{
    if (!_recording)
    {
        if (error)
            *error = "the group is not recording";
        return false;
    }

    // A live master is paused first (its loop is where the slaves run)
    Emulator& master = *_instances[0];
    if (master.IsRunning() && !master.IsPaused())
    {
        master.Pause();
        master.GetMainLoop()->WaitForPauseConfirmation(500);
    }
    WaitForSlaves();

    // TTD seeks only a stopped session (history kept; ResumeRecording picks it
    // up again). Frame-aligned targets restore a checkpoint without replaying
    // anything, so no module executes (and no group hook fires) during the seek
    for (size_t m = 0; m < MODULES; m++)
    {
        ttd::TimeTravelManager* session = GetContext(m)->pTimeTravelManager;
        if (session->IsRecording())
            session->StopRecording();
        if (!session->SeekTo(ttd::TTDTimePoint{frame, 0}))
        {
            if (error)
                *error = StringHelper::Format("module %zu: TTD cannot seek to frame %llu", m,
                                              static_cast<unsigned long long>(frame));
            return false;
        }
    }

    // The platform as it was at the start of that frame
    auto it = _platformHistory.upper_bound(frame);
    if (it != _platformHistory.begin())
    {
        const PlatformSnapshot& snapshot = std::prev(it)->second;
        _port3D00 = snapshot.port3D00;
        _regs = snapshot.regs;
        _locked = snapshot.locked;
        _slavesRunning = snapshot.slavesRunning;
        _stopWait = snapshot.stopWait;
        _wasHalted = snapshot.wasHalted;
    }
    _lockedThisFrame = false;
    _boundaryChecked = false;    // all four restored to the same frame start
    UpdateIntGates();

    // Derived per-frame state starts over from the restored frame
    _masterFloatingBus.clear();
    for (LineCapture& capture : _capture)
        capture = LineCapture{};
    {
        std::lock_guard<std::mutex> lock(_keysMutex);
        _pendingInput.clear();
    }
    _lastMasterFrame = GetContext(0)->emulatorState.frame_counter;    // not a reset
    return true;
}

bool ZXPolyGroup::ResumeRecording(std::string* error)
{
    WaitForSlaves();
    if (!_recording)
    {
        if (error)
            *error = "the group is not recording";
        return false;
    }
    for (size_t m = 0; m < MODULES; m++)
    {
        ttd::TimeTravelManager* ttd = GetContext(m)->pTimeTravelManager;
        if (!ttd->ResumeRecordingFrom(ttd->CurrentPosition()))
        {
            if (error)
                *error = StringHelper::Format("module %zu: TTD cannot resume recording", m);
            return false;
        }
    }
    _platformHistory.erase(_platformHistory.upper_bound(GetRecordedPosition()), _platformHistory.end());
    return true;
}

/// endregion </Group TTD>

/// region <Live mode>

void ZXPolyGroup::AttachToMaster()
{
    if (!IsCreated() || _attached)
        return;

    const FramebufferDescriptor& fb = GetContext(0)->pScreen->GetFramebufferDescriptor();
    ResizeDisplay(fb.width * 2u, fb.height * 2u);

    _instances[0]->GetMainLoop()->SetFrameEndHook([this](bool rendered) { OnMasterFrameEnd(rendered); });

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observer = static_cast<Observer*>(this);
    messageCenter.AddObserver(MC_KEY_PRESSED, observer,
                              static_cast<ObserverCallbackMethod>(&ZXPolyGroup::OnHostKeyPressed));
    messageCenter.AddObserver(MC_KEY_RELEASED, observer,
                              static_cast<ObserverCallbackMethod>(&ZXPolyGroup::OnHostKeyReleased));
    for (const char* topic : {MC_MOUSE_MOVE, MC_MOUSE_BUTTON, MC_MOUSE_WHEEL})
        messageCenter.AddObserver(topic, observer, static_cast<ObserverCallbackMethod>(&ZXPolyGroup::OnHostMouse));
    _attached = true;
}

void ZXPolyGroup::DetachFromMaster()
{
    if (!_attached)
        return;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observer = static_cast<Observer*>(this);
    messageCenter.RemoveObserver(MC_KEY_PRESSED, observer,
                                 static_cast<ObserverCallbackMethod>(&ZXPolyGroup::OnHostKeyPressed));
    messageCenter.RemoveObserver(MC_KEY_RELEASED, observer,
                                 static_cast<ObserverCallbackMethod>(&ZXPolyGroup::OnHostKeyReleased));
    for (const char* topic : {MC_MOUSE_MOVE, MC_MOUSE_BUTTON, MC_MOUSE_WHEEL})
        messageCenter.RemoveObserver(topic, observer, static_cast<ObserverCallbackMethod>(&ZXPolyGroup::OnHostMouse));

    if (_instances[0])
    {
        if (_instances[0]->IsRunning())
            _instances[0]->Stop();
        _instances[0]->GetMainLoop()->SetFrameEndHook(nullptr);
    }
    WaitForSlaves();
    _attached = false;
}

void ZXPolyGroup::OnMasterFrameEnd(bool rendered)
{
    // On the master's emulation thread, between two of its frames. At
    // unlimited speed the slaves' frame overlaps the master's next one -
    // except before a shown frame: its picture needs all four
    AtFrameBoundary(rendered, true);

    if (!rendered)
        return;
    if (GetVideoMode() != 0)    // mode 0 is the master's own picture
        ComposeIntoMasterFramebuffer();
    ComposeDisplayFrame();
}

void ZXPolyGroup::ResizeDisplay(unsigned width, unsigned height)
{
    std::lock_guard<std::mutex> lock(_displayMutex);
    _displayWidth = width;
    _displayHeight = height;
    _displayFront.assign(static_cast<size_t>(width) * height, 0xFF000000u);
    _displayBack.assign(static_cast<size_t>(width) * height, 0xFF000000u);
}

void ZXPolyGroup::ComposeDisplayFrame()
{
    Screen* screen = GetContext(0)->pScreen;
    const FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
    if (fb.memoryBuffer == nullptr)
        return;
    if (fb.width * 2u != _displayWidth || fb.height * 2u != _displayHeight)
        ResizeDisplay(fb.width * 2u, fb.height * 2u);    // the consumer re-attaches on NC_VIDEO_MODE_CHANGED

    // The master's picture (border and, before the lock, everything) at 2x
    const uint32_t* source = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
    const unsigned width = _displayWidth;
    for (unsigned y = 0; y < fb.height; y++)
    {
        const uint32_t* in = source + static_cast<size_t>(y) * fb.width;
        uint32_t* out0 = _displayBack.data() + static_cast<size_t>(y * 2) * width;
        uint32_t* out1 = out0 + width;
        for (unsigned x = 0; x < fb.width; x++)
        {
            out0[x * 2] = out0[x * 2 + 1] = in[x];
            out1[x * 2] = out1[x * 2 + 1] = in[x];
        }
    }

    // After the lock: the composed paper at its full 512 x 384
    const RasterDescriptor& raster = screen->rasterDescriptors[screen->GetVideoMode()];
    if (GetVideoMode() != 0 && raster.screenWidth == 256 && raster.screenHeight == 192)
    {
        std::vector<uint32_t> picture;
        Compose(picture);
        for (unsigned y = 0; y < ZXPolyScreenComposer::OUT_HEIGHT; y++)
        {
            std::memcpy(_displayBack.data() + static_cast<size_t>(raster.screenOffsetTop * 2 + y) * width +
                            raster.screenOffsetLeft * 2,
                        picture.data() + static_cast<size_t>(y) * ZXPolyScreenComposer::OUT_WIDTH,
                        ZXPolyScreenComposer::OUT_WIDTH * sizeof(uint32_t));
        }
    }

    std::lock_guard<std::mutex> lock(_displayMutex);
    _displayFront.swap(_displayBack);
}

bool ZXPolyGroup::CopyDisplay(uint8_t* dst, size_t dstSize)
{
    std::lock_guard<std::mutex> lock(_displayMutex);
    const size_t size = _displayFront.size() * sizeof(uint32_t);
    if (dst == nullptr || dstSize != size || size == 0)
        return false;
    std::memcpy(dst, _displayFront.data(), size);
    return true;
}

void ZXPolyGroup::ComposeIntoMasterFramebuffer()
{
    Screen* screen = GetContext(0)->pScreen;
    FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
    if (fb.memoryBuffer == nullptr)
        return;

    const RasterDescriptor& raster = screen->rasterDescriptors[screen->GetVideoMode()];
    if (raster.screenWidth != 256 || raster.screenHeight != 192)
        return;    // not a ZX-classic paper area

    std::vector<uint32_t> picture;
    Compose(picture);

    // The framebuffer's paper area is 256 x 192: one sample per 2 x 2 block
    // (exact for modes 0-4, 6 and 7; mode 5 shows CPU0's quadrant)
    uint32_t* pixels = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    for (unsigned y = 0; y < 192; y++)
    {
        uint32_t* row = pixels + (raster.screenOffsetTop + y) * fb.width + raster.screenOffsetLeft;
        const uint32_t* source = picture.data() + (y * 2) * ZXPolyScreenComposer::OUT_WIDTH;
        for (unsigned x = 0; x < 256; x++)
            row[x] = source[x * 2];
    }
}

void ZXPolyGroup::OnHostKeyPressed(int id, Message* message)
{
    (void)id;
    QueueHostKey(message, true);
}

void ZXPolyGroup::OnHostKeyReleased(int id, Message* message)
{
    (void)id;
    QueueHostKey(message, false);
}

void ZXPolyGroup::QueueHostKey(Message* message, bool pressed)
{
    if (message == nullptr || message->obj == nullptr || !_instances[0])
        return;

    const KeyboardEvent* event = dynamic_cast<const KeyboardEvent*>(message->obj);
    if (event == nullptr)
        return;

    // Keys for the master (or broadcast) are the machine's keys
    if (!event->targetEmulatorId.empty() && event->targetEmulatorId != std::string(_instances[0]->GetUUID()))
        return;

    if (pressed)
        PressKey(static_cast<ZXKeysEnum>(event->zxKeyCode));
    else
        ReleaseKey(static_cast<ZXKeysEnum>(event->zxKeyCode));
}

void ZXPolyGroup::OnHostMouse(int id, Message* message)
{
    (void)id;
    if (message == nullptr || message->obj == nullptr || !_instances[0])
        return;

    const MouseEvent* event = dynamic_cast<const MouseEvent*>(message->obj);
    if (event == nullptr)
        return;
    if (!event->targetId.empty() && event->targetId != _instances[0]->GetId())
        return;

    switch (event->kind)
    {
        case MouseEventKind::Move: QueueInput({InputOp::MouseMove, ZXKEY_NONE, event->dx, event->dy}); break;
        case MouseEventKind::Buttons: QueueInput({InputOp::MouseButtons, ZXKEY_NONE, event->buttonMask, 0}); break;
        case MouseEventKind::Wheel: QueueInput({InputOp::MouseWheel, ZXKEY_NONE, event->wheelSteps, 0}); break;
        default: break;
    }
}

/// endregion </Live mode>

ZXPolyGroup::ControlState ZXPolyGroup::ReadControlState(size_t module) const
{
    const EmulatorContext* context = GetContext(module);
    const Z80& cpu = *context->pCore->GetZ80();
    ControlState state;
    state.pc = cpu.pc;
    state.sp = cpu.sp;
    state.i = cpu.i;
    state.im = cpu.im;
    state.iff1 = cpu.iff1;
    state.halted = cpu.halted;
    state.t = cpu.t;
    state.p7FFD = context->emulatorState.p7FFD;
    return state;
}

std::string ZXPolyGroup::CompareControlState(const ControlState& master, const ControlState& slave)
{
    // Control state only (zxpoly TRIGGER_DIFF_MODULESTATES: PC, SP, IM, IFF).
    // Data registers legitimately differ: mid-draw, A or a pair holds a
    // graphics byte, and graphics are what the planes differ in. A split in
    // control flow always shows up here or in T
    std::string what;
    auto check = [&what](const char* name, unsigned va, unsigned vb) {
        if (va != vb && what.empty())
            what = StringHelper::Format("%s master=#%04X slave=#%04X", name, va, vb);
    };
    check("PC", master.pc, slave.pc);
    check("SP", master.sp, slave.sp);
    check("I", master.i, slave.i);
    check("IM", master.im, slave.im);
    check("IFF1", master.iff1, slave.iff1);
    check("HALT", master.halted, slave.halted);
    check("T", master.t, slave.t);
    check("7FFD", master.p7FFD, slave.p7FFD);
    return what;
}

ZXPolyGroup::Divergence ZXPolyGroup::CheckLockstep() const
{
    WaitForSlaves();
    Divergence result;
    if (!_locked)
        return result;

    if (_portReadCheck)
    {
        const std::vector<PortRead>& reference = _portReads[0];
        for (size_t m = 1; m < MODULES; m++)
        {
            const std::vector<PortRead>& log = _portReads[m];
            const size_t n = std::min(reference.size(), log.size());
            for (size_t i = 0; i < n; i++)
            {
                const PortRead& a = reference[i];
                const PortRead& b = log[i];
                if (a.port != b.port || a.t != b.t || a.value != b.value)
                {
                    result.diverged = true;
                    result.module = m;
                    result.what = StringHelper::Format(
                        "IN #%zu: master port=#%04X t=%u pc=#%04X value=#%02X, slave port=#%04X t=%u pc=#%04X value=#%02X",
                        i, a.port, a.t, a.pc, a.value, b.port, b.t, b.pc, b.value);
                    return result;
                }
            }
            if (reference.size() != log.size())
            {
                result.diverged = true;
                result.module = m;
                result.what = StringHelper::Format("IN count master=%zu slave=%zu", reference.size(), log.size());
                return result;
            }
        }
    }

    if (_instructionTrace)
    {
        const std::vector<uint16_t>& reference = _instructions[0];
        for (size_t m = 1; m < MODULES; m++)
        {
            const std::vector<uint16_t>& log = _instructions[m];
            const size_t n = std::min(reference.size(), log.size());
            size_t i = 0;
            while (i < n && reference[i] == log[i])
                i++;
            if (i == n && reference.size() == log.size())
                continue;

            std::string history;
            for (size_t k = i > 12 ? i - 12 : 0; k < i; k++)
                history += StringHelper::Format(" %04X", reference[k]);
            result.diverged = true;
            result.module = m;
            result.what = StringHelper::Format("instruction #%zu of the frame: master PC=#%04X slave PC=#%04X; "
                                               "preceding:%s",
                                               i, i < reference.size() ? reference[i] : 0xFFFF,
                                               i < log.size() ? log[i] : 0xFFFF, history.c_str());
            return result;
        }
    }

    // Control state at the last frame boundary. Before the first boundary
    // after a load or a replication the slaves are copies standing where the
    // master stands, compared as they are
    const bool atBoundary = _boundaryChecked;
    for (size_t m = 1; m < MODULES; m++)
    {
        std::string what;
        if (atBoundary)
        {
            std::lock_guard<std::mutex> lock(_lockstepMutex);
            what = _boundaryDivergence[m];
        }
        else
        {
            what = CompareControlState(ReadControlState(0), ReadControlState(m));
        }

        if (!what.empty())
        {
            result.diverged = true;
            result.module = m;
            result.what = what;
            return result;
        }
    }

    return result;
}

/// endregion </Execution>

/// region <Video>

void ZXPolyGroup::SetVideoMode(uint8_t mode)
{
    _port3D00 = static_cast<uint8_t>((_port3D00 & ~0x1Cu) | ((mode & 0x07u) << 2));
}

const uint8_t* ZXPolyGroup::GetScreenMemory(size_t module) const
{
    EmulatorContext* context = GetContext(module);
    const uint16_t page = (context->emulatorState.p7FFD & 0x08u) ? 7 : 5;
    return context->pMemory->RAMPageAddress(page);
}

void ZXPolyGroup::Compose(std::vector<uint32_t>& out)
{
    WaitForSlaves();
    out.resize(static_cast<size_t>(ZXPolyScreenComposer::OUT_WIDTH) * ZXPolyScreenComposer::OUT_HEIGHT);

    // The last completed frame, line by line as each module's video logic
    // fetched it; a module that did not run it (parked) is shown as its
    // screen is now
    const uint64_t frame = GetContext(0)->emulatorState.frame_counter - 1;
    std::array<ZXPolyScreenComposer::Lines, MODULES> fromScreen;
    std::array<const ZXPolyScreenComposer::Lines*, MODULES> lines{};
    for (size_t m = 0; m < MODULES; m++)
    {
        lines[m] = CapturedFrame(m, frame);
        if (lines[m] == nullptr)
        {
            ZXPolyScreenComposer::LinesFromScreen(GetScreenMemory(m), fromScreen[m]);
            lines[m] = &fromScreen[m];
        }
    }

    uint32_t palette[16];
    GetContext(0)->pScreen->GetRGBAPalette16(palette);

    // ZX FLASH period: 16 frames each phase
    const bool flashPhase = ((GetContext(0)->emulatorState.frame_counter >> 4) & 1u) != 0;

    ZXPolyScreenComposer::ComposeLines(lines, GetVideoMode(), flashPhase, palette, out.data());
}

/// endregion </Video>
