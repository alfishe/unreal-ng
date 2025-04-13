#pragma once
#include "pch.h"

class FileHelper_Test : public ::testing::Test
{
protected:
    // Helper function to handle platform-specific paths
    static std::string PlatformPath(const std::string& path)
    {
        // On macOS, realpath() returns paths with /private prefix for existing files
        // For non-existent files, we get the regular path
        #ifdef __APPLE__
            // Try to resolve the path - if it exists, it will have /private prefix
            char resolved[PATH_MAX];
            if (realpath(path.c_str(), resolved) != nullptr)
            {
                return std::string(resolved);
            }
            // Try to resolve the parent directory
            size_t lastSlash = path.find_last_of('/');
            if (lastSlash != std::string::npos)
            {
                std::string parentDir = path.substr(0, lastSlash);
                if (realpath(parentDir.c_str(), resolved) != nullptr)
                {
                    return std::string(resolved) + path.substr(lastSlash);
                }
            }
        #endif
        return path;
    }

    void SetUp() override;
    void TearDown() override;
};