#pragma once

/// @file ttdv1tests.h
/// @brief The test files that drive TTD v1 (`TimeTravelManager`) directly
/// (Phase 5 Step 4, 2026-10-06). core-tests record with the engine, as the
/// application does; a test from one of these files gets v1 instead, selected
/// before its suite and before each test (`TtdBackendListener`), so the
/// machines it creates hand their frames to the v1 manager it drives.
///
/// A single v1 test in a file that records with the engine opens a `V1Scope`.
/// The list goes with v1 (Phase 6). New tests do not belong here: they use the
/// selected implementation (`ttd::TTDSessionRef`, `TTDControl`) or a
/// `TimeTravelController` of their own. A file moved to the engine leaves the
/// list.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "emulator/emulator.h"

namespace ttdtest
{
/// Paths below core/tests/
inline constexpr const char* kV1TestFiles[] = {
    "debugger/ttd/bench/ttdv1feeder_test.cpp",
    "debugger/ttd/engine/ttdsessionfile_test.cpp",
    "debugger/ttd/timetravelcontroller_corpus_test.cpp",
    "debugger/ttd/timetravelcontroller_test.cpp",
    "debugger/ttd/timetravelframecache_test.cpp",
    "debugger/ttd/timetravelhooks_test.cpp",
    "debugger/ttd/timetravelmanager_display_test.cpp",
    "debugger/ttd/timetravelmanager_engineseek_test.cpp",
    "debugger/ttd/timetravelmanager_historylimit_test.cpp",
    "debugger/ttd/timetravelmanager_hostwrites_test.cpp",
    "debugger/ttd/timetravelmanager_journalcapacity_test.cpp",
    "debugger/ttd/timetravelmanager_journalsegments_test.cpp",
    "debugger/ttd/timetravelmanager_portjournal_test.cpp",
    "debugger/ttd/timetravelmanager_publishedinfo_test.cpp",
    "debugger/ttd/timetravelmanager_recordingguard_test.cpp",
    "debugger/ttd/timetravelmanager_regeneratewrites_test.cpp",
    "debugger/ttd/timetravelmanager_resumesave_test.cpp",
    "debugger/ttd/timetravelmanager_rzxplayback_test.cpp",
    "debugger/ttd/timetravelmanager_savedinputs_test.cpp",
    "debugger/ttd/timetravelmanager_screenshot_test.cpp",
    "debugger/ttd/timetravelmanager_servicestate_test.cpp",
    "debugger/ttd/timetravelmanager_shadow_test.cpp",
    "debugger/ttd/timetravelmanager_shadowfile_test.cpp",
    "debugger/ttd/timetravelmanager_shadowmodels_test.cpp",
    "debugger/ttd/timetravelmanager_shadowregions_test.cpp",
    "debugger/ttd/timetravelmanager_turboclock_test.cpp",
    "debugger/ttd/ttdautomationcontract_test.cpp",
    "debugger/ttd/ttdbookmarks_test.cpp",
    "debugger/ttd/ttdclipexport_test.cpp",
    "debugger/ttd/ttdcontrol_test.cpp",
    "debugger/ttd/ttdcorpus_test.cpp",
    "debugger/ttd/ttdcoverageintegration_test.cpp",
    "debugger/ttd/ttdcoveragequery_test.cpp",
    "debugger/ttd/ttddevicereplay_test.cpp",
    "debugger/ttd/ttddivergencecorpus_test.cpp",
    "debugger/ttd/ttddumpformat_test.cpp",
    "debugger/ttd/ttdexternalevents_test.cpp",
    "debugger/ttd/ttdexternaleventshooks_test.cpp",
    "debugger/ttd/ttdfeaturegating_test.cpp",
    "debugger/ttd/ttdfileinfo_test.cpp",
    "debugger/ttd/ttdfindlastall_test.cpp",
    "debugger/ttd/ttdformatv2_test.cpp",
    "debugger/ttd/ttdfullrestore_test.cpp",
    "debugger/ttd/ttdinputapply_test.cpp",
    "debugger/ttd/ttdinputjournal_test.cpp",
    "debugger/ttd/ttdinputplayback_test.cpp",
    "debugger/ttd/ttdlifecyclestress_test.cpp",
    "debugger/ttd/ttdmanager_test.cpp",
    "debugger/ttd/ttdmodelpagebounds_test.cpp",
    "debugger/ttd/ttdmodelstatecontract_test.cpp",
    "debugger/ttd/ttdpage255_test.cpp",
    "debugger/ttd/ttdprobesafety_test.cpp",
    "debugger/ttd/ttdreplaymode_test.cpp",
    "debugger/ttd/ttdrestore_test.cpp",
    "debugger/ttd/ttdresume_test.cpp",
    "debugger/ttd/ttdreverseexecutor_test.cpp",
    "debugger/ttd/ttdseek_test.cpp",
    "debugger/ttd/ttdseekexhaustive_test.cpp",
    "debugger/ttd/ttdserializationrobustness_test.cpp",
    "debugger/ttd/ttdsessionlifecycle_test.cpp",
    "debugger/ttd/ttdstatecompleteness_test.cpp",
    "debugger/ttd/ttdstatusendpoint_test.cpp",
    "debugger/ttd/ttdstepinstruction_test.cpp",
    "debugger/ttd/ttdsubsystemrestore_test.cpp",
    "debugger/ttd/ttdthinning_test.cpp",
    "debugger/ttd/ttdwritejournal_test.cpp",
    "debugger/ttd/ttdwritejournale2e_test.cpp",
};

/// True when @p file (a test's __FILE__) is one of kV1TestFiles
inline bool IsV1TestFile(const char* file)
{
    if (!file)
        return false;
    std::string path(file);
    for (char& c : path)
        if (c == '\\')
            c = '/';
    for (const char* entry : kV1TestFiles)
    {
        const std::string suffix = std::string("core/tests/") + entry;
        if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
            return true;
    }
    return false;
}

/// One test that checks v1 itself in a file that records with the engine (v1's
/// file format, its blob layouts): the machines created while it lives record
/// with v1; the engine again after it
class V1Scope
{
public:
    V1Scope() { Emulator::SetDefaultTimeTravelBackend(Emulator::TimeTravelBackend::V1); }
    ~V1Scope() { Emulator::SetDefaultTimeTravelBackend(Emulator::TimeTravelBackend::Engine); }
    V1Scope(const V1Scope&) = delete;
    V1Scope& operator=(const V1Scope&) = delete;
};

/// Selects the backend for each suite (its fixture may create machines in
/// SetUpTestSuite) and each test: v1 for the files above, the engine otherwise
class TtdBackendListener : public ::testing::EmptyTestEventListener
{
public:
    void OnTestSuiteStart(const ::testing::TestSuite& suite) override
    {
        if (suite.total_test_count() > 0)
            Select(suite.GetTestInfo(0)->file());
    }
    void OnTestStart(const ::testing::TestInfo& info) override { Select(info.file()); }

private:
    static void Select(const char* file)
    {
        Emulator::SetDefaultTimeTravelBackend(IsV1TestFile(file) ? Emulator::TimeTravelBackend::V1
                                                                 : Emulator::TimeTravelBackend::Engine);
    }
};
}  // namespace ttdtest
