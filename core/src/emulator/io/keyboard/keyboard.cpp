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
    uint8_t result;

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