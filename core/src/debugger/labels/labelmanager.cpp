#include "stdafx.h"
#include "labelmanager.h"

#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / destructors>

LabelManager::LabelManager(EmulatorContext *context)
{
    _context = context;
    _logger = _context->pModuleLogger;
}

LabelManager::~LabelManager()
{

}

/// endregion </Constructors / destructors>
