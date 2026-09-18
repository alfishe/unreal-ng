#pragma once

// MCP smart tool: capture_media — screenshots, digests, video, audio
//
// screenshot / screen_digest wrap the capture endpoints; record_start can run
// a bounded recording with every_nth:"auto" visual-quantum detection (screen
// digests sampled frame-by-frame; static frames are skipped by pausing the
// recorder between updates); audio_capture is a one-shot arm → run → analyze.

#include "mcp-tools.h"

namespace mcp
{

/// Registers the capture_media tool
void RegisterCaptureMedia(ToolRegistry& registry);

} // namespace mcp
