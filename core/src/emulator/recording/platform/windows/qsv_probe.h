#pragma once

#include <string>

#ifdef _WIN32

/// Intel QuickSync Video capability detection
namespace QsvProbe
{

struct QsvInfo
{
    bool available = false;
    bool supportsH264 = false;
    bool supportsHEVC = false;
    std::string adapterName;
};

/// Check if QSV hardware encoding is available
bool isAvailable();

/// Get detailed QSV information (may be slow on first call)
QsvInfo getInfo();

} // namespace QsvProbe

#endif // _WIN32
