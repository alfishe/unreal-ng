#pragma once
#include "stdafx.h"

#include <emulator/emulatorcontext.h>

/// region <Info>

// See: https://worldofspectrum.org/faq/reference/z80format.htm

/// endregion </Info>

/// region <Types>
/// endregion </Types>

class LoaderZ80
{
protected:
    EmulatorContext* _context;

public:
    LoaderZ80(EmulatorContext* context);
    virtual ~LoaderZ80();
};
