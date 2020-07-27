#include "stdafx.h"

#include "common/logger.h"

#include "keyboard.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / Destructors>

Keyboard::Keyboard(EmulatorContext *context)
{
    _context = context;
}

Keyboard::~Keyboard()
{

}

/// endregion </Constructors / Destructors>

/// region <Keyboard control>

/// Handle system reset
void Keyboard::Reset()
{
    // Clear ZX-Spectrum keyboard matrix state
    memset(_keyboard_matrix, 0xFF, sizeof (_keyboard_matrix));
}

void Keyboard::PressKey()
{

}

void Keyboard::ReleaseKey()
{

}

void Keyboard::TypeSymbol(char symbol)
{

}

void Keyboard::SendKeyCombination()
{

}

/// endregion </Keyboard control>

/// region <Handle keyboard events>

/// Respond on port IN request from Z80
/// \param port
/// \return
uint8_t Keyboard::HandlePort(uint16_t port)
{
    uint8_t result = 0xFF;

    uint8_t portFE = port & 0x00FF;
    uint8_t subport = port >> 8;
    //uint8_t matrix_index = ~subport

    if (portFE == 0xFE)
    {
        switch (subport)
        {
            case 0xFE:      // Caps Shift, Z, X, C, V
                break;
            case 0xFD:      // A, S, D, F, G
                break;
            case 0xFB:      // Q, W, E, R, T
                break;
            case 0xF7:      // 1, 2, 3, 4, 5
                break;
            case 0xEF:      // 0, 9, 8, 7, 6
                break;
            case 0xDF:      // P, O, I, U, Y
                break;
            case 0xBF:      // Enter, L, K, J, H
                break;
            case 0x7F:      // Space, Symbol Shift, M, N, B
                break;
        }
    }
    else
    {
        LOGERROR("Keyboard cannot handle non #FE port");
        assert("Keyboard non-#FE port");
    }

    return result;
}

/// Translate host keyboard event to ZX-Spectrum
/// See: http://slady.net/Sinclair-ZX-Spectrum-keyboard/
///
/// \param key
/// \param isPressed
/// \param shift
/// \param ctrl
/// \param alt
void Keyboard::OnKey(char key, bool isPressed, bool shift, bool ctrl, bool alt)
{
    // Cursor keys:
    // Left Arrow   - Caps Shift + 5
    // Right Arrow  - Caps Shift + 8
    // Up Arrow     - Caps Shift + 7
    // Down Arrow   - Caps Shift + 6


}

/// endregion </Handle keyboard events>