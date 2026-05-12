#pragma once
#ifdef _WIN32

#include "crashhandler.h"

class CrashHandlerWindows : public CrashHandler
{
public:
    void install() override;
    void uninstall() override;
};

#endif // _WIN32
