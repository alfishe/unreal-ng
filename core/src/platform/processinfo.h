#pragma once

/// @file processinfo.h
/// @brief Process ids across systems: this process's id, and whether another
/// one still runs (a recording folder names its writer by id, so a folder
/// whose writer is gone is a leftover of a crash). One implementation per
/// system family: platform/posix (kill with signal 0), platform/windows
/// (OpenProcess + GetExitCodeProcess).

#include <cstdint>

namespace platform
{
    uint32_t CurrentProcessId();
    /// True when a process with this id runs (or exists but cannot be
    /// signalled by us); false when there is none
    bool IsProcessAlive(uint32_t pid);
}  // namespace platform
