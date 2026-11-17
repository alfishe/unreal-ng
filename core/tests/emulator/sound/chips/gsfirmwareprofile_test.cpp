// The built-in General Sound firmware profiles against the shipped ROMs (gs-debugger design §6.1, F2, L4)

#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "debugger/labels/labelmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"

namespace
{

struct FirmwareCard
{
    EmulatorContext ctx{LoggerLevel::LogError};
    std::unique_ptr<SoundChip_GeneralSound> chip;
    std::unique_ptr<GSCardTarget> target;

    explicit FirmwareCard(const std::string& rom)
    {
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
        chip->loadROM(rom.c_str());
        target = std::make_unique<GSCardTarget>(&ctx, [this]() -> GeneralSoundCard* { return chip.get(); });
    }
    ~FirmwareCard() { target.reset(); }

    LabelManager& labels() { return *target->GetLabelManager(); }
};

/// The handler a ROM's tables give a command: COMTAB words for #00-#1F and #F0-#FF, COMTABH bytes for #20-#EF
uint16_t Handler(const GeneralSoundCard& card, const GSFirmwareProfile& profile, int code)
{
    const uint8_t* rom0 = card.cardRomPage(0);
    const uint8_t* rom1 = card.cardRomPage(1);
    if (code < 0x20 || code >= 0xF0)
    {
        const int index = code < 0x20 ? code : code - 0xD0;
        const uint8_t* entry = rom0 + profile.comtab + 2 * index;
        return uint16_t(entry[0] | (entry[1] << 8));
    }
    const uint8_t* low = rom1 + (profile.comtabh - 0xC000) + (code - 0x20);
    return uint16_t(low[0] | (low[0x100] << 8));
}

} // namespace

/// Every built-in profile is the firmware of a shipped ROM: the card's digest selects it and its symbols are loaded
TEST(GSFirmwareProfile_Test, EachShippedRomSelectsItsProfile)
{
    ASSERT_EQ(GSFirmwareProfiles().size(), 3u);
    for (const GSFirmwareProfile* profile : GSFirmwareProfiles())
    {
        SCOPED_TRACE(profile->id);
        FirmwareCard card(std::string("rom/") + profile->id + ".rom");
        const GSFirmwareIdentity identity = IdentifyGSFirmware(*card.chip);
        ASSERT_TRUE(identity.hasRom);
        EXPECT_EQ(identity.sha256, profile->sha256);
        EXPECT_EQ(identity.sha256Pages[0], profile->sha256Pages[0]);
        EXPECT_EQ(identity.sha256Pages[1], profile->sha256Pages[1]);
        EXPECT_EQ(identity.profile, profile);

        const std::vector<std::string> applied = card.target->ApplySymbolBundles();
        ASSERT_EQ(applied.size(), 1u);
        EXPECT_EQ(applied[0], std::string("bundle:gs:") + profile->id);
    }
    EXPECT_EQ(FindGSFirmwareProfile(std::string(64, '0')), nullptr);
}

/// The self-check: the ROM's own command tables against the profile. A handler that is not the "unknown command"
/// stub (COMZ, COMHZ) has a named command, a stub has none, and every handler has a label
TEST(GSFirmwareProfile_Test, EveryHandlerInTheRomTablesHasANamedCommand)
{
    for (const GSFirmwareProfile* profile : GSFirmwareProfiles())
    {
        SCOPED_TRACE(profile->id);
        FirmwareCard card(std::string("rom/") + profile->id + ".rom");
        card.target->ApplySymbolBundles();
        LabelManager& labels = card.labels();
        const auto comz = labels.GetLabelByName("COMZ");
        const auto comhz = labels.GetLabelByName("COMHZ");
        const auto comtab = labels.GetLabelByName("COMTAB");
        const auto comtabh = labels.GetLabelByName("COMTABH");
        ASSERT_TRUE(comz && comhz && comtab && comtabh);
        EXPECT_EQ(comtab->address, profile->comtab);
        EXPECT_EQ(comtabh->address, profile->comtabh);

        int named = 0;
        for (int code = 0; code < 256; code++)
        {
            const uint16_t handler = Handler(*card.chip, *profile, code);
            const bool stub = handler == comz->address || handler == comhz->address;
            const GSCommandInfo& command = profile->Command(uint8_t(code));
            EXPECT_EQ(command.name != nullptr, !stub) << "command #" << std::hex << code << " handler #" << handler;
            EXPECT_NE(labels.GetLabelByZ80Address(handler), nullptr) << "command #" << std::hex << code;
            if (command.name)
            {
                named++;
                EXPECT_LE(command.pre, 1);
                const int sent = command.pre + (command.post == GSCommandInfo::kVariablePost ? 0 : command.post);
                for (int i = 0; i < sent && i < 4; i++)
                    EXPECT_NE(command.params[i].name, nullptr) << "command #" << std::hex << code << " param " << i;
            }
        }
        EXPECT_EQ(named, std::string(profile->id) == "gs104" ? 129 : 131);
    }
}

/// The variables are where the firmware's labels put them; EQU ones (no label) lie in the fixed RAM window
TEST(GSFirmwareProfile_Test, TheVariablesAreAtTheirLabels)
{
    for (const GSFirmwareProfile* profile : GSFirmwareProfiles())
    {
        SCOPED_TRACE(profile->id);
        FirmwareCard card(std::string("rom/") + profile->id + ".rom");
        card.target->ApplySymbolBundles();
        for (size_t i = 0; i < profile->variableCount; i++)
        {
            const GSVariableInfo& variable = profile->variables[i];
            EXPECT_GE(variable.address, 0x4000);
            EXPECT_LT(variable.address + variable.size, 0x8000);
            if (variable.equ)
                continue;
            // One label per address: a name that shares its address (MEMTOP / TSMPH at #40E2) yields to the other
            const auto label = card.labels().GetLabelByName(variable.name);
            if (label)
                EXPECT_EQ(label->address, variable.address) << variable.name;
            else
                EXPECT_NE(card.labels().GetLabelByZ80Address(variable.address), nullptr) << variable.name;
        }
        EXPECT_EQ(profile->Variable("BUSY")->address, 0x4086);
        EXPECT_EQ(profile->Variable("nothing"), nullptr);
    }
    // 1.05's relooper variables follow the module type byte (patch.a80: PlMode EQU MODTP+1, MODLLEN EQU PlMode+1)
    const GSFirmwareProfile* gs105a = FindGSFirmwareProfile(GSFirmwareProfiles()[1]->sha256);
    EXPECT_EQ(gs105a->Variable("PlMode")->address, gs105a->Variable("MODTP")->address + 1);
    EXPECT_EQ(gs105a->Variable("MODLLEN")->address, gs105a->Variable("PlMode")->address + 1);
}

/// The commands the guide omits are named from the sources; the 1.05 patch's commands exist only in 1.05
TEST(GSFirmwareProfile_Test, TheSourcesNameWhatTheGuideOmits)
{
    const GSFirmwareProfile* gs104 = GSFirmwareProfiles()[0];
    const GSFirmwareProfile* gs105a = GSFirmwareProfiles()[1];
    EXPECT_STREQ(gs105a->Command(0x0E).name, "LPT Covox mode");
    EXPECT_EQ(gs105a->Command(0x0E).transfer, GSTransferKind::Stream);
    EXPECT_STREQ(gs105a->Command(0x13).name, "Jump to address");
    EXPECT_STREQ(gs105a->Command(0x16).name, "Put byte");
    EXPECT_EQ(gs105a->Command(0x16).pre + gs105a->Command(0x16).post, 3);
    EXPECT_EQ(gs105a->Command(0x30).transfer, GSTransferKind::ModuleUpload);
    EXPECT_STREQ(gs105a->Command(0x9A).name, "Play FX with note and volume in channel 2");
    EXPECT_EQ(gs105a->Command(0x9A).post, 2);
    EXPECT_STREQ(gs105a->Command(0xAB).name, "Change volume in channel 3");
    EXPECT_STREQ(gs105a->Command(0x6A).name, "Set player mode");
    EXPECT_EQ(gs104->Command(0x6A).name, nullptr);
    EXPECT_EQ(gs104->Command(0x6B).name, nullptr);
    EXPECT_EQ(gs105a->Command(0x08).name, nullptr);
}
