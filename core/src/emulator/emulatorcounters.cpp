#include "stdafx.h"

#include "emulatorcounters.h"

#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / Destructors>

EmulatorCounters::EmulatorCounters(EmulatorContext* context)
{
    _context = context;
    _logger = context->pModuleLogger;
}

EmulatorCounters::~EmulatorCounters()
{

}

/// endregion </Constructors / Destructors>
