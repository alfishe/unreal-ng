#pragma once

class CrashHandler
{
public:
    virtual ~CrashHandler() = default;
    virtual void install() = 0;
    virtual void uninstall() = 0;

    // Returns a platform-specific implementation. Caller owns the pointer.
    static CrashHandler* create();
};
