#pragma once
#include "stdafx.h"

#ifndef UNREAL_THREADHELPER_H
#define UNREAL_THREADHELPER_H

class ThreadHelper
{
public:
   static void setThreadName(const char* name);

   /// Elevate the CALLING thread to real-time scheduling for periodic
   /// frame-rate work (the emulation / audio-producer role). Best effort per
   /// platform - never fatal:
   /// - macOS:   time-constraint policy, the same scheduling CoreAudio gives
   ///            its own device threads (50 Hz frame period, preemptible
   ///            compute phase so true audio device threads still win)
   /// - Linux:   SCHED_FIFO - a privilege (CAP_SYS_NICE) that unprivileged
   ///            desktop processes don't have; silently stays default
   /// - Windows: above-normal priority - the audio device thread is already
   ///            MMCSS-driven and must stay ahead of the emulation producer
   static void setRealtimePriority();

   /// Restore the CALLING thread to default (timeshare) scheduling
   static void setNormalPriority();
};


#endif //UNREAL_THREADHELPER_H
