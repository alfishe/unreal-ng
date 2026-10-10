#include "stdafx.h"
#include "pch.h"

/// @file ttdfileinfo_test.cpp
/// @brief ReadTTDFileInfo: a .ttd file's header, sections and recorded machine
/// read without loading the session; TTDSessionInfo::machine for the current
/// session.
///
///   - the peripheral mask in the header equals the baseline checkpoint's
///     device set, and the header fields match the recorded session;
///   - a file written before the mask (flag clear, bytes zero) is walked to
///     the first checkpoint instead and gives the same device set;
///   - the recorded machine of a live session, of the file and of the
///     reloaded session are the same;
///   - the General Sound card of the recording is named (the reason a caller
///     reads the info: fit that card before the load);
///   - a stream that is not a .ttd file is refused.

#include <gtest/gtest.h>

#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttddumpformat.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"

namespace
{

/// A model with a General Sound slot card fitted from boot (as ttdgeneralsoundswitch_test)
constexpr const char* kGsCapableModel = "ATM710";

}  // namespace

class TTDFileInfo_Test : public ::testing::Test
{
protected:
    SoundCardScope _soundCards;   // keep the General Sound card the config fits
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelController* ttdm = nullptr;

    void SetUp() override
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator(kGsCapableModel, LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr) << "ATM710 not provisionable in this build";
        context = emulator->GetContext();
        ttdm = context->pTimeTravelController;
        ASSERT_NE(ttdm, nullptr);
        FeatureManager* fm = emulator->GetFeatureManager();
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
    }

    void TearDown() override
    {
        if (emulator)
            EmulatorTestHelper::CleanupEmulator(emulator);
    }

    /// Record a few frames and return the session as a .ttd byte string.
    std::string RecordSession()
    {
        EXPECT_TRUE(ttdm->StartRecording());
        for (int i = 0; i < 3; ++i)
            emulator->RunNFrames(1, true);
        std::ostringstream out(std::ios::binary);
        std::string err;
        EXPECT_TRUE(ttdm->SerializeSession(out, err)) << err;
        return out.str();
    }

    /// The recorded devices: the engine's device table, as the session file's facts state them
    uint64_t BaselineMask() const
    {
        uint64_t mask = 0;
        for (const ttd::TTDDeviceEntry& device : ttdm->GetEngine().Devices().Entries())
            if (const uint8_t id = static_cast<uint8_t>(device.descriptor.legacyId); id < 64)
                mask |= uint64_t(1) << id;
        return mask;
    }
};

TEST_F(TTDFileInfo_Test, HeaderMaskAndFieldsMatchTheRecording)
{
    const std::string bytes = RecordSession();
    const ttd::TTDSessionInfo session = ttdm->GetSessionInfo();

    ttd::TTDFileInfo info;
    std::string err;
    std::istringstream in(bytes, std::ios::binary);
    ASSERT_TRUE(ttd::ReadTTDFileInfo(in, info, err)) << err;

    EXPECT_EQ(info.schemaVersion, ttd::kContainerSchema) << "the engine's session file";
    EXPECT_EQ(info.machine.peripheralMask, BaselineMask());
    EXPECT_NE(info.machine.peripheralMask, 0u) << "every model fits at least the Kempston mouse";
    EXPECT_EQ(info.machine.modelId, static_cast<uint8_t>(context->config.mem_model));
    EXPECT_EQ(info.machine.model, "ATM710");
    EXPECT_EQ(info.machine.ramPageBound, session.modelRamPages);
    EXPECT_EQ(info.startFrame, session.sessionStartFrame);
    EXPECT_EQ(info.endFrame, session.currentEndFrame);
    EXPECT_EQ(info.checkpointCount, session.checkpointCount);
    EXPECT_TRUE(info.hasInputJournal);
    EXPECT_TRUE(info.hasExternalEvents);
    EXPECT_EQ(info.machine.peripherals.size(), static_cast<size_t>(std::popcount(info.machine.peripheralMask)));
}

TEST_F(TTDFileInfo_Test, SessionMachineIsTheSameLiveInTheFileAndLoaded)
{
    const std::string bytes = RecordSession();
    const ttd::TTDRecordedMachine live = ttdm->GetSessionInfo().machine;
    EXPECT_EQ(live.peripheralMask, BaselineMask());
    EXPECT_TRUE(ttdm->GetSessionInfo().recordedBy.empty());

    ttd::TTDFileInfo info;
    std::string err;
    std::istringstream in(bytes, std::ios::binary);
    ASSERT_TRUE(ttd::ReadTTDFileInfo(in, info, err)) << err;
    EXPECT_EQ(info.machine.peripheralMask, live.peripheralMask);
    EXPECT_EQ(info.machine.romSignature, live.romSignature);
    EXPECT_EQ(info.machine.model, live.model);

    ttdm->StopRecording();   // a load replaces the session: the engine refuses one while recording
    std::istringstream load(bytes, std::ios::binary);
    ASSERT_TRUE(ttdm->DeserializeSession(load, err)) << err;
    const ttd::TTDSessionInfo loaded = ttdm->GetSessionInfo();
    EXPECT_TRUE(loaded.loadedFromFile);
    EXPECT_EQ(loaded.machine.peripheralMask, live.peripheralMask);
    EXPECT_EQ(loaded.machine.romSignature, info.machine.romSignature);
    EXPECT_EQ(loaded.machine.generalSound, info.machine.generalSound);
    EXPECT_EQ(loaded.recordedBy, info.emulatorId);
}

TEST_F(TTDFileInfo_Test, GeneralSoundCardOfTheRecordingIsNamed)
{
    SoundManager* sm = context->pSoundManager;
    ASSERT_NE(sm, nullptr);
    ASSERT_TRUE(FitGeneralSoundCard(sm, GSTypeKind::Z80));
    const std::string bytes = RecordSession();

    ttd::TTDFileInfo info;
    std::string err;
    std::istringstream in(bytes, std::ios::binary);
    ASSERT_TRUE(ttd::ReadTTDFileInfo(in, info, err)) << err;
    EXPECT_EQ(info.machine.generalSound, GSTypeKind::Z80);
    EXPECT_STREQ(ttd::GeneralSoundName(info.machine.generalSound), "z80");
    bool listed = false;
    for (const std::string& name : info.machine.peripherals)
        listed |= name == "gs";
    EXPECT_TRUE(listed);
}

TEST(TTDFileInfo_Names, GeneralSoundOfAMask)
{
    using ttd::PeripheralId;
    auto bit = [](PeripheralId id) { return uint64_t(1) << static_cast<uint8_t>(id); };
    EXPECT_EQ(ttd::GeneralSoundOf(0), GSTypeKind::NONE);
    EXPECT_EQ(ttd::GeneralSoundOf(bit(PeripheralId::GeneralSound)), GSTypeKind::Z80);
    EXPECT_EQ(ttd::GeneralSoundOf(bit(PeripheralId::GeneralSoundLightweight)), GSTypeKind::LW);
    EXPECT_EQ(ttd::GeneralSoundOf(bit(PeripheralId::NeoGS) | bit(PeripheralId::BetaDisk)), GSTypeKind::NGS);
    EXPECT_EQ(ttd::PeripheralIdName(static_cast<uint8_t>(PeripheralId::BetaDisk)), "betadisk");
    EXPECT_EQ(ttd::PeripheralIdName(60), "multisound-gs");
    EXPECT_EQ(ttd::PeripheralIdName(63), "id63");

    ttd::TTDRecordedMachine m;
    m.peripheralMask = bit(PeripheralId::TSFM) | bit(PeripheralId::NeoGS);
    ttd::DescribeRecordedMachine(m);
    EXPECT_EQ(m.turboSound, "tsfm");
    EXPECT_EQ(m.generalSound, GSTypeKind::NGS);
    ASSERT_EQ(m.peripherals.size(), 2u);
    EXPECT_EQ(m.peripherals[0], "tsfm");
    EXPECT_EQ(m.peripherals[1], "neogs");
}

TEST(TTDFileInfo_Refusal, NotATtdFile)
{
    ttd::TTDFileInfo info;
    std::string err;
    std::istringstream junk(std::string("NOPE and more bytes"), std::ios::binary);
    EXPECT_FALSE(ttd::ReadTTDFileInfo(junk, info, err));
    EXPECT_NE(err.find("not a .ttd"), std::string::npos) << err;

    std::istringstream truncated(std::string("TTDD\x01\x00", 6), std::ios::binary);
    err.clear();
    EXPECT_FALSE(ttd::ReadTTDFileInfo(truncated, info, err));
    EXPECT_NE(err.find("truncated"), std::string::npos) << err;

    err.clear();
    EXPECT_FALSE(ttd::ReadTTDFileInfo(std::string("/nonexistent/dir/none.ttd"), info, err));
    EXPECT_NE(err.find("cannot open"), std::string::npos) << err;
}

TEST_F(TTDFileInfo_Test, ReadsFromAPath)
{
    const std::string bytes = RecordSession();
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("fileinfo.ttd");
    {
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    ttd::TTDFileInfo info;
    std::string err;
    ASSERT_TRUE(ttd::ReadTTDFileInfo(path, info, err)) << err;
    EXPECT_EQ(info.path, path);
    EXPECT_EQ(info.fileBytes, bytes.size());
    EXPECT_EQ(info.machine.model, "ATM710");
    std::remove(path.c_str());
}
