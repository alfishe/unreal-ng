#pragma once

/// @file dynamiclibrary.h
/// @brief A shared library loaded at run time (dlopen on macOS / Linux, LoadLibrary on Windows), so an optional host
/// component - libpcap / Npcap for the network bridge - needs no build-time dependency and a missing library is a
/// report line, not a start failure. One implementation per OS family: platform/posix, platform/windows.

#include <string>
#include <vector>

class DynamicLibrary
{
public:
    DynamicLibrary() = default;
    ~DynamicLibrary() { Close(); }

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    /// Load the first of `names` the system finds (a bare name uses the system's search path). False with `error`
    /// naming every attempt
    bool Open(const std::vector<std::string>& names, std::string& error);
    void Close();
    bool IsOpen() const { return _handle != nullptr; }
    /// The name that loaded
    const std::string& Name() const { return _name; }

    /// A function or variable by its exported name; nullptr when missing
    void* Symbol(const char* name) const;

private:
    void* _handle = nullptr;
    std::string _name;
};
