#include "nvenc_probe.h"

#ifdef _WIN32
#include <windows.h>
#include "nvEncodeAPI.h"
#endif

#include <sstream>

#ifdef _WIN32
// Function pointer type
typedef NVENCSTATUS (NVENCAPI *PFN_NvEncodeAPIGetMaxSupportedVersion)(uint32_t*);
#endif

// ============================================================================
// Implementation
// ============================================================================

bool NvencProbe::isAvailable()
{
#ifdef _WIN32
    // Try to load the NVENC DLL - it's in System32 if NVIDIA drivers are installed
    HMODULE hNvenc = LoadLibraryA("nvEncodeAPI64.dll");
    if (!hNvenc)
    {
        // Try 32-bit version (for 32-bit builds)
        hNvenc = LoadLibraryA("nvEncodeAPI.dll");
    }

    if (!hNvenc)
        return false;

    // Check if the version query function exists
    FARPROC pfn = GetProcAddress(hNvenc, "NvEncodeAPIGetMaxSupportedVersion");
    bool available = (pfn != nullptr);

    FreeLibrary(hNvenc);
    return available;
#else
    return false;
#endif
}

NvencProbe::NvencInfo NvencProbe::getInfo()
{
    return queryCapabilities();
}

NvencProbe::NvencInfo NvencProbe::queryCapabilities()
{
    NvencInfo info;

#ifdef _WIN32
    HMODULE hNvenc = LoadLibraryA("nvEncodeAPI64.dll");
    if (!hNvenc)
        hNvenc = LoadLibraryA("nvEncodeAPI.dll");

    if (!hNvenc)
        return info;

    // GetProcAddress returns FARPROC; cast to actual function pointer type.
    // GCC warns about this standard Windows pattern, MSVC does not.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    auto pfnGetMaxVersion = reinterpret_cast<PFN_NvEncodeAPIGetMaxSupportedVersion>(
        GetProcAddress(hNvenc, "NvEncodeAPIGetMaxSupportedVersion"));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

    if (pfnGetMaxVersion)
    {
        uint32_t maxVersion = 0;
        if (pfnGetMaxVersion(&maxVersion) == 0)  // 0 = NV_ENC_SUCCESS
        {
            // Check if driver version supports our header version
            uint32_t requiredVersion = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
            if (maxVersion >= requiredVersion)
            {
                info.available = true;
                info.encoderVersion = maxVersion;

                // Version from NvEncodeAPIGetMaxSupportedVersion is (MAJOR << 4) | MINOR
                uint32_t major = maxVersion >> 4;
                uint32_t minor = maxVersion & 0xF;

                // H.264 supported since NVENC 1.0
                info.supportsH264 = true;

                // HEVC supported since NVENC 5.0 (Maxwell Gen 2)
                info.supportsHEVC = (major >= 5);

                // AV1 supported since NVENC 12.0 (Ada Lovelace)
                info.supportsAV1 = (major >= 12);

                info.driverVersion = std::to_string(major) + "." + std::to_string(minor);
            }
        }
    }

    FreeLibrary(hNvenc);
#endif

    return info;
}

std::string NvencProbe::getCapabilityReport()
{
    std::stringstream report;

#ifdef _WIN32
    NvencInfo info = getInfo();

    if (!info.available)
    {
        report << "NVENC: NOT AVAILABLE\n";
        report << "  NVIDIA driver not installed, GPU doesn't support NVENC,\n";
        report << "  or driver version too old (need " << NVENCAPI_MAJOR_VERSION << "." << NVENCAPI_MINOR_VERSION << "+).\n";
        report << "  NVENC requires GeForce GTX 600 series or newer.\n";
        return report.str();
    }

    report << "NVENC: AVAILABLE\n";
    report << "  API Version: " << info.driverVersion << "\n";
    report << "  Codecs:\n";
    report << "    H.264: " << (info.supportsH264 ? "YES" : "no") << "\n";
    report << "    HEVC:  " << (info.supportsHEVC ? "YES" : "no") << "\n";
    report << "    AV1:   " << (info.supportsAV1 ? "YES" : "no") << "\n";

    if (!info.gpuName.empty())
        report << "  GPU: " << info.gpuName << "\n";
#else
    report << "NVENC: NOT AVAILABLE (Windows only)\n";
#endif

    return report.str();
}
