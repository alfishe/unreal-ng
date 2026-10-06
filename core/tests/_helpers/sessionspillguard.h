#pragma once

/// @file sessionspillguard.h
/// @brief Run a test with sessions that spill almost everything (multi-source phases/c10d-session-spill.md §5):
/// new sessions get a memory limit of two sectors and their spill files go to a scratch folder of the test; both are
/// put back afterwards. `Spilled()` says whether any chunk went to a spill file meanwhile, so a test can prove it
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
        : _active(spill), _oldLimit(SessionWriteMap::DefaultMemoryLimit()), _oldFolder(SessionWriteMap::SpillFolder()),
          _chunksBefore(SessionWriteMap::TotalSpilledChunks())
    {
        if (!_active)
            return;
        _folder = FileHelper::ToFsPath(TestPathHelper::GetUniqueTestScratchPath("session-spill-guard"));
        std::error_code ec;
        std::filesystem::create_directories(_folder, ec);
        SessionWriteMap::SetDefaultMemoryLimit(2 * SessionWriteMap::kHotEntryBytes);
        SessionWriteMap::SetSpillFolder(FileHelper::FromFsPath(_folder));
    }

    ~SessionSpillGuard()
    {
        SessionWriteMap::SetDefaultMemoryLimit(_oldLimit);
        SessionWriteMap::SetSpillFolder(_oldFolder);
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
    uint64_t _oldLimit;
    std::string _oldFolder;
    uint64_t _chunksBefore;
    std::filesystem::path _folder;
};
