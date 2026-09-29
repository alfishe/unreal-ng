#include "stdafx.h"

#include "evoavr.h"

#include <cstring>
#include <fstream>

#include "common/filehelper.h"

namespace
{
    constexpr uint8_t kExtensionFirst = 0xF0;
}  // namespace

EvoAvr::EvoAvr() : Ds12887(256)
{
    // An erased AVR EEPROM reads #FF: no user PS/2 keymap ('K','B' signature
    // absent), so the firmware uses its built-in one
    _eeprom.fill(0xFF);
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
            return static_cast<uint8_t>(0x80 | (_modifiers & 0x7F));
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
            return static_cast<uint8_t>(0x80 | (_modifiers & 0x7F));
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
            return 0x00;  // empty log (the PS/2 scancode buffer is ZX-Evo plan phase E2b)
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

void EvoAvr::SetModifiers(uint8_t mask)
{
    _modifiers = static_cast<uint8_t>(mask & 0x7F);
}

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

    // Only the battery-backed cells: the clock registers 0x00-0x0D are live
    std::memcpy(&_cells[kFirstRamCell], &image[kFirstRamCell], kExtensionFirst - kFirstRamCell);
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
