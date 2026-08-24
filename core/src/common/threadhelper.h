#pragma once
#include "stdafx.h"

#ifndef UNREAL_THREADHELPER_H
#define UNREAL_THREADHELPER_H

class ThreadHelper
{
public:
   static void setThreadName(const char* name);

   /// Raise the calling thread's scheduling class so periodic frame-deadline
   /// wake-ups are not coalesced by the OS scheduler. Without this the
   /// emulator thread regularly wakes 5-15ms (observed up to ~30ms) past its
   /// absolute frame deadline - macOS QoS timer coalescing on default-class
   /// threads - which the ~40ms audio ring headroom cannot always absorb
   /// (seen in the GUI as "AppSoundManager: ring errors ... dequeue=N"
   /// underruns). Call once on the emulation thread.
   static void setInteractiveQoS();
};


#endif //UNREAL_THREADHELPER_H
