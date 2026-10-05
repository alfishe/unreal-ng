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
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"

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

constexpr uint16_t kPc = 0x8000;        // the IN / OUT instruction outside the ROM: no ROM-fetch lock
constexpr uint16_t kRomPc = 0x3D2F;     // TR-DOS ROM: the SAA and SounDrive ports are locked

/// One bus cycle as the Z80 runs it, the IN / OUT instruction's M1 at `m1`
void Out(StagedMachine& m, uint16_t port, uint8_t value, uint16_t m1 = kPc)
{
    m.Context()->pCore->GetZ80()->m1_pc = m1;
    m.Context()->pPortDecoder->WriteCycle(port, value, m1);
}
uint8_t In(StagedMachine& m, uint16_t port, bool& cardDrove, uint16_t m1 = kPc)
{
    m.Context()->pCore->GetZ80()->m1_pc = m1;
    return m.Context()->pPortDecoder->ReadCycle(port, m1, cardDrove);
}

MultiSoundCardReport Report(const MultiSoundSlotCard& card)
{
    MultiSoundCardReport report;
    card.Card().Describe(report);
    return report;
}

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

// region <Step 2: the card on the bus>

/// Pentagon (CardWins, ~15 ms): the card's IORQGE on the #FFFD / #BFFD / #B3 / #BB families hides those cycles from
/// the board, whose AY gets none of them; the passive SAA and SounDrive ports are written on both sides and locked
/// while the IN / OUT runs from the ROM
TEST(MultiSoundSlotCard_Test, PentagonCardShadowsTheBoardAy)
{
    StagedMachine m("pentagon128k", "zxbus.1 = multisound");
    ASSERT_TRUE(m.Ok());
    MultiSoundSlotCard* card = m.Card();
    ASSERT_NE(card, nullptr);
    SoundChip_AY8910* boardAy = m.Context()->pSoundManager->getAYChip(0);
    ASSERT_NE(boardAy, nullptr);
    const uint8_t boardMixerBefore = boardAy->readRegister(7);
    EXPECT_TRUE(m.Context()->pPortDecoder->IsBuiltInShadowed("ay"));

    // YM2203 U4 (chip select 0 after the reset) register 7 through the card
    Out(m, 0xFFFD, 0x07);
    Out(m, 0xBFFD, 0x38);
    EXPECT_EQ(Report(*card).ym[0].ssgRegisters[7], 0x38);
    EXPECT_EQ(boardAy->readRegister(7), boardMixerBefore) << "the board AY saw no cycle";

    // The read: the card drives it, the board is hidden
    bool drove = false;
    EXPECT_EQ(In(m, 0xFFFD, drove), 0x38);
    EXPECT_TRUE(drove);
    EXPECT_FALSE(m.Context()->pPortDecoder->WasLastPortDecoded()) << "the board decoded nothing";

    // GS mailbox (IORQGE): the card's General Sound latches it
    Out(m, 0x00B3, 0x5A);
    EXPECT_EQ(Report(*card).gs.dataFromHost, 0x5A);

    // SAA (passive, ROM-locked): register #1C from RAM code, nothing from the ROM
    Out(m, 0x01FF, 0x1C);
    Out(m, 0x00FF, 0x01);
    EXPECT_EQ(Report(*card).saa.registers[0x1C], 0x01);
    Out(m, 0x01FF, 0x1C, kRomPc);
    Out(m, 0x00FF, 0x00, kRomPc);
    EXPECT_EQ(Report(*card).saa.registers[0x1C], 0x01) << "locked while the OUT runs from #0000-#3FFF";

    // SounDrive channel 0 (#0F, passive, ROM-locked)
    Out(m, 0x000F, 0xC0);
    EXPECT_EQ(card->Card().Logic().Dac(0).sample, MultiSoundLogic::ConvertSample(0xC0));
    Out(m, 0x000F, 0x10, kRomPc);
    EXPECT_EQ(card->Card().Logic().Dac(0).sample, MultiSoundLogic::ConvertSample(0xC0));
}

/// ZX-Evo (BoardWins, ~15 ms): the board keeps its ports from Iorq cards, but the MultiSound detects RD / WR and sees
/// them; with the YM2149 out of its socket the card alone drives #FFFD reads, and its SounDrive channel on the board
/// port #1F works
TEST(MultiSoundSlotCard_Test, ZxEvoCardSeesTheBoardPorts)
{
    StagedMachine m("atm3", "zxbus.1 = multisound");
    ASSERT_TRUE(m.Ok());
    MultiSoundSlotCard* card = m.Card();
    ASSERT_NE(card, nullptr);
    EXPECT_FALSE(m.Context()->pPortDecoder->IsBuiltInShadowed("ay")) << "BoardWins: no shadowing";

    Out(m, 0xFFFD, 0x07);
    Out(m, 0xBFFD, 0x2A);
    EXPECT_EQ(Report(*card).ym[0].ssgRegisters[7], 0x2A);
    bool drove = false;
    EXPECT_EQ(In(m, 0xFFFD, drove), 0x2A);
    EXPECT_TRUE(drove);

    Out(m, 0x001F, 0x90);
    EXPECT_EQ(card->Card().Logic().Dac(1).sample, MultiSoundLogic::ConvertSample(0x90)) << "#1F: SounDrive channel 1";
}

// endregion

// region <Step 3: the five mixer rows>

namespace
{

const AudioSourceType kMsRows[] = { AudioSourceType::MultiSoundFm, AudioSourceType::MultiSoundSsg,
                                    AudioSourceType::MultiSoundSaa, AudioSourceType::MultiSoundDac,
                                    AudioSourceType::MultiSoundMidi };

/// The mixer report's device for a source key; nullptr when the mixer has none
const StateNode* MixerDevice(const StateNode& mixer, const std::string& key)
{
    const StateNode* devices = mixer.find("devices");
    if (devices == nullptr)
        return nullptr;
    for (const StateNode& device : devices->items)
    {
        if (device.find("source") != nullptr && device.find("source")->s == key)
            return &device;
    }
    return nullptr;
}

/// `DI; HALT` at #8000: the frames run with the CPU parked
void ParkCpu(StagedMachine& m)
{
    Z80* z80 = m.Context()->pCore->GetZ80();
    z80->DirectWrite(0x8000, 0xF3);
    z80->DirectWrite(0x8001, 0x76);
    z80->pc = 0x8000;
}

} // namespace

/// Two Pentagons (~30 ms): the rows exist only with the card, carry their names and mixer keys, the shadowed board
/// AY says why it is silent, and the mixer sums on its wide bus
TEST(MultiSoundSlotCard_Test, MixerRowsOnlyWithTheCard)
{
    {
        StagedMachine m("pentagon128k", "zxbus.1 = multisound");
        ASSERT_TRUE(m.Ok());
        SoundManager* sound = m.Context()->pSoundManager;
        const char* names[] = { "MS FM", "MS SSG", "MS SAA", "MS DAC", "MS MIDI" };
        for (size_t i = 0; i < std::size(kMsRows); i++)
        {
            const AudioDeviceInfo* row = sound->device(kMsRows[i]);
            ASSERT_NE(row, nullptr) << names[i];
            EXPECT_EQ(row->name, names[i]);
        }
        EXPECT_TRUE(sound->wideMixEnabled());
        EXPECT_EQ(sound->deviceState(AudioSourceType::AY1_All), "shadowed by zxbus.1");

        const StateNode mixer = DeviceState::AudioMixer(m.Context());
        for (const char* key : { "ms_fm", "ms_ssg", "ms_saa", "ms_dac", "ms_midi" })
        {
            const StateNode* device = MixerDevice(mixer, key);
            ASSERT_NE(device, nullptr) << key;
            EXPECT_TRUE(device->find("capturable")->b) << key;
        }
        const StateNode* ay = MixerDevice(mixer, "ay1");
        ASSERT_NE(ay, nullptr);
        ASSERT_NE(ay->find("state"), nullptr);
        EXPECT_EQ(ay->find("state")->s, "shadowed by zxbus.1");
    }
    {
        StagedMachine m("pentagon128k", "zxbus.1 = moonsound");
        ASSERT_TRUE(m.Ok());
        for (AudioSourceType type : kMsRows)
            EXPECT_EQ(m.Context()->pSoundManager->device(type), nullptr);
        EXPECT_EQ(m.Context()->pSoundManager->deviceState(AudioSourceType::AY1_All), "");
    }
}

/// One Pentagon, three frames (~25 ms): a tone written through the card's ports reaches the MS SSG row and the
/// master mix; the shadowed board AY stays silent
TEST(MultiSoundSlotCard_Test, SsgToneReachesItsRow)
{
    StagedMachine m("pentagon128k", "zxbus.1 = multisound");
    ASSERT_TRUE(m.Ok());
    ParkCpu(m);
    // U4 SSG: tone A period #0100, mixer: tone A only, volume 15
    for (const auto& [reg, value] : { std::pair<uint8_t, uint8_t>{ 0, 0x00 }, { 1, 0x01 }, { 7, 0x3E }, { 8, 0x0F } })
    {
        Out(m, 0xFFFD, reg);
        Out(m, 0xBFFD, value);
    }
    m.Machine().RunNFrames(3);
    SoundManager* sound = m.Context()->pSoundManager;
    EXPECT_GT(sound->device(AudioSourceType::MultiSoundSsg)->peak, 0.01f);
    EXPECT_LT(sound->device(AudioSourceType::MultiSoundFm)->peak, 0.001f) << "FM muted after the reset";
    EXPECT_LT(sound->device(AudioSourceType::AY1_All)->peak, 0.001f) << "the shadowed board AY got no write";
}

// endregion
