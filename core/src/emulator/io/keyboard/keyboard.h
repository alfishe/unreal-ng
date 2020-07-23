#pragma once
#include "stdafx.h"

class EmulatorContext;

class Keyboard
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Keyboard() = delete;    // Disable default constructor. C++ 11 feature
    Keyboard(EmulatorContext* context);
    virtual ~Keyboard();
    /// endregion </Constructors / Destructors>
};
