#pragma once

// The name a file an INCLUDE / INCBIN names has on a retro assembler's disk (TR-DOS: no folders, 8 characters)

#include <string>

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
/// BackendOptions::fileNames' name for it when the project gives one, else the name without its folders
inline std::string DiskName(const BackendOptions& options, const std::string& name)
{
    if (const auto found = options.fileNames.find(name); found != options.fileNames.end())
        return found->second;
    const size_t slash = name.find_last_of("/\\");
    return slash == std::string::npos ? name : name.substr(slash + 1);
}
}  // namespace unrealasm::dialects
