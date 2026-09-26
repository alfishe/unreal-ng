#pragma once
#include "stdafx.h"

#ifndef UNREAL_THREADHELPER_H
#define UNREAL_THREADHELPER_H

/// Scheduling roles. Every thread gets one of three, set by the thread itself:
///
/// | Role        | Who                                  | macOS                    | Linux              | Windows            |
/// |-------------|--------------------------------------|--------------------------|--------------------|--------------------|
/// | Realtime    | the ACTIVE emulator instance's loop  | time-constraint policy   | SCHED_FIFO (needs  | ABOVE_NORMAL       |
/// |             | (cadenced playback only, not turbo)  |                          | CAP_SYS_NICE)      |                    |
/// | Interactive | per-frame hand-off to the UI         | QoS USER_INTERACTIVE     | default            | default            |
/// |             | (MessageCenter worker)               |                          |                    |                    |
/// | Normal      | everything else: other instances,    | timeshare, default QoS   | default            | NORMAL             |
/// |             | turbo, automation, encoders, tests   |                          |                    |                    |
///
/// The audio device thread is never ours to set: CoreAudio, WASAPI (MMCSS)
/// and - where not - our Linux callback elevate it, and it outranks all three.
/// On macOS a default-QoS thread under a parallel build wakes 25-100 ms late
/// (timer coalescing + timeshare decay); a user-interactive one ~15 ms, a
/// time-constraint one ~0.1 ms. Linux and Windows keep their stock
/// behaviour for Interactive: neither lets an unprivileged process raise a
/// thread above normal in a way that matters here, and neither coalesces a
/// normal thread's timers the way macOS does.
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

   /// Mark the CALLING thread latency-sensitive but not real-time (the
   /// Interactive role above). Best effort, never fatal
   static void setInteractivePriority();

   /// Restore the CALLING thread to default (timeshare) scheduling
   static void setNormalPriority();
};


#endif //UNREAL_THREADHELPER_H
