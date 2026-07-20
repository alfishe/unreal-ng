#pragma once

#include <string>

/// @brief NVENC hardware encoder detection
/// Checks if NVIDIA NVENC is available on this system by probing
/// for the nvEncodeAPI DLL and querying encoder capabilities.
class NvencProbe
{
public:
    struct NvencInfo
    {
        bool available = false;
        bool supportsH264 = false;
        bool supportsHEVC = false;
        bool supportsAV1 = false;
        std::string driverVersion;
        std::string gpuName;
        uint32_t encoderVersion = 0;
    };

    /// @brief Check if NVENC is available on this system
    /// @return true if nvEncodeAPI DLL is loadable and functional
    static bool isAvailable();

    /// @brief Get detailed NVENC capability info
    /// @return NvencInfo struct with encoder details
    static NvencInfo getInfo();

    /// @brief Get a human-readable capability report
    static std::string getCapabilityReport();

private:
    static NvencInfo queryCapabilities();
};
