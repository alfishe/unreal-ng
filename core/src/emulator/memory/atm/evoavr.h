#pragma once

// Hardware source: the ZX-Evo FPGA / AVR / ERS sources of https://github.com/alfishe/pentevo at commit c24723db
// (project home https://github.com/tslabs/zx-evo, folder pentevo), taken from the public repository, not a local edit;
// the timing rules were run in Verilator. Pinned revisions, links and what was simulated versus read:
// docs/inprogress/2026-09-15-atm-baseconf-highres-ports/sources-and-provenance.md
#include "stdafx.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "debugger/ttd/engine/ttdregiontracker.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/memory/atm/evoavrmouse.h"

/// ZX-Evo BaseConf AVR as the Z80 sees it through the Gluk clock ports
/// (#DFF7 address / #BFF7 data, #DEF7 / #BEF7 in shadow).
///
/// On the board the ATmega128 emulates an MC146818 on top of a PCF8583 RTC and
/// answers every data-port access itself (pentevo avr/baseconf/trunk/src/rtc.c,
/// version.c). The clock registers 0x00-0x09 are the shared Ds12887 chip; the
/// rest follows the AVR firmware:
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
///
/// PS/2 keyboard (the AVR's side of it, ps2.c ps2keyboard_parse and the
/// modifier part of zx.c to_zx): every scan code byte the keyboard sends goes
/// through the same parser as on the board - protocol bytes dropped, Pause
/// (E1 + 7 bytes) never logged, after a log reset the first byte logged must
/// start a key - and lands in a 16-byte ring the Z80 pops through extension
/// type 2 (0 = empty, #FF = overflow, which then resets the log). Register D
/// carries the modifier keys the parser saw. The bytes come from IPs2KeySink:
/// a physical key event, encoded as PS/2 set 2 (pckey.h).
class EvoAvr : public Ds12887, public IPs2KeySink, public ttd::ITTDRegionSource
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

    uint8_t ReadRegister(uint8_t index) override;
    void WriteRegister(uint8_t index, uint8_t value) override;
    uint8_t PeekRegister(uint8_t index) const override;
    const char* ChipName() const override { return "ZX-Evo AVR (MC146818 emulation)"; }
    const char* RegistersNote() const override
    {
        return "AVR firmware registers: A = EEPROM page, B keeps only the binary bit (always 24 h, SET ignored), "
               "C = EEPROM mode / update ended / SD present / SD write protect / Caps LED / tape out, "
               "D = #80 | PS/2 modifiers, #F0-#FF = EEPROM window or extension window";
    }

    /// Register D bits (zx.h KB_*_MASK)
    static constexpr uint8_t kModLeftCtrl = 0x01;
    static constexpr uint8_t kModRightCtrl = 0x02;
    static constexpr uint8_t kModLeftAlt = 0x04;
    static constexpr uint8_t kModRightAlt = 0x08;
    static constexpr uint8_t kModLeftShift = 0x10;
    static constexpr uint8_t kModRightShift = 0x20;
    static constexpr uint8_t kModF12 = 0x40;

    static constexpr size_t kPs2LogSize = 16;

    /// PS/2 state for TTD (PeripheralId::EvoPs2). Fixed layout, no padding
    struct Ps2State
    {
        uint8_t log[kPs2LogSize];  ///< ps2keyboard_log
        uint8_t logStart;          ///< ps2keyboard_log_start (#FF = reset state)
        uint8_t logEnd;            ///< ps2keyboard_log_end (#FE = empty after reset, #FF = overflow)
        uint8_t wasRelease;        ///< parser: F0 seen
        uint8_t wasE0;             ///< parser: E0 seen
        uint8_t lastScancode;      ///< parser: typematic filter
        uint8_t lastScancodeE0;
        uint8_t skipBytes;         ///< parser: bytes of a Pause sequence still to skip
        uint8_t modifiers;         ///< kb_ctrl_status (register D bits 6..0)
        uint8_t held[16];          ///< host side: PcKey bitmap of the keys held down
    };
    static_assert(sizeof(Ps2State) == 40, "EvoAvr::Ps2State layout changed");

public:
    EvoAvr();

    /// region <Host side>
    void SetSdStatus(bool present, bool writeProtected);
    uint8_t GetExtensionType() const { return _extType; }
    uint8_t GetEepromPage() const { return _eepromPage; }
    bool IsEepromMode() const { return _eepromMode; }
    bool IsCapsLed() const { return _capsLed; }

    /// Battery-backed state (NVRAM cells + EEPROM). Load returns false and
    /// keeps the power-on contents when the file is missing or malformed
    bool LoadNvram(const std::string& path);
    bool SaveNvram(const std::string& path) const;
    /// endregion </Host side>

    /// region <PS/2 keyboard>
    void OnPcKey(PcKey key, bool pressed) override;
    void ReleaseAllPcKeys() override;
    /// One byte from the keyboard (ps2keyboard_parse): the log and register D
    void ReceivePs2Byte(uint8_t byte);
    uint8_t GetPs2Modifiers() const { return _ps2.modifiers; }
    /// Bytes waiting in the log (0 in the reset state, 15 at most)
    size_t GetPs2LogCount() const;
    bool IsPs2LogOverflow() const { return _ps2.logEnd == 0xFF && _ps2.logStart != 0xFF; }

    /// The resets the AVR firmware owns: F12 released after a hold shorter
    /// than PWROFF_KEY_TIME (~5 s) is the reset button (atx.c soft reset) -
    /// the "F12 - exit" of the TS-BIOS setup screen. A key pressed while Ctrl
    /// and Alt are both held is the power cycle (zx.c FLAG_HARD_RESET) - the
    /// documented Right Alt + Ctrl + F12. A F12 hold of 5 s and more is the
    /// ATX power-off, not emulated. The hold is measured in host time: the AVR
    /// counts its own seconds, not the Z80's
    using ResetHandler = std::function<void(bool hardReset)>;
    void SetResetHandler(ResetHandler handler) { _resetHandler = std::move(handler); }
    /// endregion </PS/2 keyboard>

    /// The PS/2 mouse on the AVR and the Kempston-address registers it keeps
    /// (evoavrmouse.h); a sink of the emulator's MouseManager
    EvoAvrMouse& Ps2Mouse() { return _mouse; }
    const EvoAvrMouse& Ps2Mouse() const { return _mouse; }

    /// region <TTD>
    /// Volatile AVR state (AtmPagingState). The clock and its cells are the
    /// Ds12887 blob; the 4 KiB EEPROM is not captured (the guest writes it
    /// only when it saves a PS/2 keymap)
    void GetVolatileState(uint8_t& extType, uint8_t& eepromPage, uint8_t& flags) const;
    void SetVolatileState(uint8_t extType, uint8_t eepromPage, uint8_t flags);
    const Ps2State& GetPs2State() const { return _ps2; }
    void SetPs2State(const Ps2State& state) { _ps2 = state; }
    /// The 4 KiB EEPROM, battery-backed: the time-travel engine's region
    /// EvoAvrEeprom (compared at each capture: one write path, 4 KiB). v1 does
    /// not record it
    const uint8_t* EepromData() const { return _eeprom.data(); }
    void TTDRegions(std::vector<ttd::TTDDeviceRegion>& out) override;
    void TTDArmRegions(bool) override {}
    /// endregion </TTD>

protected:
    uint8_t ReadExtension(uint8_t index) const;
    /// ps2keyboard_from_log: pop one byte (0 empty, #FF overflow -> log reset)
    uint8_t PopPs2Log();
    uint8_t PeekPs2Log() const;
    void ResetPs2Log() { _ps2.logStart = 0xFF; }
    void AppendPs2Log(uint8_t byte);

    std::array<uint8_t, kEepromSize> _eeprom{};
    ttd::TTDRegionTracker _eepromTracker;
    uint8_t _extType = kExtFirmwareVersion;
    uint8_t _eepromPage = 0;
    bool _eepromMode = false;
    bool _capsLed = false;
    bool _tapeOutMode = false;
    bool _sdPresent = false;
    bool _sdWriteProtected = false;
    Ps2State _ps2{};
    ResetHandler _resetHandler;
    std::chrono::steady_clock::time_point _f12Press{};
    bool _f12Down = false;
    // Its resolution lives in the AVR's battery-backed RTC cell #FD, which the Z80 cannot reach
    EvoAvrMouse _mouse{[this] { return GetCell(EvoAvrMouse::kResolutionCell); },
                       [this](uint8_t value) { SetCell(EvoAvrMouse::kResolutionCell, value); }};
};
