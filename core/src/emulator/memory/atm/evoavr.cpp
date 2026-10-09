#include "stdafx.h"

#include "evoavr.h"

#include <cstring>
#include <fstream>

#include "common/filehelper.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/emulatorcontext.h"

namespace
{
    constexpr uint8_t kExtensionFirst = 0xF0;

    /// atx.h PWROFF_KEY_TIME: F12 held longer than this is the ATX power-off. The AVR counts it on its own
    /// timer, so it is emulated time: a replay and a turbo run decide the same way the recording did
    /// (not emulated); a shorter press-and-release soft-resets the Z80
    constexpr uint64_t kF12PowerOffMicros = 5'000'000;
}  // namespace

EvoAvr::EvoAvr() : Ds12887(256)
{
    // An erased AVR EEPROM reads #FF: no user PS/2 keymap ('K','B' signature
    // absent), so the firmware uses its built-in one
    _eeprom.fill(0xFF);

    // Power-on (main.c: ps2keyboard_init, ps2keyboard_reset_log)
    _ps2.lastScancodeE0 = 1;  // "impossible scancode: E0 00"
    ResetPs2Log();
}

void EvoAvr::TTDRegions(std::vector<ttd::TTDDeviceRegion>& out)
{
    _eepromTracker.Bind(_eeprom.data(), kEepromSize);
    ttd::TTDDeviceRegion r;
    r.desc.id = ttd::TTDRegionId::EvoAvrEeprom;
    r.desc.name = "evo-avr.eeprom";
    r.desc.ownerType = static_cast<uint16_t>(ttd::PeripheralId::Ds12887);   // the AVR's clock blob is its state
    r.desc.memory = _eeprom.data();
    r.desc.bytes = static_cast<uint32_t>(kEepromSize);
    r.desc.pieces = _eepromTracker.Pieces();
    r.tracker = &_eepromTracker;
    r.compareEachCapture = true;
    out.push_back(r);
}

void EvoAvr::WriteRegister(uint8_t index, uint8_t val)
{
    if (index >= kExtensionFirst)
    {
        if (_eepromMode)
            _eeprom[(static_cast<size_t>(_eepromPage) << 4) + (index & 0x0F)] = val;
        else
            _extType = val;  // any cell selects the extension type (rtc.c:516-528)
        return;
    }

    switch (index)
    {
        case kRegA:
            _eepromPage = val;
            return;
        case kRegC:
            // rtc.c:491-510: bit 0 = 1 clears the PS/2 log, bit 1 sets the Caps
            // LED, bit 7 selects the EEPROM window
            _capsLed = (val & 0x02) != 0;
            _eepromMode = (val & 0x80) != 0;
            if (val & 0x01)
                ResetPs2Log();
            return;
        case kRegB:
            // rtc.c keeps only the binary-mode bit: always 24-hour, no SET hold
            SetCell(kRegB, static_cast<uint8_t>((val & kBBinary) | kB24Hour));
            return;
        case kRegD:
            return;  // read-only
        default:
            Ds12887::WriteRegister(index, val);  // clock registers, B, NVRAM 0x0E-0xEF
            return;
    }
}

uint8_t EvoAvr::ReadRegister(uint8_t index)
{
    if (index >= kExtensionFirst)
    {
        if (_eepromMode)
            return _eeprom[(static_cast<size_t>(_eepromPage) << 4) + (index & 0x0F)];
        if (_extType == kExtPs2Log)
            return PopPs2Log();  // a read of any cell pops one byte (version.c:34)
        return ReadExtension(index);
    }

    switch (index)
    {
        case kRegA:
            return _eepromPage;
        case kRegC:
        {
            // The chip keeps the update-ended flag and clears it on read
            const uint8_t updateEnded = Ds12887::ReadRegister(kRegC) & kCUpdateEnded;
            return static_cast<uint8_t>((_eepromMode ? 0x80 : 0) | updateEnded | (_sdPresent ? 0x08 : 0) |
                                        (_sdWriteProtected ? 0x04 : 0) | (_capsLed ? 0x02 : 0) | (_tapeOutMode ? 0x01 : 0));
        }
        case kRegB:
            // rtc.c keeps only the binary-mode bit; bit 1 (24-hour) always reads 1
            return GetCell(kRegB);
        case kRegD:
            return static_cast<uint8_t>(0x80 | (_ps2.modifiers & 0x7F));
        default:
            return Ds12887::ReadRegister(index);  // clock registers, NVRAM 0x0E-0xEF
    }
}

/// What a guest read would return, without clearing the update-ended flag
uint8_t EvoAvr::PeekRegister(uint8_t index) const
{
    if (index >= kExtensionFirst)
    {
        if (_eepromMode)
            return _eeprom[(static_cast<size_t>(_eepromPage) << 4) + (index & 0x0F)];
        return ReadExtension(index);
    }

    switch (index)
    {
        case kRegA:
            return _eepromPage;
        case kRegB:
            return GetCell(kRegB);
        case kRegC:
            return static_cast<uint8_t>((_eepromMode ? 0x80 : 0) | (Ds12887::PeekRegister(kRegC) & kCUpdateEnded) |
                                        (_sdPresent ? 0x08 : 0) | (_sdWriteProtected ? 0x04 : 0) |
                                        (_capsLed ? 0x02 : 0) | (_tapeOutMode ? 0x01 : 0));
        case kRegD:
            return static_cast<uint8_t>(0x80 | (_ps2.modifiers & 0x7F));
        default:
            return Ds12887::PeekRegister(index);
    }
}

/// Extension window read (version.c:13-45)
uint8_t EvoAvr::ReadExtension(uint8_t index) const
{
    const size_t offset = index - kExtensionFirst;

    switch (_extType)
    {
        case kExtFirmwareVersion:
            return kFirmwareVersion[offset];
        case kExtBootloaderVersion:
            return kBootloaderVersion[offset];
        case kExtPs2Log:
            return PeekPs2Log();  // a debugger peek: the log is not changed
        case kExtModes:
            // modes_register at cell 0xF0: bit 0 VGA, bit 1 tape-out, bit 2 Caps
            // LED, bits 5:4 raster - the emulator runs the 48K raster
            return offset == 0
                       ? static_cast<uint8_t>(kModesRaster48K | (_capsLed ? 0x04 : 0) | (_tapeOutMode ? 0x02 : 0))
                       : 0xFF;
        default:
            return 0xFF;
    }
}

void EvoAvr::SetSdStatus(bool present, bool writeProtected)
{
    _sdPresent = present;
    _sdWriteProtected = writeProtected;
}

/// region <PS/2 keyboard>

void EvoAvr::OnPcKey(PcKey key, bool pressed)
{
    // zx.c to_zu: a key pressed while Ctrl and Alt are both held never reaches
    // the Z80 - it is the board's hard reset (FLAG_HARD_RESET). F12 included:
    // the documented Right Alt + Ctrl + F12 "exit to Gluk boot". The modifiers
    // are checked before this key's own bytes parse, so the Ctrl or Alt press
    // itself never fires it
    if (pressed && key != PcKey::None
        && (_ps2.modifiers & (kModLeftCtrl | kModRightCtrl)) != 0
        && (_ps2.modifiers & (kModLeftAlt | kModRightAlt)) != 0
        && _resetHandler)
    {
        _resetHandler(/*hardReset=*/true);
    }

    // interrupts.c + atx.c atx_power_task: F12 only feeds the atx_counter while
    // held, and on the release after a short hold the AVR resets the Z80 over
    // SPI (zx_spi_send(SPI_RST_REG, 0, 0x7F)) - it never becomes a key
    if (key == PcKey::Function12)
    {
        if (pressed)
        {
            if (!_ps2.f12Down)
            {
                _ps2.f12Down = 1;
                _ps2.f12PressMicros = _emulatedClock ? _emulatedClock() : 0;
            }
        }
        else if (_ps2.f12Down)
        {
            _ps2.f12Down = 0;
            const uint64_t now = _emulatedClock ? _emulatedClock() : 0;
            if (now - _ps2.f12PressMicros < kF12PowerOffMicros && _resetHandler)
                _resetHandler(/*hardReset=*/false);
        }
    }

    const size_t index = static_cast<size_t>(key);
    if (key == PcKey::None || index >= static_cast<size_t>(PcKey::Count))
        return;

    uint8_t& held = _ps2.held[index >> 3];
    const uint8_t bit = static_cast<uint8_t>(1u << (index & 7));
    held = pressed ? static_cast<uint8_t>(held | bit) : static_cast<uint8_t>(held & ~bit);

    for (uint8_t byte : pckey::Ps2Set2Bytes(key, pressed))
        ReceivePs2Byte(byte);
}

void EvoAvr::ReleaseAllPcKeys()
{
    for (size_t index = 1; index < static_cast<size_t>(PcKey::Count); index++)
    {
        if (_ps2.held[index >> 3] & (1u << (index & 7)))
            OnPcKey(static_cast<PcKey>(index), /*pressed=*/false);
    }
}

/// ps2.c ps2keyboard_parse (logging and flags) and the modifier part of zx.c
/// to_zx. The rest of to_zx (ZX matrix from the AVR keymap, Print Screen NMI,
/// Ctrl-Alt-Del reset, Scroll Lock video mode) is not the Z80-visible log: the
/// emulator's matrix comes from the host keys directly
void EvoAvr::ReceivePs2Byte(uint8_t byte)
{
    // Keyboard protocol answers are never keys
    if (byte == 0xFA || byte == 0xFE || byte == 0xEE || byte == 0xAA)
        return;

    // The keypad keys that set the mouse resolution (zx.c to_zx: on the press, no E0)
    // (a typematic repeat of the key already down is not a new press)
    const bool repeat = byte == _ps2.lastScancode && _ps2.lastScancodeE0 == 0;
    if (!_ps2.wasE0 && !_ps2.wasRelease && _ps2.skipBytes == 0 && !repeat && (byte == 0x79 || byte == 0x7B || byte == 0x7C))
        _mouse.OnKeypadKey(byte);

    // Log only whole key data: Pause is not logged, and after a reset the first
    // byte logged must start a key
    if (byte != 0xE1 && _ps2.skipBytes == 0)
    {
        if (_ps2.logStart == 0xFF)
        {
            _ps2.logEnd = 0xFE;
            _ps2.logStart = 0;
        }
        if (_ps2.logEnd != 0xFE || (_ps2.wasRelease == 0 && _ps2.wasE0 == 0))
            AppendPs2Log(byte);
    }

    if (_ps2.skipBytes)
    {
        _ps2.skipBytes--;
        return;
    }
    if (byte == 0xE0)
    {
        _ps2.wasE0 = 1;
        return;
    }
    if (byte == 0xF0)
    {
        _ps2.wasRelease = 1;
        return;
    }
    if (byte == 0xE1)  // Pause: skip the next 7 bytes
    {
        _ps2.skipBytes = 7;
        return;
    }

    // Typematic repeat of the key already down: nothing more to do
    if (byte == _ps2.lastScancode && _ps2.wasE0 == _ps2.lastScancodeE0)
    {
        if (_ps2.wasRelease)
        {
            _ps2.lastScancode = 0x00;
            _ps2.lastScancodeE0 = 1;
        }
        else
        {
            _ps2.wasE0 = 0;
            return;
        }
    }
    if (!_ps2.wasRelease)
    {
        _ps2.lastScancode = byte;
        _ps2.lastScancodeE0 = _ps2.wasE0;
    }

    if (byte == 0x12 && _ps2.wasE0)  // the fake Left Shift of Print Screen and friends
    {
        _ps2.wasE0 = 0;
        _ps2.wasRelease = 0;
        return;
    }

    uint8_t mask = 0;
    if (_ps2.wasE0)
    {
        if (byte == 0x11)
            mask = kModRightAlt;
        else if (byte == 0x14)
            mask = kModRightCtrl;
    }
    else
    {
        switch (byte)
        {
            case 0x12: mask = kModLeftShift; break;
            case 0x59: mask = kModRightShift; break;
            case 0x14: mask = kModLeftCtrl; break;
            case 0x11: mask = kModLeftAlt; break;
            case 0x07: mask = kModF12; break;
            default: break;
        }
    }
    if (mask)
        _ps2.modifiers = _ps2.wasRelease ? static_cast<uint8_t>(_ps2.modifiers & ~mask)
                                         : static_cast<uint8_t>(_ps2.modifiers | mask);

    _ps2.wasE0 = 0;
    _ps2.wasRelease = 0;
}

/// ps2.c ps2keyboard_to_log
void EvoAvr::AppendPs2Log(uint8_t byte)
{
    if (_ps2.logEnd == 0xFF)
        return;  // overflowed: nothing more until the Z80 reads the #FF
    if (_ps2.logEnd == 0xFE)
        _ps2.logEnd = _ps2.logStart;  // the first byte after a reset

    _ps2.log[_ps2.logEnd] = byte;
    _ps2.logEnd = static_cast<uint8_t>((_ps2.logEnd + 1) % kPs2LogSize);
    if (_ps2.logEnd == _ps2.logStart)
        _ps2.logEnd = 0xFF;  // the ring caught its own tail
}

/// ps2.c ps2keyboard_from_log, without its side effects: 0 when empty or in
/// the reset state, #FF after an overflow, else the oldest byte
uint8_t EvoAvr::PeekPs2Log() const
{
    if (_ps2.logStart == 0xFF)
        return 0x00;  // reset state
    if (_ps2.logEnd < kPs2LogSize)
        return _ps2.logEnd == _ps2.logStart ? 0x00 : _ps2.log[_ps2.logStart];
    return _ps2.logEnd == 0xFE ? 0x00 : 0xFF;
}

/// ps2.c ps2keyboard_from_log: pops the byte; reading the overflow #FF resets the log
uint8_t EvoAvr::PopPs2Log()
{
    const uint8_t byte = PeekPs2Log();
    if (_ps2.logStart == 0xFF)
        return byte;

    if (_ps2.logEnd < kPs2LogSize)
    {
        if (_ps2.logEnd != _ps2.logStart)
            _ps2.logStart = static_cast<uint8_t>((_ps2.logStart + 1) % kPs2LogSize);
    }
    else if (_ps2.logEnd == 0xFF)
    {
        ResetPs2Log();  // the Z80 read the overflow mark: the log starts over
    }
    return byte;
}

size_t EvoAvr::GetPs2LogCount() const
{
    if (_ps2.logStart == 0xFF || _ps2.logEnd >= kPs2LogSize)
        return 0;
    return (_ps2.logEnd + kPs2LogSize - _ps2.logStart) % kPs2LogSize;
}

/// endregion </PS/2 keyboard>

bool EvoAvr::LoadNvram(const std::string& path)
{
    if (path.empty())
        return false;

    std::ifstream file(FileHelper::ToFsPath(path), std::ios::binary);
    if (!file)
        return false;

    std::array<uint8_t, kNvramFileSize> image{};
    file.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (file.gcount() != static_cast<std::streamsize>(image.size()))
        return false;

    // Only the battery-backed cells: the clock registers 0x00-0x0D are live. Above
    // the Z80's cells, the AVR keeps the mouse resolution in its RTC chip (#FD)
    std::memcpy(&_cells[kFirstRamCell], &image[kFirstRamCell], kExtensionFirst - kFirstRamCell);
    _cells[EvoAvrMouse::kResolutionCell] = image[EvoAvrMouse::kResolutionCell];
    std::memcpy(_eeprom.data(), &image[0x100], kEepromSize);
    return true;
}

bool EvoAvr::SaveNvram(const std::string& path) const
{
    if (path.empty())
        return false;

    std::array<uint8_t, kNvramFileSize> image{};
    std::memcpy(image.data(), _cells.data(), 0x100);
    std::memcpy(&image[0x100], _eeprom.data(), kEepromSize);

    std::ofstream file(FileHelper::ToFsPath(path), std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    return static_cast<bool>(file);
}

void EvoAvr::GetVolatileState(uint8_t& extType, uint8_t& eepromPage, uint8_t& flags) const
{
    extType = _extType;
    eepromPage = _eepromPage;
    flags = static_cast<uint8_t>((_eepromMode ? 0x01 : 0) | (_capsLed ? 0x02 : 0) | (_tapeOutMode ? 0x04 : 0));
}

void EvoAvr::SetVolatileState(uint8_t extType, uint8_t eepromPage, uint8_t flags)
{
    _extType = extType;
    _eepromPage = eepromPage;
    _eepromMode = (flags & 0x01) != 0;
    _capsLed = (flags & 0x02) != 0;
    _tapeOutMode = (flags & 0x04) != 0;
}

/// region <WAIT ports>

namespace
{
/// A Gluk access's work in AVR cycles, counted on the released firmware images (llvm-objdump / rizin disassembly):
/// BaseConf pentevo cfgs/standalone_base_trdemu zxevo_fw.bin ("ZXEvo 4M", 07.01.2026, the kFirmwareVersion image);
/// TS-Labs tslabs/zx-evo 07bfd3e9 pentevo/avr/current/default/zxevo_fw.bin (2026-04-01; zx.c as at 167199ba).
/// SPI byte 20 cycles (out, 16 shift cycles at F_CPU/2, the SPIF poll, in), I2C bit 112 cycles (TWBR 48: 98.7 kHz).
/// Research and the counts per path: reference-evo-com-port.md §3.1
struct GlukService
{
    uint16_t readFull, readShort;     ///< from the main loop's flag test to the release, without gluk_get_reg
    uint16_t writeFull, writeShort;   ///< the same for a write (no cell work before the release)
    uint16_t afterTail;               ///< after the release, before gluk_set_reg
    uint8_t nvramFirst;               ///< the first NVRAM cell (BaseConf 14 registers, TS 15)
    // gluk_get_reg by cell, call to return
    uint16_t clockBcd, clockBinary, regAB, regC, regD, regE, version, ps2Log, eeprom, nvram;
};

constexpr GlukService kBaseConfGluk = {295, 295, 281, 281, 44, 0x0E, 98, 20, 22, 38, 26, 0, 41, 62, 42, 4427};
constexpr GlukService kTsOldGluk = {169, 169, 159, 159, 20, 0x0F, 122, 36, 34, 50, 76, 54, 39, 56, 44, 4419};
constexpr GlukService kTsShortGluk = {151, 90, 147, 86, 14, 0x0F, 122, 36, 34, 50, 76, 54, 39, 56, 44, 4419};

// gluk_set_reg after the release (both firmwares share rtc.c's structure; counted on the BaseConf image)
constexpr uint32_t kSetSmall = 25;      ///< a register, an alarm, the extension type, an out-of-range time
constexpr uint32_t kSetClock = 3393;    ///< a time register: BCD conversion + one rtc_write (START, 3 bytes) on I2C
constexpr uint32_t kSetYear = 6800;     ///< the year: two rtc_writes (the second START waits for the STOP)
constexpr uint32_t kSetNvram = 3283;    ///< an NVRAM cell: one rtc_write
constexpr uint32_t kSetEeprom = 55;     ///< write_eeprom past its busy wait (the 8.5 ms write runs on its own)

constexpr uint8_t kGlukYear = 0x09;

uint8_t BcdToBinary(uint8_t value)
{
    return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0F));
}
}  // namespace

void EvoAvr::SetWaitFirmware(WaitHandler handler, const EvoAvrWait::Timing& timing)
{
    _waitHandler = handler;
    _wait.SetTiming(timing);
}

uint32_t EvoAvr::GlukAccessCycles(bool read, uint8_t value, uint64_t now, uint32_t baseClockHz)
{
    const GlukService& s = _waitHandler == WaitHandler::BaseConf ? kBaseConfGluk
                           : _waitHandler == WaitHandler::TsOld  ? kTsOldGluk
                                                                 : kTsShortGluk;
    const uint8_t index = GetAddress();
    const bool shortAddress = index >= kExtensionFirst;   // TS-Conf FPGA: {~&addr[7:4], addr[3:0]} in the status
    const bool binary = (GetCell(kRegB) & kBBinary) != 0;
    EvoAvrWait::Eeprom eeprom = EvoAvrWait::Eeprom::None;

    if (read)
    {
        uint32_t cell = 0;
        if (index >= kExtensionFirst)
        {
            if (_eepromMode)
            {
                cell = s.eeprom;
                eeprom = EvoAvrWait::Eeprom::Read;
            }
            else
                cell = _extType == kExtPs2Log ? s.ps2Log : s.version;
        }
        else if (index >= s.nvramFirst)
            cell = s.nvram;   // rtc_read: START, SLA+W, cell, repeated START, SLA+R, the byte on I2C
        else if (index <= kGlukYear)
            cell = binary ? s.clockBinary : s.clockBcd;
        else if (index == kRegC)
            cell = s.regC;
        else if (index == kRegD)
            cell = s.regD;
        else if (index == kRegA || index == kRegB)
            cell = s.regAB;
        else
            cell = s.regE;   // TS: register E (the Win / Menu keys)
        const uint32_t service = (shortAddress ? s.readShort : s.readFull) + cell;
        return _wait.Access(service, s.afterTail, now, baseClockHz, eeprom);
    }

    // A write: the data reaches the AVR at the release; gluk_set_reg runs after it, delaying the main loop
    uint32_t after = kSetSmall;
    if (index >= kExtensionFirst)
    {
        if (_eepromMode)
        {
            after = kSetEeprom;
            eeprom = EvoAvrWait::Eeprom::Write;
        }
    }
    else if (index >= s.nvramFirst)
        after = kSetNvram;
    else if (index <= kGlukYear)
    {
        // rtc.c gluk_set_reg: a time register goes to the PCF8583 when in range; the alarms stay in the AVR
        const uint8_t v = binary ? value : BcdToBinary(value);
        switch (index)
        {
            case 0x00:   // seconds
            case 0x02:   // minutes
                after = v <= 59 ? kSetClock : kSetSmall;
                break;
            case 0x04:   // hours
                after = v <= 23 ? kSetClock : kSetSmall;
                break;
            case 0x06:   // day of week [the month it is written with assumed valid]
                after = v >= 1 && v <= 7 ? kSetClock : kSetSmall;
                break;
            case 0x08:   // month [the day of week assumed valid]
                after = v >= 1 && v <= 12 ? kSetClock : kSetSmall;
                break;
            case 0x07:   // day of month
                after = kSetClock;
                break;
            case kGlukYear:
                after = kSetYear;
                break;
            default:   // the alarms
                break;
        }
    }
    const uint32_t service = shortAddress ? s.writeShort : s.writeFull;
    return _wait.Access(service, s.afterTail + after, now, baseClockHz, eeprom);
}

void EvoAvr::HoldForGlukAccess(EmulatorContext* context, bool read, uint8_t value)
{
    if (!context)
        return;
    const uint32_t baseHz = context->emulatorState.base_z80_frequency ? context->emulatorState.base_z80_frequency
                                                                       : 3500000;
    _wait.HoldCpu(context, GlukAccessCycles(read, value, EvoAvrWait::BaseNow(context), baseHz));
}

/// endregion </WAIT ports>
