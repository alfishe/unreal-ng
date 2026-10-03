#include "emulator/io/keyboard/profixtkbc.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

namespace
{
/// A firmware image by the same rules as the machine ROMs: as given, next to
/// the executable, then in the resources folder
bool ReadImage(const std::string& path, std::vector<uint8_t>& out)
{
    std::string resolved = FileHelper::NormalizePath(path);
    if (!FileHelper::FileExists(resolved))
        resolved = FileHelper::PathCombine(FileHelper::GetExecutablePath(), path);
    if (!FileHelper::FileExists(resolved))
        resolved = FileHelper::PathCombine(FileHelper::GetResourcesPath(), path);
    if (!FileHelper::FileExists(resolved))
        return false;
    const size_t size = FileHelper::GetFileSize(resolved);
    if (size == 0 || size > 0x1000)
        return false;
    out.resize(size);
    return FileHelper::ReadFileToBuffer(resolved, out.data(), size) == size;
}

uint32_t Crc32(const std::vector<uint8_t>& data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data)
    {
        crc ^= byte;
        for (int n = 0; n < 8; ++n)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// The board around the MCU (PROFI-XT.PDF)
constexpr uint8_t kP2Active = 0x80;     ///< P2.7: a key is held - the WAIT flip-flop's D, the latch's output enable
constexpr uint8_t kMovxLatch = 0x80;    ///< MOVX address bit 7 = 0: the output latch DD3
constexpr uint8_t kMovxRelease = 0x20;  ///< MOVX address bit 5 = 0: the WAIT flip-flop's reset
constexpr uint8_t kLatchReset = 0x80;   ///< latch bit 7 = 0: RES, X9 /HRESET
}  // namespace

ProfiXtKbc::ProfiXtKbc(EmulatorContext* context) : _context(context)
{
}

ProfiXtKbc::~ProfiXtKbc() = default;

bool ProfiXtKbc::Load(Engine engine, const std::string& romPath, std::string& error)
{
    _loaded = false;
    _cpu.reset();
    _imageNote.clear();
    _imageCrc = 0;
    _engine = engine;
    _latch = 0xFF;
    _waitSet = false;
    _inRead = false;
    _answered = false;
    _resetOut = false;
    _resetPending = false;
    _readPort = 0xFFFE;
    _answerClock = 0;
    _reads = 0;
    _lastWaitMcu = 0;
    _line = XtLine{};
    _tableClosed.fill(0);
    _tableNumLock = false;
    _tableMode2 = false;

    if (engine == Engine::Firmware)
    {
        std::vector<uint8_t> image;
        const std::string path = romPath.empty() ? std::string(kDefaultRom) : romPath;
        if (!ReadImage(path, image))
        {
            error = "PROFI-XT firmware '" + path + "' not found or unreadable";
            return false;
        }
        _imageCrc = Crc32(image);
        if (_imageCrc == kOriginalDumpCrc)
            _imageNote = "the original PROFI-XT dump (CRC 9A8E2686) never enables interrupts and receives no key; "
                         "use the reconstructed image (data/rom/profixt/README.md)";

        // 1816VE35 = 8035: 64 bytes of RAM, the program in the external EPROM (EA = 1)
        _cpu = std::make_unique<mcs48::Mcs48>(64);
        _cpu->SetProgram(std::move(image));
        mcs48::Mcs48::Bus bus;
        bus.movxWrite = [this](uint8_t address, uint8_t value) { MovxWrite(address, value); };
        bus.portOut = [this](int port, uint8_t latch) { OnPortOut(port, latch); };
        _cpu->SetBus(bus);
        _cpu->Reset();
        // Idle lines: no Z80 read (P1 = the pull-ups, T1 = no wait), no keyboard clock (/INT high)
        _cpu->SetPins(1, 0xFF);
        _cpu->SetT0(true);
        _cpu->SetT1(true);
        _cpu->SetInt(true);
    }

    _tBase = NowBase();
    _lastNow = _tBase;
    _mcuBase = 0;
    _frac = 0;
    _loaded = true;
    return true;
}

std::string ProfiXtKbc::ControllerName() const
{
    return _engine == Engine::Table ? "PROFI-XT table" : "PROFI-XT firmware 1.27";
}

/// region <Time>

uint64_t ProfiXtKbc::BaseHz() const
{
    if (_timeSource)
        return _timeSourceHz;
    return (_context && _context->emulatorState.base_z80_frequency) ? _context->emulatorState.base_z80_frequency
                                                                     : 3500000u;
}

uint64_t ProfiXtKbc::NowBase() const
{
    if (_timeSource)
        return _timeSource();
    if (!_context || !_context->pCore || !_context->pCore->GetZ80())
        return 0;
    return _context->emulatorState.t_states + CpuTToBaseT(_context->pCore->GetZ80()->t);
}

uint64_t ProfiXtKbc::CpuTToBaseT(uint64_t cpuT) const
{
    // The ONE place CPU clocks of the current frame become base T-states (1 / base_z80_frequency, 3.5 MHz). It must
    // follow the board's clock ratio: the Profi's hi-res mode runs the CPU at a non-integer ratio of 3.5 MHz
    // (v5 5 MHz = 10/7, v3 3 MHz = 6/7) - point this at the EmulatorState conversion helper once it exists
    const uint32_t multiplier = _context->emulatorState.current_z80_frequency_multiplier
                                    ? _context->emulatorState.current_z80_frequency_multiplier
                                    : 1u;
    return cpuT / multiplier;
}

uint64_t ProfiXtKbc::McuClockAt(uint64_t baseT) const
{
    if (baseT < _tBase)
        return _mcuBase;
    return _mcuBase + ((baseT - _tBase) * kCrystalHz + _frac) / BaseHz();
}

void ProfiXtKbc::CatchUp(uint64_t baseT)
{
    if (!_cpu)
        return;
    if (baseT < _lastNow)
    {
        // The machine's clock restarted (a reset, a snapshot): time continues from here
        _tBase = baseT;
        _mcuBase = _cpu->Clock();
        _frac = 0;
    }
    _lastNow = baseT;
    RunTo(McuClockAt(baseT), false);
}

void ProfiXtKbc::OnFrameEnd()
{
    if (!_loaded || !_cpu)
        return;
    const uint64_t now = NowBase();
    CatchUp(now);
    FireReset();
    // Move the base forward, keeping the remainder: the numbers stay small, no drift
    const uint64_t baseHz = BaseHz();
    if (now > _tBase)
    {
        const uint64_t scaled = (now - _tBase) * kCrystalHz + _frac;
        _mcuBase += scaled / baseHz;
        _frac = scaled % baseHz;
        _tBase = now;
    }
}

void ProfiXtKbc::RunTo(uint64_t target, bool untilAnswer)
{
    while (_cpu->Clock() < target)
    {
        LineProcess(_cpu->Clock());
        const uint64_t next = std::min(target, std::max(LineNextEvent(), _cpu->Clock() + 1));
        _cpu->Run(next);
        if (untilAnswer && _answered)
            break;
    }
    LineProcess(_cpu->Clock());
}

/// endregion </Time>

/// region <The XT keyboard>

void ProfiXtKbc::OnPcKey(PcKey key, bool pressed)
{
    if (!_loaded || key == PcKey::None || key >= PcKey::Count)
        return;
    const uint8_t index = static_cast<uint8_t>(key);
    uint8_t& byte = _line.held[index >> 3];
    const uint8_t mask = static_cast<uint8_t>(1u << (index & 7));
    if (pressed == ((byte & mask) != 0) && key != PcKey::Pause)
        return;   // no change
    byte = static_cast<uint8_t>(pressed ? (byte | mask) : (byte & ~mask));

    if (_engine == Engine::Table)
    {
        TableKey(key, pressed);
        FireReset();
        return;
    }

    CatchUp(NowBase());
    LineEnqueue(pckey::XtSet1Bytes(key, pressed));
    // Typematic: the last key made repeats its make code while held (500 ms, then 10.9 per second)
    if (pressed && key != PcKey::Pause)
    {
        _line.repeatKey = index;
        _line.repeatAt = _cpu->Clock() + MicrosToClocks(kTypematicDelayMicros);
    }
    else if (!pressed && index == _line.repeatKey)
        _line.repeatKey = 0;
    FireReset();
}

void ProfiXtKbc::ReleaseAllPcKeys()
{
    for (int n = 1; n < static_cast<int>(PcKey::Count); ++n)
    {
        if (_line.held[n >> 3] & (1u << (n & 7)))
            OnPcKey(static_cast<PcKey>(n), false);
    }
}

bool ProfiXtKbc::KeyHeld(PcKey key) const
{
    const uint8_t index = static_cast<uint8_t>(key);
    return index < static_cast<uint8_t>(PcKey::Count) && (_line.held[index >> 3] & (1u << (index & 7))) != 0;
}

void ProfiXtKbc::LineEnqueue(const std::vector<uint8_t>& bytes)
{
    for (uint8_t b : bytes)
    {
        if (_line.count >= sizeof(_line.queue))
            return;   // the keyboard's buffer is full: the byte is lost
        _line.queue[(_line.head + _line.count) % sizeof(_line.queue)] = b;
        ++_line.count;
    }
}

uint64_t ProfiXtKbc::LineNextEvent() const
{
    uint64_t next = UINT64_MAX;
    if (_line.sending)
        next = _line.nextEdge;
    else if (_line.count)
        next = _line.idleUntil;
    if (_line.repeatKey)
        next = std::min(next, _line.repeatAt);
    return next;
}

void ProfiXtKbc::LineProcess(uint64_t clock)
{
    const uint64_t bit = MicrosToClocks(kBitMicros);
    for (int guard = 0; guard < 64; ++guard)
    {
        if (_line.repeatKey && clock >= _line.repeatAt)
        {
            LineEnqueue(pckey::XtSet1Bytes(static_cast<PcKey>(_line.repeatKey), true));
            _line.repeatAt += MicrosToClocks(kTypematicPeriodMicros);
            continue;
        }
        if (!_line.sending)
        {
            if (!_line.count || clock < _line.idleUntil)
                return;
            _line.frameByte = _line.queue[_line.head];
            _line.head = static_cast<uint8_t>((_line.head + 1) % sizeof(_line.queue));
            --_line.count;
            _line.sending = 1;
            _line.bit = 0;
            _line.phase = 0;
            _line.nextEdge = std::max(clock, _line.idleUntil);
        }
        if (clock < _line.nextEdge)
            return;
        if (_line.phase == 0)
        {
            // A clock edge: the data bit is latched onto T0 inverted (DD5 /Q), the differentiator pulls /INT low.
            // The frame is a start bit (1) and 8 data bits, LSB first
            const bool level = _line.bit == 0 ? true : ((_line.frameByte >> (_line.bit - 1)) & 1) != 0;
            _cpu->SetT0(!level);
            _cpu->SetInt(false);
            _line.phase = 1;
            _line.nextEdge += kIntPulseClocks;
        }
        else
        {
            _cpu->SetInt(true);
            _line.phase = 0;
            _line.nextEdge += bit - kIntPulseClocks;
            if (++_line.bit > 8)
            {
                _line.sending = 0;
                _line.idleUntil = _line.nextEdge + MicrosToClocks(kByteGapMicros);
            }
        }
    }
}

/// endregion </The XT keyboard>

/// region <The board>

void ProfiXtKbc::MovxWrite(uint8_t address, uint8_t value)
{
    // DD3 is clocked by OR(A7, /WR), the WAIT flip-flop reset by OR(A5, /WR)
    if (!(address & kMovxLatch))
    {
        _latch = value;
        UpdateResetLine();
    }
    if (!(address & kMovxRelease) && _waitSet)
    {
        _waitSet = false;
        _cpu->SetT1(true);
        if (_inRead)
        {
            _answered = true;
            _answerClock = _cpu->Clock() + 2u * mcs48::Mcs48::kClocksPerCycle;   // the end of the MOVX
            _cpu->RequestStop();
        }
    }
}

void ProfiXtKbc::OnPortOut(int port, [[maybe_unused]] uint8_t latch)
{
    if (port == 2)
        UpdateResetLine();
}

void ProfiXtKbc::UpdateResetLine()
{
    // RES is latch bit 7, on X9 only while P2.7 enables the latch's outputs
    const bool asserted = _cpu && (_cpu->Latch(2) & kP2Active) && !(_latch & kLatchReset);
    if (asserted && !_resetOut)
        _resetPending = true;
    _resetOut = asserted;
}

void ProfiXtKbc::FireReset()
{
    // After the MCU run: /HRESET resets the machine (the controller has its own power-on reset and keeps running)
    if (!_resetPending)
        return;
    _resetPending = false;
    if (_context && _context->pSoftResetSink)
        _context->pSoftResetSink->RequestSoftReset();
}

void ProfiXtKbc::AddZ80Wait(uint64_t mcuClocks)
{
    if (!mcuClocks || !_context || !_context->pCore || !_context->pCore->GetZ80())
        return;
    // The Z80 waited from the read until the release, in CPU clocks at its current speed (current_z80_frequency is
    // the real CPU clock, whatever ratio the board runs at)
    const uint64_t cpuHz = _context->emulatorState.current_z80_frequency ? _context->emulatorState.current_z80_frequency
                                                                         : _context->emulatorState.base_z80_frequency;
    const uint64_t clocks = (mcuClocks * cpuHz + kCrystalHz - 1) / kCrystalHz;
    _context->pCore->GetZ80()->AddWaitStates(static_cast<uint32_t>(clocks));
}

uint8_t ProfiXtKbc::ReadPort(uint16_t port)
{
    if (!_loaded)
        return 0xFF;
    ++_reads;
    _readPort = port;
    if (_engine == Engine::Table)
        return TableRead(port);

    CatchUp(NowBase());
    // /CSKBD clocks the WAIT flip-flop with D = P2.7: no key held, no wait, the latch is off X9 (pull-ups)
    if (!(_cpu->Latch(2) & kP2Active))
    {
        _lastWaitMcu = 0;
        FireReset();
        return 0xFF;
    }

    _waitSet = true;
    _answered = false;
    _inRead = true;
    _cpu->SetT1(false);
    _cpu->SetPins(1, static_cast<uint8_t>(port >> 8));   // P1 = BA8..BA15, held while the Z80 waits
    const uint64_t start = _cpu->Clock();
    RunTo(start + kCrystalHz / 20, true);   // 50 ms: the hardware would hang here
    _inRead = false;
    _cpu->SetPins(1, 0xFF);

    if (!_answered)
    {
        _waitSet = false;
        _cpu->SetT1(true);
        _lastWaitMcu = 0;
        if (_context && _context->pModuleLogger)
        {
            ModuleLogger* _logger = _context->pModuleLogger;
            const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
            const uint16_t _SUBMODULE = 0x0000;
            MLOGWARNING("ProfiXtKbc: IN #%04X not answered in 50 ms (PC %03X)", port, _cpu->Pc());
        }
        FireReset();
        return 0xFF;
    }

    _lastWaitMcu = _answerClock > start ? _answerClock - start : 0;
    AddZ80Wait(_lastWaitMcu);
    const uint8_t result = static_cast<uint8_t>(_latch | 0xC0);
    FireReset();
    return result;
}

/// endregion </The board>

/// region <Table engine>

profixt::MatrixMask ProfiXtKbc::TableClosed() const
{
    profixt::MatrixMask closed = 0;
    for (uint64_t mask : _tableClosed)
        closed |= mask;
    return closed;
}

void ProfiXtKbc::TableKey(PcKey key, bool pressed)
{
    const size_t index = static_cast<size_t>(key);
    if (!pressed)
    {
        _tableClosed[index] = 0;
        return;
    }
    // The lock keys toggle on make (the firmware's Num Lock and Scroll Lock flags)
    if (key == PcKey::NumLock)
        _tableNumLock = !_tableNumLock;
    else if (key == PcKey::ScrollLock)
        _tableMode2 = !_tableMode2;

    profixt::KeyContext context;
    context.shift = (KeyHeld(PcKey::LeftShift) && key != PcKey::LeftShift) ||
                    (KeyHeld(PcKey::RightShift) && key != PcKey::RightShift);
    context.numLock = _tableNumLock;
    context.mode2 = _tableMode2;
    _tableClosed[index] = profixt::Translate(key, context);

    // Any Ctrl + any Alt + Del (the keypad's, or the E0 one): latch 7Fh, /HRESET
    const bool ctrl = KeyHeld(PcKey::LeftCtrl) || KeyHeld(PcKey::RightCtrl);
    const bool alt = KeyHeld(PcKey::LeftAlt) || KeyHeld(PcKey::RightAlt);
    if (ctrl && alt && (key == PcKey::Delete || key == PcKey::KeypadDecimal))
        _resetPending = true;
}

uint8_t ProfiXtKbc::TableRead(uint16_t port)
{
    const profixt::MatrixMask closed = TableClosed();
    // No key held: P2.7 = 0, no wait, the pull-ups
    if (closed == 0)
    {
        _lastWaitMcu = 0;
        return 0xFF;
    }
    const uint8_t high = static_cast<uint8_t>(port >> 8);
    uint8_t answer = 0x3F;
    if (high == 0xAA || high == 0x55)
    {
        // The mode switch: #AAFE on, #55FE off; the latch gets 80h (KD0..KD5 = 0)
        _tableMode2 = high == 0xAA;
        answer = 0x00;
    }
    else if (high == 0x00)
    {
        for (int row = 0; row < 8; ++row)
            answer &= profixt::RowBits(closed, row);
    }
    else
    {
        // The firmware's half-row decode (its table at 400h): A8..A11 first - exactly one low selects that half-row
        // and A12..A15 are not looked at; none low: A12..A15 the same way; anything else is "no key"
        auto single = [](uint8_t nibble) -> int {
            for (int n = 0; n < 4; ++n)
            {
                if (nibble == static_cast<uint8_t>(0x0F & ~(1u << n)))
                    return n;
            }
            return -1;
        };
        const uint8_t low = static_cast<uint8_t>(high & 0x0F);
        int row = -1;
        if (low != 0x0F)
            row = single(low);
        else
        {
            const int upper = single(static_cast<uint8_t>(high >> 4));
            row = upper >= 0 ? upper + 4 : -1;
        }
        if (row >= 0)
            answer = profixt::RowBits(closed, row);
    }
    _lastWaitMcu = kTableWaitClocks;
    AddZ80Wait(_lastWaitMcu);
    return static_cast<uint8_t>(answer | 0xC0);
}

/// endregion </Table engine>

/// region <Reports>

uint8_t ProfiXtKbc::Row(int row) const
{
    row &= 7;
    if (_engine == Engine::Table)
        return profixt::RowBits(TableClosed(), row);
    return _cpu ? static_cast<uint8_t>(_cpu->Ram(static_cast<uint8_t>(0x20 + row)) & 0x3F) : 0x3F;
}

bool ProfiXtKbc::Mode2() const
{
    if (_engine == Engine::Table)
        return _tableMode2;
    // Firmware: R7 of bank 0, bit 6 (set by #AAFE, cleared by #55FE, toggled by Scroll Lock)
    return _cpu && (_cpu->Ram(7) & 0x40) != 0;
}

bool ProfiXtKbc::NumLock() const
{
    if (_engine == Engine::Table)
        return _tableNumLock;
    // Firmware: R7 of bank 0, bit 4 clear after Num Lock (set at power-on)
    return _cpu && (_cpu->Ram(7) & 0x10) == 0;
}

/// endregion </Reports>

/// region <Automation maps>

std::vector<PcKey> ProfiXtKbc::PcKeysForZxKey(ZXKeysEnum key) const
{
    // The controller maps Ctrl to Caps Shift and Shift to Symbol Shift; letters, digits, Enter and Space are 1:1.
    // A ZX key that is a combination presses its two matrix keys
    ZXKeysEnum modifier = ZXKEY_NONE;
    ZXKeysEnum base = key;
    switch (key)
    {
        case ZXKEY_EXT_UP: modifier = ZXKEY_CAPS_SHIFT; base = ZXKEY_7; break;
        case ZXKEY_EXT_DOWN: modifier = ZXKEY_CAPS_SHIFT; base = ZXKEY_6; break;
        case ZXKEY_EXT_LEFT: modifier = ZXKEY_CAPS_SHIFT; base = ZXKEY_5; break;
        case ZXKEY_EXT_RIGHT: modifier = ZXKEY_CAPS_SHIFT; base = ZXKEY_8; break;
        case ZXKEY_EXT_DELETE: modifier = ZXKEY_CAPS_SHIFT; base = ZXKEY_0; break;
        case ZXKEY_EXT_BREAK: modifier = ZXKEY_CAPS_SHIFT; base = ZXKEY_SPACE; break;
        case ZXKEY_EXT_EDIT: modifier = ZXKEY_CAPS_SHIFT; base = ZXKEY_1; break;
        case ZXKEY_EXT_CAPSLOCK: modifier = ZXKEY_CAPS_SHIFT; base = ZXKEY_2; break;
        case ZXKEY_EXT_DOT: modifier = ZXKEY_SYM_SHIFT; base = ZXKEY_M; break;
        case ZXKEY_EXT_COMMA: modifier = ZXKEY_SYM_SHIFT; base = ZXKEY_N; break;
        case ZXKEY_EXT_PLUS: modifier = ZXKEY_SYM_SHIFT; base = ZXKEY_K; break;
        case ZXKEY_EXT_MINUS: modifier = ZXKEY_SYM_SHIFT; base = ZXKEY_J; break;
        case ZXKEY_EXT_MULTIPLY: modifier = ZXKEY_SYM_SHIFT; base = ZXKEY_B; break;
        case ZXKEY_EXT_DIVIDE: modifier = ZXKEY_SYM_SHIFT; base = ZXKEY_V; break;
        case ZXKEY_EXT_EQUAL: modifier = ZXKEY_SYM_SHIFT; base = ZXKEY_L; break;
        case ZXKEY_EXT_BAR: modifier = ZXKEY_SYM_SHIFT; base = ZXKEY_S; break;
        case ZXKEY_EXT_BACKSLASH: modifier = ZXKEY_SYM_SHIFT; base = ZXKEY_D; break;
        case ZXKEY_EXT_DBLQUOTE: modifier = ZXKEY_SYM_SHIFT; base = ZXKEY_P; break;
        default: break;
    }
    auto one = [](ZXKeysEnum zx) -> PcKey {
        if (zx == ZXKEY_CAPS_SHIFT)
            return PcKey::LeftCtrl;
        if (zx == ZXKEY_SYM_SHIFT)
            return PcKey::LeftShift;
        if (zx >= ZXKEY_EXT_CTRL)
            return PcKey::None;
        const std::vector<PcKey> keys = pckey::FromZxKey(zx);   // letters, digits, Enter, Space: one key each
        return keys.size() == 1 ? keys[0] : PcKey::None;
    };
    std::vector<PcKey> out;
    if (modifier != ZXKEY_NONE)
        out.push_back(one(modifier));
    if (const PcKey pc = one(base); pc != PcKey::None)
        out.push_back(pc);
    if (modifier != ZXKEY_NONE && out.size() < 2)
        out.clear();
    return out;
}

std::vector<PcKey> ProfiXtKbc::PcKeysForCharacter([[maybe_unused]] char c, const std::vector<ZXKeysEnum>& zxKeys) const
{
    std::vector<PcKey> out;
    for (ZXKeysEnum zx : zxKeys)
    {
        for (PcKey pc : PcKeysForZxKey(zx))
        {
            if (std::find(out.begin(), out.end(), pc) == out.end())
                out.push_back(pc);
        }
    }
    return out;
}

/// endregion </Automation maps>

/// region <TTD>

void ProfiXtKbc::SaveState(State& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.version = kStateVersion;
    out.engine = static_cast<uint8_t>(_engine);
    out.latch = _latch;
    out.waitSet = _waitSet ? 1 : 0;
    out.inRead = _inRead ? 1 : 0;
    out.answered = _answered ? 1 : 0;
    out.resetOut = _resetOut ? 1 : 0;
    out.resetPending = _resetPending ? 1 : 0;
    out.tableNumLock = _tableNumLock ? 1 : 0;
    out.tableMode2 = _tableMode2 ? 1 : 0;
    out.readPort = _readPort;
    out.tBase = _tBase;
    out.mcuBase = _mcuBase;
    out.frac = _frac;
    out.lastNow = _lastNow;
    out.answerClock = _answerClock;
    out.reads = _reads;
    out.lastWaitMcu = _lastWaitMcu;
    if (_cpu)
        _cpu->SaveState(out.cpu);
    out.line = _line;
    for (size_t n = 0; n < _tableClosed.size(); ++n)
        out.tableClosed[n] = _tableClosed[n];
}

bool ProfiXtKbc::LoadState(const State& in)
{
    if (!_loaded || in.version != kStateVersion || in.engine != static_cast<uint8_t>(_engine))
        return false;
    _latch = in.latch;
    _waitSet = in.waitSet != 0;
    _inRead = in.inRead != 0;
    _answered = in.answered != 0;
    _resetOut = in.resetOut != 0;
    _resetPending = in.resetPending != 0;
    _tableNumLock = in.tableNumLock != 0;
    _tableMode2 = in.tableMode2 != 0;
    _readPort = in.readPort;
    _tBase = in.tBase;
    _mcuBase = in.mcuBase;
    _frac = in.frac;
    _lastNow = in.lastNow;
    _answerClock = in.answerClock;
    _reads = in.reads;
    _lastWaitMcu = in.lastWaitMcu;
    if (_cpu)
        _cpu->LoadState(in.cpu);
    _line = in.line;
    for (size_t n = 0; n < _tableClosed.size(); ++n)
        _tableClosed[n] = in.tableClosed[n];
    return true;
}

/// endregion </TTD>
