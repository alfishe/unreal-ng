#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"

/// @file ttdmodelstatecontract_test.cpp
/// @brief The contract between a model and TTD (parent TDD 6.4).
///
/// TTDChipsetState carries only the standard Spectrum 128K ports, so any latch
/// beyond those is captured only if the model's port decoder declares it and
/// supplies a serializer. A model that declares nothing and has extra state
/// loses it silently on restore - the failure mode these tests exist to make
/// impossible to reach unnoticed.

namespace
{
/// A decoder that declares model state it does not implement - the mistake the
/// guard exists to catch. Derives from a real decoder so the machine keeps
/// working normally in every other respect. Declares NeoGS, an id only a
/// fitted NeoGS card registers (GSType=NGS - never on this Pentagon): the
/// model declares it, nothing covers it, which is exactly the mistake the
/// guard exists to catch.
class LyingDecoder : public PortDecoder_Pentagon128
{
public:
    using PortDecoder_Pentagon128::PortDecoder_Pentagon128;

    std::vector<ttd::PeripheralId> GetTTDModelStateIds() const override
    {
        return {ttd::PeripheralId::NeoGS};
    }
    // CreateTTDSerializers() deliberately left as the empty base implementation.
};
}  // namespace

/// Every model must cover each id it declares. This walks the real decoder of a
/// real machine, so adding a model that declares state without implementing it
/// fails here rather than at someone's seek months later.
TEST(TTDModelStateContract_Test, DeclaredStateIsCoveredBySerializers)
{
    for (const char* model : {"48K", "128k", "PENTAGON", "SCORPION", "PROFSCORP", "ATM710", "ATM3", "PROFI", "SPRINTER"})
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        if (emulator == nullptr)
            continue;  // model not provisionable in this build - not a contract failure

        EmulatorContext* context = emulator->GetContext();
        ASSERT_NE(context, nullptr) << model;
        ASSERT_NE(context->pPortDecoder, nullptr) << model;

        const auto declared = context->pPortDecoder->GetTTDModelStateIds();
        const auto serializers = context->pPortDecoder->CreateTTDSerializers();

        std::vector<ttd::PeripheralId> provided;
        for (const auto& s : serializers)
        {
            ASSERT_NE(s, nullptr) << model << ": CreateTTDSerializers returned a null entry";
            provided.push_back(s->TTDPeripheralId());
        }

        for (ttd::PeripheralId id : declared)
        {
            EXPECT_NE(std::find(provided.begin(), provided.end(), id), provided.end())
                << model << " declares TTD state id " << static_cast<unsigned>(id)
                << " but supplies no serializer for it";
        }

        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

/// Both Scorpion variants must declare their model state: #1FFD and the magic
/// button trigger drive the paging chain on each, the ProfROM plane on
/// PROFSCORP. If this regresses, a seek lands on the wrong ROM / RAM page.
TEST(TTDModelStateContract_Test, ScorpionModelsDeclareScorpionState)
{
    for (const char* model : {"SCORPION", "PROFSCORP"})
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr) << model;

        EmulatorContext* context = emulator->GetContext();
        const auto declared = context->pPortDecoder->GetTTDModelStateIds();

        EXPECT_NE(std::find(declared.begin(), declared.end(), ttd::PeripheralId::ScorpionProfROM),
                  declared.end())
            << model << " must declare ScorpionProfROM state (#1FFD, DOS trigger, plane)";

        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

/// A machine described entirely by the standard 128K ports declares nothing,
/// and must still record normally - the guard must not become a tax on the
/// models that are already correct.
TEST(TTDModelStateContract_Test, StandardModelDeclaresNothingAndStillRecords)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    EXPECT_TRUE(context->pPortDecoder->GetTTDModelStateIds().empty());

    FeatureManager* fm = emulator->GetFeatureManager();
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kTimeTravel, true);

    EXPECT_TRUE(context->pTimeTravelManager->StartRecording());
    context->pTimeTravelManager->StopRecording();

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The guard itself: declaring state without implementing it must refuse the
/// recording outright, naming what is missing. Recording anyway would produce a
/// session that looks valid and restores wrong.
TEST(TTDModelStateContract_Test, DeclaredButUnimplementedStateRefusesRecording)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    FeatureManager* fm = emulator->GetFeatureManager();
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kTimeTravel, true);

    // Scoped: the decoder's destructor logs through the context's logger, so it
    // must be gone before CleanupEmulator frees the context
    bool started = false;
    {
        PortDecoder* original = context->pPortDecoder;
        LyingDecoder lying(context);
        context->pPortDecoder = &lying;

        // Exercised through the public entry point: refusing to record is the
        // behaviour that matters, not the private helper that decides it.
        started = context->pTimeTravelManager->StartRecording();

        context->pPortDecoder = original;  // restore before anything else touches it
    }

    EXPECT_FALSE(started) << "a model declaring uncovered state must not record";
    EXPECT_NE(context->pTimeTravelManager->GetState(), ttd::TTDSessionState::Recording);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A refused recording leaves nothing behind: debugmode / timetravel, switched
/// on by StartRecording for the capture, go back off.
TEST(TTDModelStateContract_Test, RefusedRecordingRollsBackTheFlagsItSwitchedOn)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    FeatureManager* fm = emulator->GetFeatureManager();
    fm->setFeature(Features::kDebugMode, false);
    fm->setFeature(Features::kTimeTravel, false);

    bool started = false;
    {
        PortDecoder* original = context->pPortDecoder;
        LyingDecoder lying(context);
        context->pPortDecoder = &lying;
        started = context->pTimeTravelManager->StartRecording();
        context->pPortDecoder = original;
    }

    ASSERT_FALSE(started);
    EXPECT_FALSE(fm->isEnabled(Features::kDebugMode));
    EXPECT_FALSE(fm->isEnabled(Features::kTimeTravel));

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The PeripheralId table is part of the .ttd format: every id keeps its
/// number, and ttd.ksy documents each one as "<number> <Name>" (TSConf
/// INF-10; the unique-table rule of PLAN #40 Phase 0, Step 1). A new id appends here and
/// in ttd.ksy in the same change.
TEST(TTDPeripheralIdTable_Test, NumbersAreStableAndDocumentedInTheFormat)
{
    struct Row
    {
        ttd::PeripheralId id;
        uint8_t number;
        const char* name;
    };
    const Row rows[] = {
        {ttd::PeripheralId::TurboSound, 0, "TurboSound"},
        {ttd::PeripheralId::BetaDisk, 1, "BetaDisk"},
        {ttd::PeripheralId::Tape, 2, "Tape"},
        {ttd::PeripheralId::Covox, 3, "Covox"},
        {ttd::PeripheralId::TSFM, 4, "TSFM"},
        {ttd::PeripheralId::GeneralSound, 5, "GeneralSound"},
        {ttd::PeripheralId::ScorpionProfROM, 6, "ScorpionProfROM"},
        {ttd::PeripheralId::KempstonMouse, 7, "KempstonMouse"},
        {ttd::PeripheralId::AtmPaging, 8, "AtmPaging"},
        {ttd::PeripheralId::ProfiPaging, 9, "ProfiPaging"},
        {ttd::PeripheralId::MoonSound, 10, "MoonSound"},
        {ttd::PeripheralId::GeneralSoundLightweight, 11, "GeneralSoundLightweight"},
        {ttd::PeripheralId::NeoGS, 12, "NeoGS"},
        {ttd::PeripheralId::Plus3Paging, 13, "Plus3Paging"},
        {ttd::PeripheralId::Upd765, 14, "Upd765"},
        {ttd::PeripheralId::EvoSdCard, 15, "EvoSdCard"},
        {ttd::PeripheralId::TsConfPaging, 16, "TsConfPaging"},
        {ttd::PeripheralId::AtaChannel, 17, "AtaChannel"},
        {ttd::PeripheralId::Ds12887, 18, "Ds12887"},
        {ttd::PeripheralId::EvoPs2, 19, "EvoPs2"},
        {ttd::PeripheralId::ZxNetUsb, 20, "ZxNetUsb"},
        {ttd::PeripheralId::EvoTurboCache, 21, "EvoTurboCache"},
        {ttd::PeripheralId::EvoFontRam, 22, "EvoFontRam"},
        {ttd::PeripheralId::KempstonJoystick, 23, "KempstonJoystick"},
        {ttd::PeripheralId::SerialPort, 24, "SerialPort"},
        {ttd::PeripheralId::SprinterPld, 25, "SprinterPld"},
        {ttd::PeripheralId::Atm2Kbc, 26, "Atm2Kbc"},
        {ttd::PeripheralId::MachineSerialPeer, 27, "MachineSerialPeer"},
        {ttd::PeripheralId::SprinterVideoRam, 28, "SprinterVideoRam"},
        {ttd::PeripheralId::Z84C15, 29, "Z84C15"},
        {ttd::PeripheralId::SprinterFastRam, 30, "SprinterFastRam"},
        {ttd::PeripheralId::SprinterInput, 31, "SprinterInput"},
        {ttd::PeripheralId::SprinterCovoxBlaster, 32, "SprinterCovoxBlaster"},
        {ttd::PeripheralId::SprinterIsa, 33, "SprinterIsa"},
        {ttd::PeripheralId::SprinterPads, 34, "SprinterPads"},
        {ttd::PeripheralId::Wd1793Context, 35, "Wd1793Context"},
        {ttd::PeripheralId::AtmIoBus, 36, "AtmIoBus"},
        {ttd::PeripheralId::Atm2IoEsp, 37, "Atm2IoEsp"},
        {ttd::PeripheralId::EvoMouse, 38, "EvoMouse"},
        {ttd::PeripheralId::ZiFiLine, 39, "ZiFiLine"},
        {ttd::PeripheralId::ZiFi, 40, "ZiFi"},
        {ttd::PeripheralId::CdDrive, 41, "CdDrive"},
        {ttd::PeripheralId::Vdac2Memory, 42, "Vdac2Memory"},
        {ttd::PeripheralId::Vdac2, 43, "Vdac2"},
        {ttd::PeripheralId::Smuc, 44, "Smuc"},
        {ttd::PeripheralId::EvoAvrVolatile, 45, "EvoAvrVolatile"},
        {ttd::PeripheralId::KeyboardMatrix, 46, "KeyboardMatrix"},
        {ttd::PeripheralId::RzxPlayback, 47, "RzxPlayback"},
    };
    EXPECT_EQ(static_cast<size_t>(ttd::PeripheralId::Count), std::size(rows)) << "a new id needs a row here and in ttd.ksy";

    const std::string ksyPath = (TestPathHelper::FindProjectRoot() / "core/src/debugger/ttd/ttd.ksy").string();
    std::ifstream in(ksyPath, std::ios::binary);
    ASSERT_TRUE(in.good()) << ksyPath;
    std::string ksy((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // The id list is prose wrapped over lines: compare with single spaces
    std::string flat;
    for (char c : ksy)
    {
        const char s = (c == '\n' || c == '\r') ? ' ' : c;
        if (s == ' ' && !flat.empty() && flat.back() == ' ')
            continue;
        flat += s;
    }

    for (const Row& row : rows)
    {
        EXPECT_EQ(static_cast<uint8_t>(row.id), row.number) << row.name;
        const std::string entry = std::to_string(row.number) + " " + row.name;
        EXPECT_NE(flat.find(entry), std::string::npos) << "ttd.ksy does not document \"" << entry << "\"";
    }
}

/// Phase 2, Step 4: every device of every creatable model, with every sound
/// card fitted, matches its own descriptor, and the engine's device table
/// builds from them (dependencies present, time fields inside the state).
/// Creates 32 machine configurations (no ROM boot): above the 50 ms guideline,
/// one contract check over every model
TEST(TTDModelStateContract_Test, EveryDeviceMatchesItsDescriptorOnEveryModel)
{
    SoundCardScope cards{TestSound::All};
    size_t checked = 0;
    for (const char* model : {"48K", "128k", "PLUS2", "PLUS2A", "PLUS3", "PENTAGON", "SCORPION", "PROFSCORP", "ATM710",
                              "ATM450", "ATM3", "PROFI", "PROFI3", "TSL", "TSL-VDAC2", "SPRINTER"})
    {
        for (GSTypeKind gs : {GSTypeKind::Z80, GSTypeKind::NGS})
        {
            SCOPED_TRACE(std::string(model) + (gs == GSTypeKind::NGS ? " + NeoGS" : " + GS"));
            // TSFM in the TurboSound slot where the helper can stage it (it
            // finds no config folder for the ATM models and machine variants:
            // those keep their own slot)
            Emulator* emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind(model, TurboSoundKind::FM);
            if (emulator == nullptr)
                emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
            if (emulator == nullptr)
            {
                ADD_FAILURE() << "cannot create " << model;
                continue;
            }
            EmulatorContext* context = emulator->GetContext();
            ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, gs));

            ttd::TTDPeripheralRegistry registry;
            std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
            std::string error;
            EXPECT_TRUE(ttd::RegisterMachinePeripherals(context, registry, owned, &error)) << error;
            std::string tableError;
            EXPECT_TRUE(registry.CheckDeviceTable(tableError)) << tableError;
            EXPECT_EQ(registry.DeviceEntries().size(), registry.Count());
            ++checked;
            EmulatorTestHelper::CleanupEmulator(emulator);
        }
    }
    EXPECT_EQ(checked, 32u) << "every model is creatable in this build";
}

namespace
{
struct ContractDevice : ttd::TTDSerializable
{
    ttd::PeripheralId id = ttd::PeripheralId::Covox;
    ttd::TTDDeviceDescriptor extra;
    bool describeOther = false;
    size_t TTDStateSize() const override { return 8; }
    void TTDSaveState(uint8_t* dst) const override { std::memset(dst, 0, 8); }
    void TTDLoadState(const uint8_t*) override {}
    std::string TTDDeviceName() const override { return "Contract"; }
    ttd::PeripheralId TTDPeripheralId() const override { return id; }
    ttd::TTDDeviceDescriptor TTDDescribe() const override
    {
        ttd::TTDDeviceDescriptor d = ttd::TTDSerializable::TTDDescribe();
        d.timeFields = extra.timeFields;
        d.restoreAfter = extra.restoreAfter;
        if (describeOther)
            d.stateSize = 16;
        return d;
    }
};
}  // namespace

/// The registration check names what does not match: a device registered
/// under another id, a descriptor whose size is not the device's, a time field
/// outside the state, a dependency on a device the machine lacks
TEST(TTDModelStateContract_Test, ADeviceNotMatchingItsDescriptorIsRefusedByName)
{
    std::string error;
    {
        ContractDevice d;
        ttd::TTDPeripheralRegistry registry;
        registry.Register(ttd::PeripheralId::Covox, &d);
        EXPECT_TRUE(registry.CheckDeviceTable(error)) << error;
    }
    {
        ContractDevice d;
        ttd::TTDPeripheralRegistry registry;
        registry.Register(ttd::PeripheralId::Tape, &d);
        EXPECT_FALSE(registry.CheckDeviceTable(error));
        EXPECT_NE(error.find("Contract"), std::string::npos) << error;
    }
    {
        ContractDevice d;
        d.describeOther = true;
        ttd::TTDPeripheralRegistry registry;
        registry.Register(ttd::PeripheralId::Covox, &d);
        EXPECT_FALSE(registry.CheckDeviceTable(error));
        EXPECT_NE(error.find("16 bytes"), std::string::npos) << error;
    }
    {
        ContractDevice d;
        d.extra.timeFields = {{4, 8}};
        ttd::TTDPeripheralRegistry registry;
        registry.Register(ttd::PeripheralId::Covox, &d);
        EXPECT_FALSE(registry.CheckDeviceTable(error));
        EXPECT_NE(error.find("time field"), std::string::npos) << error;
    }
    {
        ContractDevice d;
        d.extra.restoreAfter = {{ttd::TTDDeviceType::BetaDisk, "betadisk"}};
        ttd::TTDPeripheralRegistry registry;
        registry.Register(ttd::PeripheralId::Covox, &d);
        EXPECT_FALSE(registry.CheckDeviceTable(error));
        EXPECT_NE(error.find("betadisk"), std::string::npos) << error;
    }
}
