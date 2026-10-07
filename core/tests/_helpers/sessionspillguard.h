#pragma once

/// @file sessionspillguard.h
/// @brief Run a test with sessions that keep almost nothing in memory (multi-source phases/c10e-session-journal.md
/// §6): new sessions get one arena of 2 sectors and their temp journals go to a scratch folder of the test; the
/// settings are put back afterwards. `Spilled()` says whether any chunk went to a spill file meanwhile, so a test can prove it
/// ran over the spill tier. Fixtures take it as their first member, driven by a bool test parameter:
///
///     class Foo_Test : public ::testing::TestWithParam<bool> { SessionSpillGuard _spill{GetParam()}; ... };
///     INSTANTIATE_TEST_SUITE_P(Tiers, Foo_Test, ::testing::Bool(), SessionSpillGuard::TierName);

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/io/storage/sessionwritemap.h"

class SessionSpillGuard
{
public:
    explicit SessionSpillGuard(bool spill)
        : _active(spill), _old(SessionWriteMap::Defaults()), _chunksBefore(SessionWriteMap::TotalSpilledChunks())
    {
        if (!_active)
            return;
        _folder = FileHelper::ToFsPath(TestPathHelper::GetUniqueTestScratchPath("session-spill-guard"));
        std::error_code ec;
        std::filesystem::create_directories(_folder, ec);
        SessionSettings tiny = _old;
        tiny.memoryLimit = 1024;
        tiny.arenaBytes = 1024;
        tiny.spillFolder = FileHelper::FromFsPath(_folder);
        SessionWriteMap::SetDefaults(tiny);
    }

    ~SessionSpillGuard()
    {
        SessionWriteMap::SetDefaults(_old);
        if (_active)
        {
            std::error_code ec;
            std::filesystem::remove_all(_folder, ec);
        }
    }

    SessionSpillGuard(const SessionSpillGuard&) = delete;
    SessionSpillGuard& operator=(const SessionSpillGuard&) = delete;

    bool Active() const { return _active; }
    /// Chunks went to a spill file since the guard was made
    bool Spilled() const { return SessionWriteMap::TotalSpilledChunks() > _chunksBefore; }

    /// "Memory" / "Spilled" for INSTANTIATE_TEST_SUITE_P
    static std::string TierName(const ::testing::TestParamInfo<bool>& info) { return info.param ? "Spilled" : "Memory"; }

private:
    bool _active;
    SessionSettings _old;
    uint64_t _chunksBefore;
    std::filesystem::path _folder;
};
