#pragma once

/// @file deviceaccessnode.h
/// @brief The device access evidence (deviceaccesswatch.h) as the automation planes show it: the `evidence` of
/// `controllers auto` and the monitoring name `devices.access` (GET /api/v1/monitor/devices.access, the WebSocket
/// topic, CLI / Python / Lua `monitor`, MCP `inspect_state` aspect `monitor`). One builder, so every plane shows the
/// same fields.

#include "emulator/ports/deviceaccesswatch.h"
#include "emulator/state/statenode.h"

/// {active, frames_observed, window, dropped_channels, channels: [{device, channel, dir, port, input?, frames,
/// reads_per_frame, tested_bits?, sites: [{pc, count, tested_bits?}], other_sites}]}: the channels with an access in
/// the window, in first-seen order
StateNode DeviceAccessEvidenceNode(const DeviceAccessEvidence& evidence);
