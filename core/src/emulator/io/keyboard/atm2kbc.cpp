#include "emulator/io/keyboard/atm2kbc.h"

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
constexpr Atm2Kbc::FirmwareInfo kFirmware[] = {
    {Atm2Kbc::Firmware::None, "NONE", nullptr, 0, false, false, "no controller: #FE is the plain matrix port"},
    {Atm2Kbc::Firmware::V22At7, "V22-7", "rom/atm2kbc/at22-7mhz.bin", 7000000, false, false, "2.2 (2005), 7 MHz, no RS-232"},
    {Atm2Kbc::Firmware::V22At11, "V22-11", "rom/atm2kbc/at22-11mhz.bin", 11059200, false, false, "2.2 (2005), 11.0592 MHz, no RS-232"},
    {Atm2Kbc::Firmware::V22At12, "V22-12", "rom/atm2kbc/at22-12mhz.bin", 12000000, false, false, "2.2 (2005), 12 MHz, no RS-232"},
    {Atm2Kbc::Firmware::V31At7, "V31-7", "rom/atm2kbc/at31-7mhz.bin", 7000000, true, true, "3.1 (2006), 7 MHz, RS-232"},
    {Atm2Kbc::Firmware::V31At11, "V31-11", "rom/atm2kbc/at31-11mhz.bin", 11059200, true, true, "3.1 (2006), 11.0592 MHz, RS-232"},
    {Atm2Kbc::Firmware::V32At7, "V32-7", "rom/atm2kbc/at32m-7mhz.bin", 7000000, false, true, "3.2m (2019), 7 MHz (stock F0), RS-232"},
    {Atm2Kbc::Firmware::V32At11, "V32-11", "rom/atm2kbc/at32m-11mhz.bin", 11059200, false, true, "3.2m (2019), 11.0592 MHz, RS-232"},
    {Atm2Kbc::Firmware::V40, "V40", "rom/atm2kbc/at40.bin", 11059200, true, true, "4.0 (2021, AT89S52), exact 38400 / 115200"},
    {Atm2Kbc::Firmware::V41, "V41", "rom/atm2kbc/at41.bin", 11059200, true, true, "4.1 (2023, AT89S52): clock kept, RX overflow INT"},
};

bool SameText(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b)
    {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 32);
        if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 32);
        if (x != y)
            return false;
    }
    return *a == 0 && *b == 0;
}

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
    if (size == 0 || size > 0x10000)
        return false;
    out.resize(size);
    return FileHelper::ReadFileToBuffer(resolved, out.data(), size) == size;
}

// MCU port bits (firmware atm_at32.asm :37-54, schematic cp7_2)
constexpr uint8_t kP1IntT = 0x20;    ///< P1.5: low = Z80 /INT
constexpr uint8_t kP1Reset = 0x40;   ///< P1.6: low = Z80 /RES
constexpr uint8_t kP1WOn = 0x80;     ///< P1.7: 1 = no WAIT generation
}  // namespace

const Atm2Kbc::FirmwareInfo* Atm2Kbc::Info(Firmware firmware)
{
    for (const FirmwareInfo& info : kFirmware)
    {
        if (info.firmware == firmware)
            return &info;
    }
    return nullptr;
}

bool Atm2Kbc::ParseFirmware(const char* text, Firmware& out)
{
    if (!text || !*text)
    {
        out = kDefaultFirmware;
        return true;
    }
    for (const FirmwareInfo& info : kFirmware)
    {
        if (SameText(text, info.name))
        {
            out = info.firmware;
            return true;
        }
    }
    return false;
}

const char* Atm2Kbc::FirmwareName(Firmware firmware)
{
    const FirmwareInfo* info = Info(firmware);
    return info ? info->name : "?";
}

Atm2Kbc::Atm2Kbc(EmulatorContext* context) : _context(context)
{
}

Atm2Kbc::~Atm2Kbc()
{
    if (_context && _context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->SetDeviceIntLine(Z80::kDeviceIntAtm2Kbc, false);
}

bool Atm2Kbc::Load(Firmware firmware, const std::string& romPath, std::string& error)
{
    _cpu.reset();
    _firmware = Firmware::None;
    _crystalHz = 0;
    if (_context && _context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->SetDeviceIntLine(Z80::kDeviceIntAtm2Kbc, false);
    if (firmware == Firmware::None)
        return true;
    const FirmwareInfo* info = Info(firmware);
    if (!info)
    {
        error = "unknown keyboard controller firmware";
        return false;
    }
    std::vector<uint8_t> image;
    const std::string path = romPath.empty() ? std::string(info->file) : romPath;
    if (!ReadImage(path, image))
    {
        error = "keyboard controller firmware '" + path + "' not found or unreadable";
        return false;
    }

    _cpu = std::make_unique<mcs51::Mcs51>(info->i8052 ? mcs51::Mcs51::Variant::I8052 : mcs51::Mcs51::Variant::I8051);
    _cpu->SetProgram(std::move(image));
    mcs51::Mcs51::Bus bus;
    bus.movxRead = [this](uint16_t address) { return MovxRead(address); };
    bus.movxWrite = [this](uint16_t address, uint8_t value) { MovxWrite(address, value); };
    bus.portOut = [this](int port, uint8_t latch) { OnPortOut(port, latch); };
    _cpu->SetBus(bus);
    _cpu->Reset();
    // Idle lines: PS/2 clock and data high, RXD high (mark), VE1 per #FF77
    _cpu->SetPin(3, 4, _board.ve1);

    _firmware = firmware;
    _crystalHz = info->crystalHz;
    _board = Board{_board.latchedHigh, 0xFF, false, _board.ve1, false};
    _resetLow = false;
    _p3 = 0xFF;
    _kbd = Keyboard{};
    _reads = 0;
    _lastWaitMcu = 0;
    _tBase = NowBase();
    _lastNow = _tBase;
    _mcuBase = 0;
    _frac = 0;
    return true;
}

uint64_t Atm2Kbc::NowBase() const
{
    if (!_context || !_context->pCore || !_context->pCore->GetZ80())
        return 0;
    const uint32_t multiplier = _context->emulatorState.current_z80_frequency_multiplier
                                    ? _context->emulatorState.current_z80_frequency_multiplier
                                    : 1u;
    return _context->emulatorState.t_states + _context->pCore->GetZ80()->t / multiplier;
}

uint64_t Atm2Kbc::McuClockAt(uint64_t baseT)
{
    if (baseT < _tBase)
        return _mcuBase;
    const uint64_t baseHz = (_context && _context->emulatorState.base_z80_frequency)
                                ? _context->emulatorState.base_z80_frequency
                                : 3500000u;
    return _mcuBase + ((baseT - _tBase) * _crystalHz + _frac) / baseHz;
}

void Atm2Kbc::Rebase()
{
    if (!Present())
        return;
    const uint64_t now = NowBase();
    _tBase = now;
    _lastNow = now;
    _mcuBase = _cpu->Clock();
    _frac = 0;
}

void Atm2Kbc::CatchUp(uint64_t baseT)
{
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

void Atm2Kbc::RunTo(uint64_t target, bool untilAnswer)
{
    // A run can also end early (the MCU reset itself): go on to the target
    while (_cpu->Clock() < target)
    {
        KeyboardProcess(_cpu->Clock());
        const uint64_t next = std::min(target, std::max(KeyboardNextEvent(), _cpu->Clock() + 1));
        _cpu->Run(next);
        if (untilAnswer && _board.answered)
            break;
    }
    KeyboardProcess(_cpu->Clock());
}

/// region <PC keyboard>

void Atm2Kbc::OnPcKey(PcKey key, bool pressed)
{
    if (!Present() || key == PcKey::None || key >= PcKey::Count)
        return;
    CatchUp(NowBase());
    const uint8_t index = static_cast<uint8_t>(key);
    uint8_t& byte = _kbd.held[index >> 3];
    const uint8_t mask = static_cast<uint8_t>(1u << (index & 7));
    if (pressed == ((byte & mask) != 0) && key != PcKey::Pause)
        return;   // no change
    byte = static_cast<uint8_t>(pressed ? (byte | mask) : (byte & ~mask));
    KeyboardEnqueue(pckey::Ps2Set2Bytes(key, pressed));

    // Typematic: the last key made repeats while held (power-on default 500 ms, 10.9 / s)
    if (pressed && key != PcKey::Pause)
    {
        _kbd.repeatKey = key;
        _kbd.repeatAt = _cpu->Clock() + static_cast<uint64_t>(_crystalHz) / 2;
    }
    else if (!pressed && key == _kbd.repeatKey)
        _kbd.repeatKey = PcKey::None;
}

void Atm2Kbc::ReleaseAllPcKeys()
{
    for (int n = 0; n < static_cast<int>(PcKey::Count); ++n)
    {
        if (_kbd.held[n >> 3] & (1u << (n & 7)))
            OnPcKey(static_cast<PcKey>(n), false);
    }
}

void Atm2Kbc::KeyboardEnqueue(const std::vector<uint8_t>& bytes)
{
    for (uint8_t b : bytes)
    {
        if (_kbd.count >= _kbd.queue.size())
        {
            _kbd.overflow = 1;   // the keyboard sends 00 once there is room
            return;
        }
        _kbd.queue[(_kbd.head + _kbd.count) % _kbd.queue.size()] = b;
        ++_kbd.count;
    }
}

uint64_t Atm2Kbc::KeyboardNextEvent() const
{
    uint64_t next = UINT64_MAX;
    if (_kbd.sending)
        next = _kbd.nextEdge;
    else if (_kbd.count || _kbd.overflow)
        next = _kbd.idleUntil;
    if (_kbd.repeatKey != PcKey::None)
        next = std::min(next, _kbd.repeatAt);
    return next;
}

void Atm2Kbc::KeyboardProcess(uint64_t clock)
{
    const uint64_t bit = KeyboardBitClocks();
    for (int guard = 0; guard < 64; ++guard)
    {
        if (_kbd.repeatKey != PcKey::None && clock >= _kbd.repeatAt)
        {
            KeyboardEnqueue(pckey::Ps2Set2Bytes(_kbd.repeatKey, true));
            _kbd.repeatAt += static_cast<uint64_t>(_crystalHz) * 10u / 109u;   // 10.9 per second
            continue;
        }
        if (!_kbd.sending)
        {
            if ((!_kbd.count && !_kbd.overflow) || clock < _kbd.idleUntil)
                return;
            if (_kbd.overflow && _kbd.count < _kbd.queue.size())
            {
                _kbd.frameByte = 0x00;
                _kbd.overflow = 0;
            }
            else
            {
                _kbd.frameByte = _kbd.queue[_kbd.head];
                _kbd.head = static_cast<uint8_t>((_kbd.head + 1) % _kbd.queue.size());
                --_kbd.count;
            }
            _kbd.sending = true;
            _kbd.bit = 0;
            _kbd.phase = 0;
            _kbd.nextEdge = std::max(clock, _kbd.idleUntil);
        }
        if (clock < _kbd.nextEdge)
            return;
        // One line change: data, then clock low (the MCU samples data on the falling edge), then clock high
        switch (_kbd.phase)
        {
            case 0:
            {
                bool level = true;
                if (_kbd.bit == 0)
                    level = false;   // start bit
                else if (_kbd.bit <= 8)
                    level = (_kbd.frameByte >> (_kbd.bit - 1)) & 1;
                else if (_kbd.bit == 9)
                {
                    uint8_t v = _kbd.frameByte;
                    int ones = 0;
                    for (; v; v = static_cast<uint8_t>(v & (v - 1)))
                        ++ones;
                    level = (ones & 1) == 0;   // odd parity
                }
                _cpu->SetPin(3, 5, level);
                _kbd.phase = 1;
                _kbd.nextEdge += bit / 4;
                break;
            }
            case 1:
                _cpu->SetPin(3, 2, false);
                _kbd.phase = 2;
                _kbd.nextEdge += bit / 2;
                break;
            default:
                _cpu->SetPin(3, 2, true);
                _kbd.phase = 0;
                _kbd.nextEdge += bit / 4;
                if (++_kbd.bit > 10)
                {
                    _kbd.sending = false;
                    _cpu->SetPin(3, 5, true);
                    _kbd.idleUntil = _kbd.nextEdge + bit;   // a bit time between frames
                }
                break;
        }
    }
}

/// endregion </PC keyboard>

void Atm2Kbc::OnFrameEnd()
{
    if (!Present())
        return;
    const uint64_t now = NowBase();
    CatchUp(now);
    // Move the base forward, keeping the remainder: the numbers stay small, no drift
    const uint64_t baseHz = (_context && _context->emulatorState.base_z80_frequency)
                                ? _context->emulatorState.base_z80_frequency
                                : 3500000u;
    if (now > _tBase)
    {
        const uint64_t scaled = (now - _tBase) * _crystalHz + _frac;
        _mcuBase += scaled / baseHz;
        _frac = scaled % baseHz;
        _tBase = now;
    }
}

void Atm2Kbc::SetVe1(bool ve1)
{
    if (ve1 == _board.ve1)
        return;
    if (Present())
        CatchUp(NowBase());
    _board.ve1 = ve1;
    if (Present())
        _cpu->SetPin(3, 4, ve1);
}

uint8_t Atm2Kbc::ReadPort(uint16_t port)
{
    if (!Present())
        return _native ? _native(port) : 0xFF;
    ++_reads;
    CatchUp(NowBase());

    _board.latchedHigh = static_cast<uint8_t>(port >> 8);
    _readPort = port;
    const bool waitOff = _board.ve1 || (_cpu->Latch(1) & kP1WOn);

    // /KEYRD reaches INT1 on every read; the WAIT flip-flop only when W_ON = 0
    _cpu->SetPin(3, 3, false);
    if (waitOff)
    {
        _cpu->SetPin(3, 3, true);
        return _native ? _native(port) : 0xFF;   // open question: the bus without a served read
    }

    _board.waitSet = true;
    _board.answered = false;
    _inRead = true;
    const uint64_t start = _cpu->Clock();
    const uint64_t limit = start + _crystalHz / 20;   // 50 ms: the hardware would hang here
    RunTo(limit, true);
    _inRead = false;
    _cpu->SetPin(3, 3, true);

    if (!_board.answered)
    {
        _board.waitSet = false;
        if (_context && _context->pModuleLogger)
        {
            ModuleLogger* _logger = _context->pModuleLogger;
            const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
            const uint16_t _SUBMODULE = 0x0000;
            MLOGWARNING("Atm2Kbc: IN #%04X not answered in 50 ms (firmware %s, PC %04X)", port,
                        FirmwareName(_firmware), _cpu->Pc());
        }
        return 0xFF;
    }

    // The Z80 waited from the read until the /VWR, in CPU clocks at its current speed
    _lastWaitMcu = _answerClock > start ? _answerClock - start : 0;
    if (_context && _context->pCore && _context->pCore->GetZ80())
    {
        const uint64_t cpuHz = _context->emulatorState.current_z80_frequency
                                   ? _context->emulatorState.current_z80_frequency
                                   : _context->emulatorState.base_z80_frequency;
        const uint64_t clocks = (_lastWaitMcu * cpuHz + _crystalHz - 1) / _crystalHz;
        _context->pCore->GetZ80()->AddWaitStates(static_cast<uint32_t>(clocks));
    }
    return _board.dataOut;
}

uint8_t Atm2Kbc::MovxRead(uint16_t address)
{
    // P2.0 (address bit 8) = 1: the native port D45; 0: the latched A15..A8
    if (address & 0x0100)
    {
        const uint16_t port = static_cast<uint16_t>((_board.latchedHigh << 8) | (_readPort & 0xFF));
        return _native ? _native(port) : 0xFF;
    }
    return _board.latchedHigh;
}

void Atm2Kbc::MovxWrite(uint16_t address, uint8_t value)
{
    if (!(address & 0x0100))
        return;
    DataStrobe(value, 24);   // the write strobe is in MOVX's second machine cycle
}

void Atm2Kbc::DataStrobe(uint8_t value, uint64_t offset)
{
    // D102 drives the Z80 data bus; /VWR presets the WAIT flip-flop
    _board.dataOut = value;
    if (_board.waitSet)
    {
        _board.waitSet = false;
        if (_inRead)
        {
            _board.answered = true;
            _answerClock = _cpu->Clock() + offset;
            _cpu->RequestStop();
        }
    }
}

void Atm2Kbc::OnPortOut(int port, uint8_t latch)
{
    if (port == 3)
    {
        // Firmware built with en_movx = 0 (3.1, 4.1) strobes the bus by hand:
        // /VWR (P3.6) latches P0 into D102, /VRD (P3.7) puts the latch D23 or
        // the native port D45 (P2.0 selects, as VA8) on P0
        const uint8_t falling = static_cast<uint8_t>(_p3 & ~latch);
        const uint8_t rising = static_cast<uint8_t>(latch & ~_p3);
        _p3 = latch;
        const bool data = (_cpu->Latch(2) & 0x01) != 0;
        if ((falling & 0x40) && data)
            DataStrobe(_cpu->Latch(0), 12);
        if (falling & 0x80)
        {
            const uint8_t value = data ? MovxRead(0x0100) : MovxRead(0x0000);
            for (int bit = 0; bit < 8; ++bit)
                _cpu->SetPin(0, bit, (value >> bit) & 1);
        }
        if (rising & 0x80)
        {
            for (int bit = 0; bit < 8; ++bit)
                _cpu->SetPin(0, bit, true);
        }
        return;
    }
    if (port != 1 || !_context || !_context->pCore)
        return;
    Z80* z80 = _context->pCore->GetZ80();
    if (z80)
        z80->SetDeviceIntLine(Z80::kDeviceIntAtm2Kbc, (latch & kP1IntT) == 0);

    // P1.6 low drives the board's reset line, which also feeds the MCU's own
    // RST: the Z80 and the controller reset together (RAM kept). Firmware 2.2 /
    // 3.1 depend on it: at power-on they write the 'ATM' signature, pull /RES
    // and wait in `ajmp $` for that reset (atm_at31.asm "reset:")
    const bool resetLow = (latch & kP1Reset) == 0;
    if (resetLow == _resetLow)
        return;
    _resetLow = resetLow;
    if (!resetLow)
        return;
    _cpu->RequestReset();
    if (_context->pSoftResetSink)
        _context->pSoftResetSink->RequestSoftReset();
}

void Atm2Kbc::BoardReset()
{
    // The reset button: the same line resets the controller (its RAM survives)
    if (!Present())
        return;
    CatchUp(NowBase());
    _cpu->Reset();
    _cpu->SetPin(3, 4, _board.ve1);
    _resetLow = false;
    _p3 = 0xFF;
    _board.waitSet = false;
}
