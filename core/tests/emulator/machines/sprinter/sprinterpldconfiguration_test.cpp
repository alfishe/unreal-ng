// PLD configuration modules (Sprinter test-plan §2.3 T-PLDM; tdd-ports-memory
// §6.1, decision D11): the registry, a stub module chosen by its hash, its
// overrides with everything else falling through to Standard, reset and reload.

#include "sprinterfixture.h"

#include "emulator/ports/models/sprinter/sprinterpldconfig.h"
#include "emulator/ports/models/sprinter/sprinterpldstandard.h"

namespace
{
/// A test-only module: overrides one port code and the mapping of window 2,
/// counts what the decoder asks it
class StubModule : public SprinterPldConfiguration
{
public:
    StubModule(uint32_t fullHash, uint32_t headHash)
    {
        _descriptor.name = "Stub";
        _descriptor.fullHash = fullHash;
        _descriptor.headHash = headHash;
    }

    const SprinterPldModuleDescriptor& Descriptor() const override { return _descriptor; }

    bool ReadCode(PortDecoder_Sprinter&, uint8_t code, uint16_t, uint8_t& value) override
    {
        reads++;
        if (code != kOwnCode)
            return false;
        value = 0x5A;
        return true;
    }
    bool WriteCode(PortDecoder_Sprinter&, uint8_t code, uint16_t, uint8_t value) override
    {
        writes++;
        if (code != kOwnCode)
            return false;
        lastWrite = value;
        return true;
    }
    bool UpdateBanks(SprinterMemory& memory, const SprinterPldState& pld) override
    {
        if (!remapWindow2)
            return false;
        memory.StandardUpdateBanks(pld);
        memory.MapRamToBank(2, 0x77, true);
        return true;
    }
    void OnActivate(SprinterPldState&) override { activations++; }
    void OnReset(SprinterResetKind kind, SprinterPldState&) override
    {
        resets++;
        lastReset = kind;
    }

    static constexpr uint8_t kOwnCode = 0x3F;
    int reads = 0;
    int writes = 0;
    int activations = 0;
    int resets = 0;
    uint8_t lastWrite = 0;
    bool remapWindow2 = true;
    SprinterResetKind lastReset = SprinterResetKind::PowerOn;

private:
    SprinterPldModuleDescriptor _descriptor;
};
}  // namespace

class SprinterPldConfiguration_Test : public SprinterFixture
{
protected:
    /// Feed a load whose hashes are those of `value` repeated. A load is 473 720
    /// configuration writes (~15 ms in Release): the tests that load twice run longer
    void LoadStream(uint8_t value)
    {
        _decoder->BeginLoading();
        for (uint32_t i = 0; i < SprinterPldConfig::kPldConfigurationWrites; i++)
            _decoder->OnConfigurationWrite(value);
        _decoder->OnMachineStep(0);
    }

    static void StreamHashes(uint8_t value, uint32_t& full, uint32_t& head)
    {
        SprinterPldState pld{};
        SprinterPldConfig::Begin(pld);
        for (uint32_t i = 0; i < SprinterPldConfig::kPldConfigurationWrites; i++)
            SprinterPldConfig::OnWrite(pld, value);
        full = pld.bitstreamHashFull;
        head = pld.bitstreamHashHead;
    }
};

// T-PLDM-1: lookup by the full hash, by the head hash only, and an unknown stream
TEST_F(SprinterPldConfiguration_Test, Registry_FindsByFullThenHeadHash)
{
    SprinterPldConfigurationRegistry registry;
    ASSERT_EQ(registry.Count(), 2u) << "Standard and Game";
    EXPECT_EQ(registry.Standard().Descriptor().name, "Standard");
    EXPECT_EQ(registry.Find(SprinterPldStandard::kFullHash304, 0), 0);

    const size_t stub = registry.Register(std::make_unique<StubModule>(0x11551111, 0x5EAD0001));
    EXPECT_EQ(registry.Find(0x11551111, 0), static_cast<int>(stub)) << "full hash";
    EXPECT_EQ(registry.Find(0x22222222, 0x5EAD0001), static_cast<int>(stub)) << "head hash only (MAME-compatible)";
    EXPECT_EQ(registry.Find(0x22222222, 0x33333333), -1) << "unknown";
    EXPECT_EQ(registry.FindByName("Stub"), static_cast<int>(stub));
    EXPECT_EQ(registry.FindByName("Game"), static_cast<int>(SprinterPldConfigurationRegistry::kGameIndex));
}

// T-PLDM-2: a stub module chosen by its hash overrides one code and one window;
// everything else is Standard
TEST_F(SprinterPldConfiguration_Test, StubModule_OverridesOnlyWhatItOwns)
{
    uint32_t full = 0, head = 0;
    StreamHashes(0x42, full, head);
    auto module = std::make_unique<StubModule>(full, head);
    StubModule* stub = module.get();
    const size_t index = _decoder->GetRegistry().Register(std::move(module));

    LoadStream(0x42);
    ASSERT_EQ(Pld().configModule, index);
    EXPECT_EQ(stub->activations, 1);
    EXPECT_EQ(&_decoder->ActiveModule(), stub);

    OpenDcp();
    EXPECT_EQ(Tag(0x8000), 0x77) << "the module's mapping";
    EXPECT_EQ(Tag(0x4000), Pld().Cell(0xE9)) << "Standard's window 1";

    SetCode(0x1155, true, StubModule::kOwnCode);
    EXPECT_EQ(In(0x1155), 0x5A) << "the module's own code";
    SetCode(0x1155, false, StubModule::kOwnCode);
    Out(0x1155, 0x9C);
    EXPECT_EQ(stub->lastWrite, 0x9C);

    SetCode(0x2222, false, 0xC4);
    Out(0x2222, 0x21);
    EXPECT_EQ(Pld().portY, 0x21) << "a Standard code still works";
}

// T-PLDM-4: reset and reload with the stub module active
TEST_F(SprinterPldConfiguration_Test, StubModule_ResetAndReload)
{
    uint32_t full = 0, head = 0;
    StreamHashes(0x42, full, head);
    auto module = std::make_unique<StubModule>(full, head);
    StubModule* stub = module.get();
    _decoder->GetRegistry().Register(std::move(module));
    LoadStream(0x42);
    ASSERT_EQ(&_decoder->ActiveModule(), stub);
    const int resetsAfterLoad = stub->resets;
    EXPECT_EQ(stub->lastReset, SprinterResetKind::Configured);

    // Reload with another stream: the lookup runs again and Standard wins
    OpenDcp();
    SetCode(0x40BC, false, 0x2E);
    Out(0x40BC, 0);
    _decoder->OnMachineStep(0);
    EXPECT_EQ(stub->resets, resetsAfterLoad + 1);
    EXPECT_EQ(stub->lastReset, SprinterResetKind::Reload);
    for (uint32_t i = 0; i < SprinterPldConfig::kPldConfigurationWrites; i++)
        _decoder->OnConfigurationWrite(0x13);
    _decoder->OnMachineStep(0);
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Standard");
}

// T-PLDM-5: the decoder reaches the codes through the active module first; a
// module that declines everything leaves the machine exactly Standard
TEST_F(SprinterPldConfiguration_Test, Decoder_AsksTheActiveModuleFirst)
{
    uint32_t full = 0, head = 0;
    StreamHashes(0x42, full, head);
    auto module = std::make_unique<StubModule>(full, head);
    StubModule* stub = module.get();
    stub->remapWindow2 = false;
    _decoder->GetRegistry().Register(std::move(module));
    LoadStream(0x42);

    OpenDcp();
    const int readsBefore = stub->reads;
    SetCode(0x00E2, true, 0xF0);
    EXPECT_EQ(In(0x00E2), Pld().cells[Pld().pg3]);
    EXPECT_EQ(stub->reads, readsBefore + 1);

    const int writesBefore = stub->writes;
    SetCode(0x00A2, false, 0xE9);
    Out(0x00A2, 0x31);
    EXPECT_EQ(stub->writes, writesBefore + 1);
    EXPECT_EQ(Tag(0x4000), 0x31);
    EXPECT_EQ(Tag(0x8000), Pld().Cell(0xEA)) << "Standard's mapping when the module declines";
}
