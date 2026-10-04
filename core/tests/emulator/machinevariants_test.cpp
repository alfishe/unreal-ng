// Machine variants (machinevariants.h): a base model with a fixed board, created
// and reported by name on every surface.

#include <gtest/gtest.h>

#include <string>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/machinevariants.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

TEST(MachineVariants_Test, FindsByNameAndAliasIgnoringCase)
{
    const MachineVariant* variant = MachineVariants::Find("TSL-VDAC2");
    ASSERT_NE(variant, nullptr);
    EXPECT_STREQ(variant->baseModel, "TSL");
    EXPECT_EQ(MachineVariants::Find("tsl-vdac2"), variant);
    EXPECT_EQ(MachineVariants::Find("TSCONF-VDAC2"), variant);
    EXPECT_EQ(MachineVariants::Find("TSL"), nullptr) << "a plain model is not a variant";

    CONFIG config{};
    config.mem_model = MM_TSL;
    EXPECT_EQ(MachineVariants::Of(config), nullptr);
    variant->apply(config);
    EXPECT_EQ(config.ts_vdac, 7);
    EXPECT_EQ(config.ide_scheme, IDE_NONE);
    EXPECT_EQ(MachineVariants::Of(config), variant) << "a machine with that board is reported as the variant";
}

TEST(MachineVariants_Test, TsConfVdac2IsCreatedByName)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
#ifdef ENABLE_VDAC2
    auto emulator = manager->CreateEmulatorWithModel("variant-test", "TSL-VDAC2", LoggerLevel::LogError, &error);
    ASSERT_NE(emulator, nullptr) << error;
    EmulatorContext* context = emulator->GetContext();
    EXPECT_EQ(context->config.mem_model, MM_TSL);
    EXPECT_EQ(context->config.ts_vdac, 7);
    EXPECT_EQ(context->config.ide_scheme, IDE_NONE);
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    EXPECT_NE(decoder->GetVdac2Card(), nullptr) << "the card is fitted";
    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
    EXPECT_EQ(identity.Model, "TSL");
    EXPECT_EQ(identity.Variant, "TSL-VDAC2");
    manager->RemoveEmulator(emulator->GetUUID());

    // The variant's RAM size is fixed
    EXPECT_EQ(manager->CreateEmulatorWithModelAndRAM("variant-test", "TSL-VDAC2", 1024, LoggerLevel::LogError, &error),
              nullptr);
    EXPECT_NE(error.find("4096"), std::string::npos) << error;
#else
    EXPECT_EQ(manager->CreateEmulatorWithModel("variant-test", "TSL-VDAC2", LoggerLevel::LogError, &error), nullptr);
    EXPECT_NE(error.find("ENABLE_VDAC2"), std::string::npos) << error;
#endif
}

/// docs/inprogress/2026-10-04-profi-plus: PROFI-PLUS is a v5 with the V0.03 decoder (ExtPorts=v003) running ROM BIOS
/// Plus 0.41h1, created and reported by name
TEST(MachineVariants_Test, ProfiPlusIsCreatedByName)
{
    const MachineVariant* variant = MachineVariants::Find("profi-plus");
    ASSERT_NE(variant, nullptr);
    EXPECT_EQ(MachineVariants::Find("PROFIPLUS"), variant);
    EXPECT_STREQ(variant->baseModel, "PROFI");

    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
    auto emulator = manager->CreateEmulatorWithModel("variant-profi-plus", "PROFI-PLUS", LoggerLevel::LogError, &error);
    ASSERT_NE(emulator, nullptr) << error;
    EmulatorContext* context = emulator->GetContext();
    EXPECT_EQ(context->config.mem_model, MM_PROFI);
    EXPECT_EQ(context->config.profi_ext_ports, 2);
    EXPECT_NE(std::string(context->config.profi_rom_path).find("bios-plus-041h1"), std::string::npos);
    // The SYS page is BIOS Plus: its banner sits in the first page
    const uint8_t* sys = context->pMemory->ROMPageHostAddress(0);
    const std::string page(reinterpret_cast<const char*>(sys), 0x4000);
    EXPECT_NE(page.find("ROM-BIOS PLUS"), std::string::npos) << "the BIOS Plus image is loaded";
    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
    EXPECT_EQ(identity.Model, "PROFI");
    EXPECT_EQ(identity.Variant, "PROFI-PLUS");
    manager->RemoveEmulator(emulator->GetUUID());
}
