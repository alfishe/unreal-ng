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

ZXPolyGroup::ZXPolyGroup(std::string symbolicPrefix) : _prefix(std::move(symbolicPrefix))
{
}

ZXPolyGroup::~ZXPolyGroup()
{
    Destroy();
}

/// endregion </Constructors / destructors>

/// region <Lifecycle>

bool ZXPolyGroup::Create(const std::string& model, std::string* error)
{
    Destroy();

    EmulatorManager* manager = EmulatorManager::GetInstance();
    for (size_t m = 0; m < MODULES; m++)
    {
        std::string createError;
        const std::string id = StringHelper::Format("%s-%zu", _prefix.c_str(), m);
        _instances[m] = manager->CreateEmulatorWithModel(id, model, LoggerLevel::LogError, &createError);
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
        // RGBA rendering (turbo mode; the composer reads their video RAM)
        if (m > 0)
        {
            _instances[m]->EnableTurboMode();
            if (context->pSoundManager)
                context->pSoundManager->mute();
        }

        // Keys reach the members only through the group (frame-boundary aligned)
        if (context->pKeyboard)
            context->pKeyboard->SetHostInputGated(true);

        // The ZX-Poly platform ports sit in front of the model's port decoder
        _interceptors[m] = std::make_unique<ZXPolyPortInterceptor>(*this, m);
        context->pCore->GetZ80()->portInterceptor = _interceptors[m].get();
    }

    InstallMasterM1Hook();
    ResetPlatformState();
    return true;
}

void ZXPolyGroup::Destroy()
{
    DetachFromMaster();

    EmulatorManager* manager = EmulatorManager::GetInstance();
    for (size_t m = 0; m < MODULES; m++)
    {
        if (_instances[m])
        {
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
    }
    _locked = false;
    _slavesRunning = false;
    _lockedThisFrame = false;
}

/// endregion </Lifecycle>

/// region <Loading and replication>

bool ZXPolyGroup::LoadZXP(const std::string& path, std::string* error)
{
    if (!IsCreated())
    {
        if (error)
            *error = "group not created";
        return false;
    }

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

    // The loader replaced every instance's state: start the frame again from
    // it, as Emulator::LoadSnapshot does
    for (auto& instance : _instances)
        instance->RestartFrame();

    _lastMasterFrame = GetContext(0)->emulatorState.frame_counter;
    return true;
}

bool ZXPolyGroup::LoadPROM(const std::string& path, std::string* error)
{
    if (!IsCreated())
    {
        if (error)
            *error = "group not created";
        return false;
    }

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

void ZXPolyGroup::ReplicateFromMaster()
{
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

    _locked = true;
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
        ReplicateFromMaster();

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
    cpu.int_pending = false;

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
    const bool waiting = module != 0 && !_locked && !_slavesRunning;
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
            if (reg == 0 && (value & 0x20u))
                LocalReset(target);
        }
        return true;
    }

    return false;
}

bool ZXPolyGroup::OnPortIn(size_t module, uint16_t port, uint8_t& value)
{
    const size_t mapped = (_port3D00 >> 5) & 0x03u;
    if (module == 0 && !_locked && mapped != 0)
    {
        // IO-mapped window: the mapped module's memory at address = port
        value = *ModuleRam(mapped, port);
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

        value = reg == 0 ? ModuleStatus(target) : 0xFFu;
        return true;
    }

    return false;
}

/// endregion </Platform ports>

/// region <Execution>

void ZXPolyGroup::PressKey(ZXKeysEnum key)
{
    std::lock_guard<std::mutex> lock(_keysMutex);
    _pendingKeys.emplace_back(key, true);
}

void ZXPolyGroup::ReleaseKey(ZXKeysEnum key)
{
    std::lock_guard<std::mutex> lock(_keysMutex);
    _pendingKeys.emplace_back(key, false);
}

void ZXPolyGroup::ApplyPendingInput()
{
    std::lock_guard<std::mutex> lock(_keysMutex);
    for (const auto& [key, pressed] : _pendingKeys)
    {
        for (size_t m = 0; m < MODULES; m++)
        {
            Keyboard* keyboard = GetContext(m)->pKeyboard;
            if (pressed)
                keyboard->PressKey(key);
            else
                keyboard->ReleaseKey(key);
        }
    }
    _pendingKeys.clear();
}

void ZXPolyGroup::EnablePortReadCheck(bool enable)
{
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
    _instructionTrace = enable;
    for (size_t m = 0; m < MODULES; m++)
    {
        Z80* cpu = GetContext(m)->pCore->GetZ80();
        _instructions[m].clear();
        if (!enable)
        {
            if (m > 0)
                cpu->m1TraceHook = nullptr;
            continue;
        }

        if (m == 0)
            continue;    // the master's hook is the group's own (InstallMasterM1Hook)
        std::vector<uint16_t>* log = &_instructions[m];
        cpu->m1TraceHook = [log](uint16_t pc) { log->push_back(pc); };
    }
}

void ZXPolyGroup::RunSlaveToMasterPosition(size_t module)
{
    // The master may have been driven any way (a test's RunFrame, its own
    // frame loop, a debugger pause): whatever instruction it stopped after,
    // the slave runs the same instruction stream to exactly that position
    const uint64_t masterFrame = GetContext(0)->emulatorState.frame_counter;
    const uint32_t masterT = GetContext(0)->pCore->GetZ80()->t;

    EmulatorContext* context = GetContext(module);
    auto reached = [&](uint32_t t) {
        const uint64_t frame = context->emulatorState.frame_counter;
        return frame > masterFrame || (frame == masterFrame && t >= masterT);
    };
    if (reached(context->pCore->GetZ80()->t))
        return;

    _instances[module]->RunUntilCondition([&](const Z80State& state) { return reached(state.t); },
                                          context->config.frame * 2, false);
}

void ZXPolyGroup::InstallMasterM1Hook()
{
    // Before every master instruction: the instruction trace (debug aid), and
    // while the slaves run unlocked (their own code, talking to the master
    // through the platform ports) they catch up with the master instruction
    // by instruction - the zxpoly board steps all four once per round
    Z80* master = GetContext(0)->pCore->GetZ80();
    master->m1TraceHook = [this](uint16_t pc) {
        if (_instructionTrace)
            _instructions[0].push_back(pc);
        if (_slavesRunning)
            CatchUpSlaves();
    };
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

void ZXPolyGroup::AdvanceSlaves()
{
    if (!_locked && !_slavesRunning)
        return;    // unlocked with nWAIT = 0: the slaves are parked

    for (size_t m = 1; m < MODULES; m++)
        RunSlaveToMasterPosition(m);
    _lockedThisFrame = false;
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
    for (auto& log : _portReads)
        log.clear();
    for (auto& log : _instructions)
        log.clear();

    ApplyPendingInput();
    _instances[0]->RunFrame(true);
    DetectMasterReset();
    AdvanceSlaves();
}

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

    if (_instances[0])
    {
        if (_instances[0]->IsRunning())
            _instances[0]->Stop();
        _instances[0]->GetMainLoop()->SetFrameEndHook(nullptr);
    }
    _attached = false;
}

void ZXPolyGroup::OnMasterFrameEnd(bool rendered)
{
    // On the master's emulation thread, between two of its frames
    DetectMasterReset();
    AdvanceSlaves();

    // Keys queued meanwhile reach all four keyboards before the next frame
    ApplyPendingInput();

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

/// endregion </Live mode>

void ZXPolyGroup::RunFrames(unsigned frames)
{
    for (unsigned i = 0; i < frames; i++)
        RunFrame();
}

ZXPolyGroup::Divergence ZXPolyGroup::CheckLockstep() const
{
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

    const Z80& a = *GetContext(0)->pCore->GetZ80();
    const EmulatorState& sa = GetContext(0)->emulatorState;

    for (size_t m = 1; m < MODULES; m++)
    {
        const Z80& b = *GetContext(m)->pCore->GetZ80();
        const EmulatorState& sb = GetContext(m)->emulatorState;

        std::string what;
        auto check = [&](const char* name, unsigned va, unsigned vb) {
            if (va != vb && what.empty())
                what = StringHelper::Format("%s master=#%04X slave=#%04X", name, va, vb);
        };
        // Control state only (zxpoly TRIGGER_DIFF_MODULESTATES: PC, SP, IM,
        // IFF). Data registers legitimately differ: mid-draw, A or a pair
        // holds a graphics byte, and graphics are what the planes differ in.
        // A split in control flow always shows up here or in T
        check("PC", a.pc, b.pc);
        check("SP", a.sp, b.sp);
        check("I", a.i, b.i);
        check("IM", a.im, b.im);
        check("IFF1", a.iff1, b.iff1);
        check("HALT", a.halted, b.halted);
        check("T", a.t, b.t);
        check("7FFD", sa.p7FFD, sb.p7FFD);

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

void ZXPolyGroup::Compose(std::vector<uint32_t>& out) const
{
    out.resize(static_cast<size_t>(ZXPolyScreenComposer::OUT_WIDTH) * ZXPolyScreenComposer::OUT_HEIGHT);

    std::array<const uint8_t*, ZXPolyScreenComposer::MODULES> vram{};
    for (size_t m = 0; m < MODULES; m++)
        vram[m] = GetScreenMemory(m);

    uint32_t palette[16];
    GetContext(0)->pScreen->GetRGBAPalette16(palette);

    // ZX FLASH period: 16 frames each phase
    const bool flashPhase = ((GetContext(0)->emulatorState.frame_counter >> 4) & 1u) != 0;

    ZXPolyScreenComposer::Compose(vram, GetVideoMode(), flashPhase, palette, out.data());
}

/// endregion </Video>
