#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

/// @brief Standardized HUD timings, durations, and TTLs to avoid magic numbers
namespace HudTiming
{
    using namespace std::chrono_literals;

    // --- Toast Display Durations (TTLs) ---
    inline constexpr std::chrono::milliseconds ToastInstant{500};
    inline constexpr std::chrono::milliseconds ToastShort{1500};
    inline constexpr std::chrono::milliseconds ToastNormal{3000};
    inline constexpr std::chrono::milliseconds ToastMedium{4000};
    inline constexpr std::chrono::milliseconds ToastLong{5000};
    inline constexpr std::chrono::milliseconds ToastAlert{6000};

    // Semantic event bindings
    inline constexpr std::chrono::milliseconds ToastDefault = ToastNormal;
    inline constexpr std::chrono::milliseconds ToastDiskInserted{3500};
    inline constexpr std::chrono::milliseconds ToastDiskEjected = ToastShort;
    inline constexpr std::chrono::milliseconds ToastDiskWritten{2500};
    inline constexpr std::chrono::milliseconds ToastDiskSaveRetargeted = ToastAlert;
    inline constexpr std::chrono::milliseconds ToastTapeLoaded = ToastNormal;
    inline constexpr std::chrono::milliseconds ToastTapeLoadFailed = ToastLong;
    inline constexpr std::chrono::milliseconds ToastBreakpointHit = ToastLong;
    inline constexpr std::chrono::milliseconds ToastEmulatorStopped = ToastShort;
    inline constexpr std::chrono::milliseconds ToastSystemReset = ToastShort;
    inline constexpr std::chrono::milliseconds ToastRecordingSaved = ToastLong;
    inline constexpr std::chrono::milliseconds ToastFileLoaded = ToastNormal;
    inline constexpr std::chrono::milliseconds ToastFileLoadFailed = ToastLong;

    // --- Animation & Transition Durations ---
    inline constexpr std::chrono::milliseconds AnimEnter{220};
    inline constexpr std::chrono::milliseconds AnimExit{180};
    inline constexpr std::chrono::milliseconds AnimPulse{550};
    inline constexpr std::chrono::milliseconds AnimRecordingBlink{1000};  // 1Hz recording indicator blink
    inline constexpr std::chrono::milliseconds AnimStackReflow{160};
    inline constexpr std::chrono::milliseconds AnimReducedMotion{100};

    // --- Indicator Durations & Timeouts ---
    inline constexpr std::chrono::milliseconds IndicatorExecuteTimeout{1500};
    inline constexpr std::chrono::milliseconds IndicatorMemoryPageTimeout{1200};

    // --- Cross-frame oscillation detection ---
    // Number of frames to track oscillation patterns (25 frames ≈ 500ms at 50fps)
    inline constexpr uint8_t CrossFrameOscillationWindow = 25;

    // --- ZX Spectrum screen memory pages ---
    // Normal screen uses RAM page 5, shadow screen uses RAM page 7
    inline constexpr uint8_t ScreenNormalPage = 5;
    inline constexpr uint8_t ScreenShadowPage = 7;
} // namespace HudTiming

/// @brief Default limits for toast and indicator queues
namespace HudLimits
{
    inline constexpr size_t DefaultVisibleToasts = 3;
    inline constexpr size_t DefaultQueuedToasts = 3;
} // namespace HudLimits
