#pragma once
#if defined(__linux__)

#include "crashhandler.h"

class CrashHandlerLinux : public CrashHandler
{
public:
    void install() override;
    void uninstall() override;
};

#endif // __linux__
