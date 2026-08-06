#pragma once

#include <string>

/// Forward declarations
struct EncoderConfig;
namespace RecordingTypes { struct HWAccelInfo; }

/// @brief Realtime encoding capability estimator
///
/// Determines whether a given encoding configuration can keep up with
/// the 50Hz frame budget (20ms per frame) in realtime. If not, the
/// emulator will switch to non-realtime mode where every frame is
/// captured with correct PTS but emulation may slow down.
///
/// Uses a heuristic table for instant estimates, plus an optional
/// benchmark that encodes a few test frames for empirical measurement.
class RealtimeEstimator
{
public:
    /// @brief Realtime capability result
    enum class RealtimeCapability
    {
        Realtime,      ///< Encoding fits within frame budget
        NonRealtime,   ///< Encoding will exceed frame budget
        Unknown        ///< Cannot determine
    };

    /// @brief Benchmark result from empirical test encoding
    struct BenchmarkResult
    {
        double avgMsPerFrame = 0.0;  ///< Average encoding time per frame
        bool fitsRealtime = false;   ///< True if avgMsPerFrame < frame budget
        std::string details;         ///< Human-readable details
    };

    /// @brief Heuristic estimate based on codec/platform table (instant)
    /// @param config Encoder configuration
    /// @param useNativeEncoder Whether a native (hardware) encoder is being used
    /// @param ffmpegHwInfo Hardware acceleration info from FFmpegProbe (optional)
    /// @return Estimated realtime capability
    static RealtimeCapability estimate(const EncoderConfig& config,
                                       bool useNativeEncoder,
                                       bool ffmpegHwVideoToolbox = false,
                                       bool ffmpegHwNVENC = false,
                                       bool ffmpegHwQS = false,
                                       bool ffmpegHwVA = false);

    /// @brief Get human-readable reason for the estimate
    /// @param config Encoder configuration
    /// @param useNativeEncoder Whether a native encoder is being used
    /// @return Explanation string
    static std::string getReason(const EncoderConfig& config,
                                 bool useNativeEncoder);

    /// @brief Get the frame time budget in milliseconds
    /// @param config Encoder configuration
    /// @return Frame budget in ms (e.g., 20.0 for 50fps)
    static double getFrameBudgetMs(const EncoderConfig& config);

private:
    /// Frame budget threshold: if encode time exceeds this fraction of frame budget,
    /// we consider it non-realtime
    static constexpr double REALTIME_THRESHOLD = 0.8; // 80% of frame budget
};
