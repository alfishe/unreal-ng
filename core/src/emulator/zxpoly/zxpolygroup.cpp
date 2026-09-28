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
        _slave7FFD[m] = 0;
    }
    _locked = false;
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

uint16_t ZXPolyGroup::ResetCommandTarget(size_t module) const
{
    // After a local reset the first three opcode fetches at #0000 return R1,
    // R2, R3. Every known loader puts JP nn there (#C3, lo, hi)
    const std::array<uint8_t, 4>& r = _regs[module];
    return r[1] == 0xC3 ? static_cast<uint16_t>(r[2] | (r[3] << 8)) : 0x0000;
}

void ZXPolyGroup::PerformLock()
{
    // Runs inside the master's locking OUT (#3D00), at its IORQ T-state. The
    // rest of that instruction only charges T-states (OUT (C),r and OUT (n),A:
    // 3 T after the port write), so the master's state here is its state
    // after the instruction, less those 3 T - the slaves get them added below.
    //
    // Local reset of the master's CPU (memory and devices untouched), then the
    // injected JP: 10 T-states, one M1 cycle
    constexpr unsigned OUT_TAIL_T = 3;
    constexpr unsigned INJECTED_JP_T = 10;
    Z80& cpu = *GetContext(0)->pCore->GetZ80();
    const bool resetRequested = (_port3D00 & MAIN_RESET) != 0;
    if (resetRequested)
    {
        cpu.pc = ResetCommandTarget(0);
        cpu.memptr = cpu.pc;
        cpu.sp = 0xFFFF;
        cpu.af = 0xFFFF;
        cpu.i = 0;
        cpu.r_low = 1;
        cpu.r_hi = 0;
        cpu.im = 0;
        cpu.iff1 = 0;
        cpu.iff2 = 0;
        cpu.halted = 0;
        cpu.tt += INJECTED_JP_T * cpu.rate;
    }

    ReplicateFromMaster();
    _lockedThisFrame = true;

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

        // The slave's own paging latch (only a window-routed #7FFD write
        // changes it before the lock)
        slave->pPortDecoder->UnlockPaging();
        slave->pPortDecoder->DecodePortOut(0x7FFD, _slave7FFD[m], slaveCpu.pc);
        slave->emulatorState.p7FFD = _slave7FFD[m];
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

    // The reset command registers are consumed by the reset
    if (resetRequested)
    {
        for (auto& regs : _regs)
            regs[1] = regs[2] = regs[3] = 0;
    }
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

size_t ZXPolyGroup::WindowOffset(size_t module, uint16_t address) const
{
    // The target module's own mapping; page 0 is always RAM0 through the window
    size_t page;
    switch (address >> 14)
    {
        case 0: page = 0; break;
        case 1: page = 5; break;
        case 2: page = 2; break;
        default: page = _slave7FFD[module] & 0x07u; break;
    }
    return page * PAGE_SIZE + (address & 0x3FFFu);
}

bool ZXPolyGroup::OnPortOut(size_t module, uint16_t port, uint8_t value)
{
    // #3D00 is never a ULA #FE write on ZX-Poly. Writes: master only, unlocked
    if (port == PORT_ZXPOLY_MAIN)
    {
        if (module == 0 && !_locked)
        {
            _port3D00 = value;
            if (value & MAIN_LOCK)
                PerformLock();
        }
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
            return false;
        _overlay[mapped][WindowOffset(mapped, port)] = value;
        return true;
    }

    size_t target = 0;
    size_t reg = 0;
    if (IsModuleRegisterPort(port, target, reg))
    {
        // #xxFF is the Beta 128 system port while TR-DOS is active
        if (IsTRDOSActive(module))
            return false;

        // Writes: unlocked only, and only to a module with index >= writer
        if (!_locked && module <= target)
            _regs[target][reg] = value;
        return true;
    }

    return false;
}

bool ZXPolyGroup::OnPortIn(size_t module, uint16_t port, uint8_t& value)
{
    const size_t mapped = (_port3D00 >> 5) & 0x03u;
    const bool window = module == 0 && !_locked && mapped != 0;

    if (window)
    {
        const size_t offset = WindowOffset(mapped, port);
        const int16_t written = _overlay[mapped][offset];
        // A parked slave's RAM holds only what the loader wrote; elsewhere the
        // byte is undefined on the real platform - answer with the master's
        value = written >= 0 ? static_cast<uint8_t>(written)
                             : GetContext(0)->pMemory->RAMPageAddress(
                                   static_cast<uint16_t>(offset / PAGE_SIZE))[offset % PAGE_SIZE];
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

        // R0 status: D0 HALT, D1 WAIT. Before the lock the slaves are parked
        const bool parked = !_locked && target != 0 && (_port3D00 & MAIN_NWAIT) == 0;
        value = reg == 0 ? static_cast<uint8_t>(parked ? 0x02u : 0x00u) : 0xFFu;
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
            cpu->m1TraceHook = nullptr;
            continue;
        }

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
                                          context->config.frame * 2);
}

void ZXPolyGroup::AdvanceSlaves()
{
    if (!_locked)
        return;    // loader phase: the slaves are parked

    for (size_t m = 1; m < MODULES; m++)
        RunSlaveToMasterPosition(m);
    _lockedThisFrame = false;
}

void ZXPolyGroup::RunFrame()
{
    for (auto& log : _portReads)
        log.clear();
    for (auto& log : _instructions)
        log.clear();

    ApplyPendingInput();
    _instances[0]->RunFrame(true);
    AdvanceSlaves();
}

/// region <Live mode>

void ZXPolyGroup::AttachToMaster()
{
    if (!IsCreated() || _attached)
        return;

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
    AdvanceSlaves();

    // Keys queued meanwhile reach all four keyboards before the next frame
    ApplyPendingInput();

    if (rendered && _locked)
        ComposeIntoMasterFramebuffer();
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

    // Before the lock the slaves are parked and hold no picture: the master
    // is shown as a classic screen whatever the requested mode
    const uint8_t mode = _locked ? GetVideoMode() : 0;
    ZXPolyScreenComposer::Compose(vram, mode, flashPhase, palette, out.data());
}

/// endregion </Video>
