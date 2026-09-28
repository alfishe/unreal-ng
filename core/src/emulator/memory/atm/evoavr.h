#pragma once
#include "stdafx.h"

#include <array>
#include <cstdint>
#include <string>

#include "cmos.h"

/// ZX-Evo BaseConf AVR as the Z80 sees it through the Gluk clock ports
/// (#DFF7 address / #BFF7 data, #DEF7 / #BEF7 in shadow).
///
/// On the board the ATmega128 emulates an MC146818 on top of a PCF8583 RTC and
/// answers every data-port access itself (pentevo avr/baseconf/trunk/src/rtc.c,
/// version.c). The clock registers 0x00-0x09 behave like the plain CMOS base
/// class; the rest follows the AVR firmware:
///
///   0x0A (A)     EEPROM page pointer, read/write
///   0x0B (B)     only the binary-mode bit (2) is kept, bit 1 reads 1
///   0x0C (C)     b7 EEPROM-window mode, b4 update-ended flag (cleared by the
///                read), b3 SD card present, b2 SD write-protected, b1 Caps LED,
///                b0 tape-out mode; writing b0 = 1 clears the PS/2 log
///   0x0D (D)     0x80 | live PS/2 modifiers (L/R Ctrl, L/R Alt, L/R Shift, F12)
///   0x0E-0xEF    battery-backed NVRAM (EVO Reset Service settings live at the
///                top: reset target, boot device, virtual drive, CRC)
///   0xF0-0xFF    EEPROM mode: 16-byte window into the 4 KiB AVR EEPROM at
///                (A << 4); otherwise the extension window. A write to ANY of
///                these cells selects the extension type (it is never stored as
///                data, so a read never echoes it - the ERS uses an echo to
///                detect "no Evo AVR"): 0 firmware version, 1 bootloader
///                version, 2 PS/2 scancode log, 3 modes register.
///
/// Version records are the 16-byte tags of the released images (see
/// kFirmwareVersion / kBootloaderVersion).
class EvoAvr : public CMOS
{
public:
    /// Extension types selected through cells 0xF0-0xFF (AVR main.h)
    static constexpr uint8_t kExtFirmwareVersion = 0;
    static constexpr uint8_t kExtBootloaderVersion = 1;
    static constexpr uint8_t kExtPs2Log = 2;
    static constexpr uint8_t kExtModes = 3;

    /// Version tag of pentevo cfgs/standalone_base_trdemu/trunk/zxevo_fw.bin
    /// (AVR flash 0x1DFF0): "ZXEvo 4M", 07.01.2026, release, CRC 0x4741
    static constexpr std::array<uint8_t, 16> kFirmwareVersion = {
        0x5A, 0x58, 0x45, 0x76, 0x6F, 0x20, 0x34, 0x4D, 0x00, 0x00, 0x00, 0x00, 0x27, 0xB4, 0x41, 0x47};

    /// Version tag of pentevo avrboot/trunk/avr/zxevo_bl.hex (AVR flash
    /// 0x1FFF0): "ZXEvoAVRBoot", 25.05.2019, beta (release bit clear)
    static constexpr std::array<uint8_t, 16> kBootloaderVersion = {
        0x5A, 0x58, 0x45, 0x76, 0x6F, 0x41, 0x56, 0x52, 0x42, 0x6F, 0x6F, 0x74, 0xB9, 0x26, 0xC4, 0x2B};

    /// modes_register raster field, bits 5:4 (AVR main.h): 00 Pentagon, 01 60 Hz, 10 48K, 11 128K
    static constexpr uint8_t kModesRaster48K = 0x20;

    static constexpr size_t kEepromSize = 4096;
    /// NVRAM file: the 256 clock/NVRAM cells followed by the 4 KiB EEPROM
    static constexpr size_t kNvramFileSize = 0x100 + kEepromSize;

public:
    EvoAvr();

    void WriteCMOS(uint8_t val) override;
    uint8_t ReadCMOS() override;

    /// region <Host side>
    void SetSdStatus(bool present, bool writeProtected);
    void SetModifiers(uint8_t mask);  ///< register D bits 6..0
    uint8_t GetExtensionType() const { return _extType; }
    uint8_t GetEepromPage() const { return _eepromPage; }
    bool IsEepromMode() const { return _eepromMode; }
    bool IsCapsLed() const { return _capsLed; }

    /// Battery-backed state (NVRAM cells + EEPROM). Load returns false and
    /// keeps the power-on contents when the file is missing or malformed
    bool LoadNvram(const std::string& path);
    bool SaveNvram(const std::string& path) const;
    /// endregion </Host side>

    /// region <TTD>
    /// Volatile AVR state (the NVRAM and EEPROM are battery-backed
    /// configuration, captured like the rest of the CMOS contents: not at all)
    void GetVolatileState(uint8_t& extType, uint8_t& eepromPage, uint8_t& flags) const;
    void SetVolatileState(uint8_t extType, uint8_t eepromPage, uint8_t flags);
    /// endregion </TTD>

protected:
    uint8_t ReadExtension(uint8_t index);

    std::array<uint8_t, kEepromSize> _eeprom{};
    uint8_t _extType = kExtFirmwareVersion;
    uint8_t _eepromPage = 0;
    bool _eepromMode = false;
    bool _capsLed = false;
    bool _tapeOutMode = false;
    bool _sdPresent = false;
    bool _sdWriteProtected = false;
    uint8_t _modifiers = 0;
};
