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
///
/// It is the keyboard's IExtendedKeySink too: the membrane's 16 extended keys as ZX keys (ZXKEY_EXT_LEFT ..., the host's matrix side and the
/// on-screen keyboard) are held here, not on the matrix. Each one presses its two matrix keys (membrane.vhd 157-238: LEFT = CAPS SHIFT + 5,
/// EXTEND = CAPS SHIFT + SYMBOL SHIFT ...) unless NR #68 bit 4 cancels the extended keys' matrix entries (zxnext.vhd 5451), and shows in
/// NR #B0 / #B1 either way. Not modelled: the membrane's shift advance by one scan (a naive reader of a composite key).

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include <functional>

#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"

class NextExtendedInput final : public IPs2KeySink, public IExtendedKeySink
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

    /// region <IExtendedKeySink>
    bool OwnsExtendedKey(ZXKeysEnum key) const override;
    void OnExtendedKey(ZXKeysEnum key, bool pressed) override;
    uint8_t ExtendedMatrixRow(uint8_t row) const override;
    void ReleaseExtendedKeys() override { _membrane.store(0, std::memory_order_relaxed); }
    /// endregion

    /// NR #68 bit 4 as the machine has it (the port decoder): set, the extended keys press no matrix keys
    void SetCancelMatrixEntries(std::function<bool()> cancel) { _cancel = std::move(cancel); }

    /// region <The registers>
    uint8_t ReadB0() const;  ///< ; " , . UP DOWN LEFT RIGHT (bit 7 .. 0)
    uint8_t ReadB1() const;  ///< DELETE EDIT BREAK INV TRU GRAPH CAPSLOCK EXTEND
    uint8_t ReadB2() const;  ///< right pad X Z Y MODE, left pad X Z Y MODE
    /// endregion

    /// The extended keys held (bits = Key): the PC keys' and the membrane's
    uint16_t Keys() const { return static_cast<uint16_t>(PcKeys() | MembraneKeys()); }
    uint16_t PcKeys() const { return _keys.load(std::memory_order_relaxed); }
    void SetPcKeys(uint16_t keys) { _keys.store(keys, std::memory_order_relaxed); }
    uint16_t MembraneKeys() const { return _membrane.load(std::memory_order_relaxed); }
    void SetMembraneKeys(uint16_t keys) { _membrane.store(keys, std::memory_order_relaxed); }
    /// The extra buttons of a side (kPad* bits)
    uint8_t Pad(bool left) const
    {
        return left ? _padLeft.load(std::memory_order_relaxed) : PadBitsToExtras(_right.load(std::memory_order_relaxed));
    }
    void SetPad(bool left, uint8_t buttons)
    {
        if (left)
            _padLeft.store(buttons & kPadMask, std::memory_order_relaxed);
        else
            _right.store(static_cast<uint16_t>((_right.load(std::memory_order_relaxed) & ~kPadExtraBits) | ExtrasToPadBits(buttons)),
                         std::memory_order_relaxed);
    }

    /// The 12-bit pad state of the RIGHT connector (the left one's low byte is the Kempston device): bit 0 R, 1 L, 2 D, 3 U, 4 B, 5 C, 6 A,
    /// 7 START, 8 Y, 9 Z, 10 X, 11 MODE. The right connector has no host keys: a tool sets it
    uint16_t Right() const { return _right.load(std::memory_order_relaxed); }
    void SetRight(uint16_t state) { _right.store(state & 0x0FFF, std::memory_order_relaxed); }
    /// bits 11..8 of a pad state <-> the kPad* extras
    static constexpr uint16_t kPadExtraBits = 0x0F00;
    static constexpr uint16_t ExtrasToPadBits(uint8_t extras)
    {
        return static_cast<uint16_t>(((extras & kPadMode) ? 0x0800 : 0) | ((extras & kPadX) ? 0x0400 : 0) | ((extras & kPadZ) ? 0x0200 : 0) |
                                     ((extras & kPadY) ? 0x0100 : 0));
    }
    static constexpr uint8_t PadBitsToExtras(uint16_t pad)
    {
        return static_cast<uint8_t>(((pad & 0x0800) ? kPadMode : 0) | ((pad & 0x0400) ? kPadX : 0) | ((pad & 0x0200) ? kPadZ : 0) |
                                    ((pad & 0x0100) ? kPadY : 0));
    }

    /// The PC key that presses an extra pad button of the host's pad (the left connector): keypad 7 / 9 / 1 / 3 by default, away from the
    /// Kempston directions (keypad 8 2 4 6) and fire (keypad 0)
    PcKey PadKey(uint8_t button) const;
    void SetPadKey(uint8_t button, PcKey key);

    /// The name of an extended key bit (report text); empty beyond the table
    static const char* KeyName(unsigned bit);

private:
    std::atomic<uint16_t> _keys{0};      ///< from the PC keys (IPs2KeySink)
    std::atomic<uint16_t> _membrane{0};  ///< from the ZX extended keys (IExtendedKeySink): these press matrix keys
    std::function<bool()> _cancel;
    std::atomic<uint8_t> _padLeft{0};
    std::atomic<uint16_t> _right{0};  ///< the whole right pad; its extras are bits 11..8
    std::array<std::atomic<uint8_t>, kPadButtons> _padKeys{};  ///< index = the button's bit number; a PcKey, 0 unbound
};
