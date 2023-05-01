#pragma once

class EmulatorContext;

class Tape
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Tape() = delete;    // Disable default constructor. C++ 11 feature
    Tape(EmulatorContext* context) : _context(context) {};
    virtual ~Tape() = default;
    /// endregion </Constructors / Destructors>

public:
    uint8_t handlePortIn();
};
