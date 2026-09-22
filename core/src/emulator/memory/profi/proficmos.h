#pragma once
#include "stdafx.h"

#include <ctime>
#include <chrono>
#include "emulator/io/rtc/ds12885.h"

/// DS12885-style RTC/CMOS register file (256-byte bank; time/date registers
/// synthesized from host time) - the same chip-level logic ATM3 wires through
/// memory/atm/cmos.h.
///
/// Lives under memory/profi/, not shared with ATM3's CMOS class: Profi's real
/// hardware RTC (karabas-pro clone: a 256-byte dual-port RAM fed by the AVR,
/// no counting clock, no I2C) is a different, unproven implementation, and
/// ATM3's class also carries I2C NVRAM state Profi never had. Only the
/// register map (ds12885.h) is shared, per that header's own rule.
class ProfiCMOS
{
protected:
    uint8_t _cmos[0x100] = {};
    CMOSTypeEnum _cmos_type = None;
    uint8_t _cmos_addr = 0;

    // Time/date register state. Per-instance (see ATM3's CMOS header for why:
    // a function-local static here would let every ProfiCMOS instance in the
    // process share one clock).
    tm _lastTime = {};
    bool _updateFinished = false;
    unsigned _seconds = 0;
    std::chrono::steady_clock::time_point _lastSample{};
    bool _timeValid = false;

    // Deterministic clock: when set, time-register reads serve this frozen
    // instant instead of live host time (tests / replay).
    bool _fixedTime = false;
    time_t _fixedTimeValue = 0;

public:
    void SetCMOSType(CMOSTypeEnum type);
    void SetCMOSAddress(uint8_t addr);
    void WriteCMOS(uint8_t val);
    uint8_t ReadCMOS();

    /// Serve a frozen instant instead of live host time (tests / replay).
    void SetFixedTime(time_t t);
    /// Back to live host time.
    void UseLiveTime();

protected:
    uint8_t DecodeFromBCD(uint8_t binary);
};
