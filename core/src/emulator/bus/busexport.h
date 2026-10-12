#pragma once

/// @file busexport.h
/// @brief Bus trace export (technical-design.md §8): JSON lines with every field, and VCD with the SPI wires
/// synthesized from the transactions, for GTKWave / PulseView (whose SPI and SD-card decoders then run on it).
///
/// VCD: timescale 1 ns, time 0 at the first event. Signals `sck`, `mosi`, `miso` and an active-low `cs_n_<device>`
/// per device slot. A byte is 8 SCK periods (mode 0: data set while SCK is low, sampled on the rising edge) over the
/// bus's byte time, starting at the event's time - or where the previous byte ended when the trace stamped two
/// bytes closer than a byte time (a lazy master stamps at delivery). Protocol events (SD commands, blocks) carry no
/// wires; JSON lines has them.

#include <string>
#include <vector>

#include "emulator/bus/bustraceevent.h"
#include "emulator/bus/bustap.h"

struct BusExportTiming
{
    unsigned frameTStates = 69888;  ///< T-states in a frame
    double cpuHz = 3500000.0;       ///< T-states per second
};

std::string BusExportJsonLines(const BusTapStatus& status, const std::vector<BusTraceEvent>& events);
std::string BusExportVcd(const BusTapStatus& status, const std::vector<BusTraceEvent>& events, const BusExportTiming& timing);
