#pragma once
#ifdef __APPLE__

#include "crashhandler.h"

class CrashHandlerMacOS : public CrashHandler
{
public:
    void install() override;
    void uninstall() override;
};

#endif // __APPLE__
