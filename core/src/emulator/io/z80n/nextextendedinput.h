#pragma once

/// @file nextextendedinput.h
/// @brief The Next keyboard's extended keys and the MD pad's extra buttons (NR #B0, #B1, #B2; zxnext.vhd 6155-6167).
///
/// The Next's PS/2 keyboard controller (membrane.vhd, ps2_keyb.vhd, keymaps.vhd) fills the matrix cells of columns 5 and 6 with keys the 8x5 ZX
/// matrix has no place for: EXTEND, UP, CAPS LOCK, GRAPH, TRUE VIDEO, INV VIDEO, BREAK, EDIT, ; " , . DELETE and the arrows. The register reads
/// show them (1 = pressed) independent of the matrix. A pad on a joystick connector adds MODE, X, Y and Z (the 12-bit i_JOY_LEFT / i_JOY_RIGHT);
/// A, C and START are bits 6, 5 and 7 of the Kempston byte already.
///
/// It is the machine's IPs2KeySink: the physical PC keys the host sends (MessageCenter PCKEY_PRESSED / PCKEY_RELEASED) land here. The host's
/// pad is the LEFT connector; the right one is set by a tool.

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "emulator/io/keyboard/pckey.h"

class NextExtendedInput final : public IPs2KeySink
{
public:
    /// Bit numbers of i_KBD_EXTENDED_KEYS (the comment at zxnext.vhd 6155: bits 15..8 = DOWN LEFT RIGHT DELETE . , " ;  bits 7..0 = EDIT BREAK INV TRU GRAPH CAPSLOCK UP EXTEND)
    enum Key : uint8_t
    {
        kExtend = 0, kUp, kCapsLock, kGraph, kTrueVideo, kInvVideo, kBreak, kEdit,
        kSemicolon, kQuote, kComma, kPeriod, kDelete, kRight, kLeft, kDown,
        kKeyCount
    };
    /// A pad's extra buttons, as bits of NR #B2 for one side (the left connector's are bits 3:0, the right one's 7:4)
    static constexpr uint8_t kPadMode = 0x01, kPadY = 0x02, kPadZ = 0x04, kPadX = 0x08;
    static constexpr uint8_t kPadMask = 0x0F;
    static constexpr unsigned kPadButtons = 4;

    NextExtendedInput();

    /// region <IPs2KeySink>
    void OnPcKey(PcKey key, bool pressed) override;
    void ReleaseAllPcKeys() override;
    std::string ControllerName() const override { return "ZX Next keyboard"; }
    /// endregion

    /// region <The registers>
    uint8_t ReadB0() const;  ///< ; " , . UP DOWN LEFT RIGHT (bit 7 .. 0)
    uint8_t ReadB1() const;  ///< DELETE EDIT BREAK INV TRU GRAPH CAPSLOCK EXTEND
    uint8_t ReadB2() const;  ///< right pad X Z Y MODE, left pad X Z Y MODE
    /// endregion

    uint16_t Keys() const { return _keys.load(std::memory_order_relaxed); }
    void SetKeys(uint16_t keys) { _keys.store(keys, std::memory_order_relaxed); }
    /// The extra buttons of a side (kPad* bits)
    uint8_t Pad(bool left) const { return (left ? _padLeft : _padRight).load(std::memory_order_relaxed); }
    void SetPad(bool left, uint8_t buttons) { (left ? _padLeft : _padRight).store(buttons & kPadMask, std::memory_order_relaxed); }

    /// The PC key that presses an extra pad button of the host's pad (the left connector): keypad 7 / 9 / 1 / 3 by default, away from the
    /// Kempston directions (keypad 8 2 4 6) and fire (keypad 0)
    PcKey PadKey(uint8_t button) const;
    void SetPadKey(uint8_t button, PcKey key);

    /// The name of an extended key bit (report text); empty beyond the table
    static const char* KeyName(unsigned bit);

private:
    std::atomic<uint16_t> _keys{0};
    std::atomic<uint8_t> _padLeft{0};
    std::atomic<uint8_t> _padRight{0};
    std::array<std::atomic<uint8_t>, kPadButtons> _padKeys{};  ///< index = the button's bit number; a PcKey, 0 unbound
};
