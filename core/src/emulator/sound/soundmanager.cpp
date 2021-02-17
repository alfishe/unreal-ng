#include "stdafx.h"
#include "soundmanager.h"

#include "emulator/emulatorcontext.h"

/// region <Constructors / Destructors>

SoundManager::SoundManager(EmulatorContext *context)
{
    _context = context;
    _logger = context->pModuleLogger;
}

SoundManager::~SoundManager()
{

}

/// endregion </Constructors / Destructors>

/// region <Methods>

void SoundManager::reset()
{

}

/// endregion </Methods>