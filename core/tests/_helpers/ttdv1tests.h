#pragma once

/// @file ttdv1tests.h
/// @brief The test files that drive TTD v1 (`TimeTravelManager`) directly
/// (Phase 5 Step 4, 2026-10-06). core-tests record with the engine, as the
/// application does; a test from one of these files gets v1 instead, selected
/// before its suite and before each test (`TtdBackendListener`), so the
/// machines it creates hand their frames to the v1 manager it drives.
///
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
    "automation/dezog/dezogdebugadapter_test.cpp",
    "debugger/joystick/debugjoystickmanager_test.cpp",
    "debugger/media/sectorwrite_test.cpp",
    "debugger/mouse/debugmousemanager_machines_test.cpp",
    "debugger/mouse/debugmousemanager_paused_test.cpp",
    "debugger/mouse/debugmousemanager_test.cpp",
    "debugger/ports/portwrite_test.cpp",
    "debugger/ttd/atm/ttdatmpaging_test.cpp",
    "debugger/ttd/atm/ttdevops2_test.cpp",
    "debugger/ttd/bench/ttdv1feeder_test.cpp",
    "debugger/ttd/engine/ttdsessionfile_test.cpp",
    "debugger/ttd/ide/ttdatachannel_test.cpp",
    "debugger/ttd/ide/ttdcddrive_test.cpp",
    "debugger/ttd/network/ttdzxnetusb_test.cpp",
    "debugger/ttd/profi/ttdprofipaging_test.cpp",
    "debugger/ttd/scorpion/ttdscorpionpaging_test.cpp",
    "debugger/ttd/scorpion/ttdscorpionprofrom_test.cpp",
    "debugger/ttd/sprinter/ttdsprinter_test.cpp",
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
    "debugger/ttd/ttdayserializer_test.cpp",
    "debugger/ttd/ttdbookmarks_test.cpp",
    "debugger/ttd/ttdclipexport_test.cpp",
    "debugger/ttd/ttdcontrol_test.cpp",
    "debugger/ttd/ttdcorpus_test.cpp",
    "debugger/ttd/ttdcoverageintegration_test.cpp",
    "debugger/ttd/ttdcoveragequery_test.cpp",
    "debugger/ttd/ttdcovoxserializer_test.cpp",
    "debugger/ttd/ttddevicereplay_test.cpp",
    "debugger/ttd/ttddivergencecorpus_test.cpp",
    "debugger/ttd/ttdds12887_test.cpp",
    "debugger/ttd/ttddumpformat_test.cpp",
    "debugger/ttd/ttdevoflash_test.cpp",
    "debugger/ttd/ttdexternalevents_test.cpp",
    "debugger/ttd/ttdexternaleventshooks_test.cpp",
    "debugger/ttd/ttdfeaturegating_test.cpp",
    "debugger/ttd/ttdfileinfo_test.cpp",
    "debugger/ttd/ttdfindlastall_test.cpp",
    "debugger/ttd/ttdformatv2_test.cpp",
    "debugger/ttd/ttdfullrestore_test.cpp",
    "debugger/ttd/ttdgeneralsoundswitch_test.cpp",
    "debugger/ttd/ttdinputapply_test.cpp",
    "debugger/ttd/ttdinputjournal_test.cpp",
    "debugger/ttd/ttdinputplayback_test.cpp",
    "debugger/ttd/ttdlifecyclestress_test.cpp",
    "debugger/ttd/ttdmanager_test.cpp",
    "debugger/ttd/ttdmodelpagebounds_test.cpp",
    "debugger/ttd/ttdmodelstatecontract_test.cpp",
    "debugger/ttd/ttdmultisound_test.cpp",
    "debugger/ttd/ttdneogs_test.cpp",
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
    "debugger/ttd/ttdtapeserializer_test.cpp",
    "debugger/ttd/ttdthinning_test.cpp",
    "debugger/ttd/ttdtsfm_test.cpp",
    "debugger/ttd/ttdvdac2_test.cpp",
    "debugger/ttd/ttdwd1793serializer_test.cpp",
    "debugger/ttd/ttdwritejournal_test.cpp",
    "debugger/ttd/ttdwritejournale2e_test.cpp",
    "emulator/emulator_test.cpp",
    "emulator/io/fdc/diskfastload_test.cpp",
    "emulator/io/fdc/upd765_test.cpp",
    "emulator/io/ide/cdaudiocontrol_test.cpp",
    "emulator/io/ide/idecontroller_test.cpp",
    "emulator/io/keyboard/atm2kbc_test.cpp",
    "emulator/io/keyboard/profixtkbc_test.cpp",
    "emulator/io/sprinter/isa/isazxbusadapter_test.cpp",
    "emulator/io/tape/tapeloading_integration_test.cpp",
    "emulator/io/tape/tapeslot_test.cpp",
    "emulator/machines/sprinter/sprintergeneralsound_test.cpp",
    "emulator/machines/sprinter/sprinternetwork_test.cpp",
    "emulator/machines/sprinter/sprinternetworkkit_test.cpp",
    "emulator/machines/sprinter/sprinterzxmode_test.cpp",
    "emulator/machines/tsconf/tsconfmemoryregions_test.cpp",
    "emulator/media/mediamanager_test.cpp",
    "emulator/memory/memorycontended_test.cpp",
    "emulator/ports/models/portdecoder_atm3_test.cpp",
    "emulator/ports/models/portdecoder_spectrum3_test.cpp",
    "emulator/profi_boot_test.cpp",
    "emulator/slots/slotchange_test.cpp",
    "emulator/slots/slotcontrol_test.cpp",
    "emulator/slots/slotttd_test.cpp",
    "emulator/sound/chips/neogs/neogsmedia_test.cpp",
    "emulator/sound/chips/soundchip_turbosound_test.cpp",
    "emulator/sound/tsfm/tsfm_core_test.cpp",
    "emulator/sound/tsfm/ym2203pair_test.cpp",
    "emulator/video/contention_test.cpp",
    "emulator/zxpoly/zxpolygroup_test.cpp",
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
