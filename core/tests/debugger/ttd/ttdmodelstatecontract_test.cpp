#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
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
/// working normally in every other respect.
class LyingDecoder : public PortDecoder_Pentagon128
{
public:
    using PortDecoder_Pentagon128::PortDecoder_Pentagon128;

    std::vector<ttd::PeripheralId> GetTTDModelStateIds() const override
    {
        return {ttd::PeripheralId::GeneralSound};
    }
    // CreateTTDSerializers() deliberately left as the empty base implementation.
};
}  // namespace

/// Every model must cover each id it declares. This walks the real decoder of a
/// real machine, so adding a model that declares state without implementing it
/// fails here rather than at someone's seek months later.
TEST(TTDModelStateContract_Test, DeclaredStateIsCoveredBySerializers)
{
    for (const char* model : {"48K", "128k", "PENTAGON", "SCORPION", "PROFSCORP"})
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

/// PROFSCORP must actually declare its ProfROM state - if this regresses, the
/// plane/page latches go uncaptured and a seek lands on the wrong ROM page.
TEST(TTDModelStateContract_Test, ProfScorpDeclaresProfRomState)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PROFSCORP", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    const auto declared = context->pPortDecoder->GetTTDModelStateIds();

    EXPECT_NE(std::find(declared.begin(), declared.end(), ttd::PeripheralId::ScorpionProfROM),
              declared.end())
        << "PROFSCORP must declare ScorpionProfROM state";

    EmulatorTestHelper::CleanupEmulator(emulator);
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

    PortDecoder* original = context->pPortDecoder;
    LyingDecoder lying(context);
    context->pPortDecoder = &lying;

    // Exercised through the public entry point: refusing to record is the
    // behaviour that matters, not the private helper that decides it.
    const bool started = context->pTimeTravelManager->StartRecording();

    context->pPortDecoder = original;  // restore before anything else touches it

    EXPECT_FALSE(started) << "a model declaring uncovered state must not record";
    EXPECT_NE(context->pTimeTravelManager->GetState(), ttd::TTDSessionState::Recording);

    EmulatorTestHelper::CleanupEmulator(emulator);
}
