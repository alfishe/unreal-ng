#pragma once

/// @file espdescribe.h
/// @brief One status report for an emulated ESP module, whatever port it sits on (the SprinterESP card, the ZiFi
/// line, the COM port, a ZX-WiFi card): its firmware and state, the Wi-Fi, the AT session or the ZiFi native
/// session, the last exchanges. Every automation surface and the Qt Network window show this node.

#include "emulator/state/statenode.h"

class EspModule;

namespace espdescribe
{
/// Fill `out` (an object) with the module's report
void Describe(const EspModule& esp, StateNode& out);
}  // namespace espdescribe
