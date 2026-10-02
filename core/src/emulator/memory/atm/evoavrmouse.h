#pragma once

// Hardware source: pentevo avr/current/ps2.c (ps2mouse_task, ps2mouse_set_resolution,
// ps2mouse_init_sequence) and zx.c (zx_mouse_reset, zx_mouse_task) of
// https://github.com/tslabs/zx-evo (folder pentevo)

#include <atomic>
#include <cstdint>
#include <functional>

#include "emulator/io/mouse/imousesink.h"

/// The ZX-Evo's mouse: a PS/2 wheel mouse on the AVR, which keeps the three
/// registers the FPGA shows at the Kempston addresses (#FADF buttons + wheel,
/// #FBDF X, #FFDF Y) and sends them over SPI after every packet. Used by the
/// ZX-Evo BaseConf (ATM3) and TS-Conf boards.
///
/// What the AVR firmware does, and so this device:
///   - initializes the mouse as a wheel mouse (sample rate 200 / 100 / 80, then
///     "get device type": ID 3) and sends 4-byte packets at 100 Hz;
///   - byte 1 (Y overflow, X overflow, Y sign, X sign, 1, M, R, L) becomes the
///     low nibble of the button register: (b ^ 7) & #0F, so the buttons read
///     active low with bit 3 set;
///   - bytes 2 and 3 are added to the 8-bit X and Y registers (X grows to the
///     right, Y grows upward: PS/2 axes, the Kempston ones);
///   - byte 4 (the wheel, Z: negative = rolled away from the user) is added to
///     the high nibble of the button register;
///   - the mouse found: X = 0, Y = 1, buttons #FF (zx_mouse_reset(1): what ZX
///     detection routines take for "mouse present"); no mouse: X = Y = #FF;
///   - the resolution (1, 2, 4 or 8 counts per mm; AVR RTC cell #FD, 0..3) is
///     changed with keypad '+', '-' and '*' while both mouse buttons are held.
///
/// The host mouse arrives in emulated pixels (MouseManager). One pixel of host
/// travel is one count at the resolution's lowest step (1 count/mm), so the
/// default resolution moves exactly like the Kempston interface, and each step
/// up doubles the counts, as a finer mouse does on the board.
///
/// Worked example: resolution 1 (2 counts/mm), the host moves 3 right, 1 down
/// and holds the left button: X += 6, Y -= 2, buttons #FE -> #(wheel)E: low
/// nibble 1110 (bit 3 set, L pressed).
class EvoAvrMouse : public IMouseSink
{
public:
    static constexpr uint8_t kResolutionCell = 0xFD;  ///< AVR RTC_PS2MOUSE_RES_REG
    static constexpr uint8_t kFoundX = 0x00;
    static constexpr uint8_t kFoundY = 0x01;

    /// The state TTD saves (PeripheralId::EvoMouse). Fixed layout, no padding
    struct State
    {
        uint8_t x;
        uint8_t y;
        uint8_t buttons;    ///< wheel nibble, 1, M, R, L (active low)
        uint8_t connected;  ///< a mouse on the PS/2 port (1) or none (0)
    };

    /// `resolution` / `setResolution`: the AVR's RTC cell #FD (battery-backed)
    EvoAvrMouse(std::function<uint8_t()> resolution, std::function<void(uint8_t)> setResolution);

    /// A mouse plugged into the PS/2 port ([INPUT] Mouse=KEMPSTON): a change
    /// re-runs the AVR's mouse reset (found: X = 0, Y = 1; none: X = Y = #FF)
    void SetConnected(bool connected);
    bool IsConnected() const { return _connected.load(std::memory_order_relaxed) != 0; }

    /// Registers as the Z80 reads them: 0 = buttons + wheel, 1 = X, 2 = Y
    uint8_t ReadRegister(uint8_t selectRegister) const;

    /// A keypad key the AVR's keyboard parser saw pressed (set 2 make code, no E0):
    /// '+' #79, '-' #7B, '*' #7C change the resolution while both buttons are held
    void OnKeypadKey(uint8_t scancode);
    uint8_t Resolution() const;

    /// region <IMouseSink>
    bool IsMouseFitted() const override { return IsConnected(); }
    void OnMouseMotion(int dx, int dy) override;
    void OnMouseButtons(uint8_t activeLowMask) override;
    void OnMouseWheel(int steps) override;
    void OnMouseCounters(uint8_t x, uint8_t y) override;
    /// endregion

    /// region <TTD>
    State GetState() const;
    void SetState(const State& state);
    /// endregion

private:
    void ResetRegisters(bool found);

    std::function<uint8_t()> _resolution;
    std::function<void(uint8_t)> _setResolution;
    // Written on the emulator thread, read by automation snapshots too
    std::atomic<uint8_t> _x{0xFF};
    std::atomic<uint8_t> _y{0xFF};
    std::atomic<uint8_t> _buttons{0xFF};
    std::atomic<uint8_t> _connected{0};
};
