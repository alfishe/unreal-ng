#pragma once

/// @file gsmailbox.h
/// @brief Shared host <-> card latch block - exact original semantics
/// (Unreal gsz80.cpp gsdata_out/gsdata_in/gscmd/gsstat, ZXMAK2
/// GeneralSoundDevice.cs): single latch per direction plus one shared
/// status byte, no queues.
///
/// Protocol truth (verified against Unreal, ZXMAK2 and the gs105a firmware):
/// - status bit7 = data flip-flop, shared by BOTH directions:
///     host OUT #B3 -> set; card IN/OUT DATRG (0x02) -> clear;
///     card IN/OUT OUTRG (0x03) -> set; host IN #B3 -> clear.
/// - status bit0 = command flip-flop:
///     host OUT #BB -> set; card RSCOM (0x05, in or out) -> clear.
///     The card-side COMRG read (0x01) does NOT touch it.
/// - IN #BB returns status | 0x7E (bits 1-6 are pull-ups).

#include <cstdint>

#include "emulator/sound/chips/gs/gsporttrace.h"  // GSActivityCounters

struct GSForwardMailbox
{
    // Latches and the ZX-visible status byte (Unreal names
    // gsdata_out / gsdata_in / gscmd / gsstat)
    uint8_t dataFromHost = 0;    // host OUT #B3 latch, card DATRG read
    uint8_t dataToHost = 0;      // card OUTRG write latch, host IN #B3 read
    uint8_t commandFromHost = 0; // host OUT #BB latch, card COMRG read
    uint8_t status = 0;          // bit7 = data flip-flop, bit0 = command flip-flop

    // Owner's counters (diagnostics only; nullptr = silent)
    GSActivityCounters* counters = nullptr;

    /// Full power-on state (creation, ZX reset with GSReset=0)
    void resetAll()
    {
        dataFromHost = 0;
        dataToHost = 0;
        commandFromHost = 0;
        status = 0;
    }
};
