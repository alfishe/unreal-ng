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