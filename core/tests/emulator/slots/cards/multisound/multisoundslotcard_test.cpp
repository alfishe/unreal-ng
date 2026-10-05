// MultiSoundSlotCard (core/src/emulator/slots/cards/multisound/multisoundslotcard.h): the ZX-MultiSound in a slot of a
// running machine (docs/inprogress/2026-10-03-zx-multisound/tdd-integration.md MS-4). The machines are created from a
// shipped config with its [SLOTS] section replaced, as a user would fit the card.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/card.h"
#include "emulator/slots/cards/multisound/multisoundslotcard.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/soundmanager.h"

namespace fs = std::filesystem;

namespace
{

/// A shipped config with its [SLOTS] section replaced and extra sections appended, staged in the per-process scratch
/// folder; the machine is created from it with every sound device as configured
class StagedMachine
{
public:
    StagedMachine(const std::string& folder, const std::string& slotsSection, const std::string& extra = {})
    {
        const fs::path source = TestPathHelper::FindProjectRoot() / "data" / "configs" / folder / "unreal.ini";
        std::ifstream in(source, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t shipped = text.find("\n[SLOTS]");
        if (shipped != std::string::npos)
        {
            const size_t next = text.find("\n[", shipped + 1);
            text.erase(shipped, next == std::string::npos ? std::string::npos : next - shipped);
        }
        text += "\n[SLOTS]\n" + slotsSection + "\n" + extra + "\n";
        _path = TestPathHelper::GetUniqueTestScratchPath("ms4-" + folder + ".ini");
        std::ofstream out(_path, std::ios::binary);
        out << text;
        out.close();
        _emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
        _emulator->SetCustomConfigPath(_path.string());
        _ok = _emulator->Init();
    }
    ~StagedMachine()
    {
        _emulator->Release();
        std::error_code ignored;
        fs::remove(_path, ignored);
    }
    bool Ok() const
    {
        return _ok;
    }
    EmulatorContext* Context() const
    {
        return _emulator->GetContext();
    }
    Emulator& Machine()
    {
        return *_emulator;
    }
    /// The slot-built MultiSound in a slot; nullptr when none was built
    MultiSoundSlotCard* Card(const std::string& slot = "zxbus.1") const
    {
        SlotManager* slots = Context()->pSlotManager;
        return slots != nullptr ? dynamic_cast<MultiSoundSlotCard*>(slots->FindCard(slot)) : nullptr;
    }
    const SlotManager::BuiltIn* BuiltIn(const std::string& id) const
    {
        return Context()->pSlotManager->Current().FindBuiltIn(id);
    }

private:
    SoundCardScope _everySound;
    fs::path _path;
    std::unique_ptr<Emulator> _emulator;
    bool _ok = false;
};

const slots::CardDef& MultiSoundDef()
{
    static const slots::SlotPlanner planner;
    return *planner.FindCard("multisound");
}

} // namespace

// region <Step 1: the card is built for its slot>

TEST(MultiSoundSlotCard_Test, OptionsComeFromTheSlotOptions)
{
    const slots::CardDef& def = MultiSoundDef();
    slots::CardOptions options;
    MultiSoundOptions o = MultiSoundSlotCard::OptionsFrom(def, options);
    EXPECT_TRUE(o.ym && o.saa && o.gs && o.sd) << "default: every DIP function on";
    EXPECT_EQ(o.gsRam, MultiSoundGsRam::OneMb);
    EXPECT_EQ(o.ctrlMask, MultiSoundCtrlMask::Pro);

    ASSERT_TRUE(slots::ParseCardOptions(def, "dip=ym,gs gsRam=2m ctrlMask=classic", options));
    o = MultiSoundSlotCard::OptionsFrom(def, options);
    EXPECT_TRUE(o.ym);
    EXPECT_FALSE(o.saa);
    EXPECT_TRUE(o.gs);
    EXPECT_FALSE(o.sd);
    EXPECT_EQ(o.gsRam, MultiSoundGsRam::TwoMb);
    EXPECT_EQ(o.ctrlMask, MultiSoundCtrlMask::Classic);
}

/// One Pentagon with the card (~40 ms: the card's GS loads its firmware): the card is built for its slot with the
/// slot's options, the mixer's rate and the [MIDI] bank; the board AY stays fitted, shadowed
TEST(MultiSoundSlotCard_Test, BuiltForItsSlotWithItsOptions)
{
    StagedMachine m("pentagon128k",
                    "zxbus.1 = multisound\nzxbus.1.dip = ym,saa,gs\nzxbus.1.gsRam = 2m\nzxbus.1.ctrlMask = classic",
                    "[MIDI]\nBank = midi/ms4-no-such-bank.sf2");
    ASSERT_TRUE(m.Ok());
    MultiSoundSlotCard* card = m.Card();
    ASSERT_NE(card, nullptr) << "the slot builds the card";
    EXPECT_EQ(card->SlotId(), "zxbus.1");
    EXPECT_EQ(std::string(card->Def().id), "multisound");

    const MultiSoundOptions& o = card->Card().Options();
    EXPECT_TRUE(o.ym && o.saa && o.gs);
    EXPECT_FALSE(o.sd);
    EXPECT_EQ(o.ctrlMask, MultiSoundCtrlMask::Classic);
    EXPECT_EQ(card->Card().Gs().getRamSizeKB(), 2048u) << "gsRam=2m sizes the board's GS RAM";

    const MultiSoundCardConfig& config = card->Card().Config();
    EXPECT_EQ(config.hostTickRate, 3500000u) << "the emulator's audio axis";
    EXPECT_EQ(config.outputRate, static_cast<uint32_t>(m.Context()->pSoundManager->getCoreRate()));
    EXPECT_EQ(config.midiBankPath, "midi/ms4-no-such-bank.sf2") << "[MIDI] Bank=";
    MultiSoundCardReport report;
    card->Card().Describe(report);
    EXPECT_EQ(report.midi.bankStatus, "no bank");
    EXPECT_NE(report.midi.bankError.find("ms4-no-such-bank"), std::string::npos) << report.midi.bankError;

    const SlotManager::BuiltIn* ay = m.BuiltIn("ay");
    ASSERT_NE(ay, nullptr);
    EXPECT_EQ(ay->state, "shadowed by zxbus.1");
    EXPECT_FALSE(ay->removed);
    ASSERT_NE(m.Context()->pSoundManager->getTurboSound(), nullptr) << "the Pentagon's AY stays in its socket";
}

/// Two ZX-Evo machines (~40 ms each): Q7, the card takes the YM2149 out of its socket unless the config keeps it
TEST(MultiSoundSlotCard_Test, TakesTheZxEvoYm2149OutOfItsSocket)
{
    {
        StagedMachine m("atm3", "zxbus.1 = multisound");
        ASSERT_TRUE(m.Ok());
        ASSERT_NE(m.Card(), nullptr);
        const SlotManager::BuiltIn* ay = m.BuiltIn("ay");
        ASSERT_NE(ay, nullptr);
        EXPECT_TRUE(ay->removed);
        EXPECT_EQ(ay->state, "taken out of its socket for zxbus.1");
        EXPECT_EQ(m.Context()->config.sound.turboSoundKind, TurboSoundKind::None);
        EXPECT_EQ(m.Context()->pSoundManager->getTurboSound(), nullptr) << "no board AY device";
    }
    {
        // An explicit `ay-socket = ay` keeps the chip: the card would fight it, so it is not fitted
        StagedMachine m("atm3", "ay-socket = ay\nzxbus.1 = multisound");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Card(), nullptr);
        const SlotManager::Slot* slot = nullptr;
        for (const SlotManager::Slot& entry : m.Context()->pSlotManager->Current().entries)
        {
            slot = entry.entry.slot == "zxbus.1" ? &entry : slot;
        }
        ASSERT_NE(slot, nullptr);
        EXPECT_TRUE(slot->entry.disabled);
        EXPECT_NE(slot->entry.disabledReason.find("socket"), std::string::npos) << slot->entry.disabledReason;
        EXPECT_NE(m.Context()->pSoundManager->getTurboSound(), nullptr);
    }
}

// endregion
