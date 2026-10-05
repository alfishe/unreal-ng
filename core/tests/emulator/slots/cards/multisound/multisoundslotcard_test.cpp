// MultiSoundSlotCard (core/src/emulator/slots/cards/multisound/multisoundslotcard.h): the ZX-MultiSound in a slot of a
// running machine (docs/inprogress/2026-10-03-zx-multisound/tdd-integration.md MS-4). The machines are created from a
// shipped config with its [SLOTS] section replaced, as a user would fit the card.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "3rdparty/sam2695/tests/sf2builder.h"
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
#include "multisoundstagedmachine.h"

namespace fs = std::filesystem;
using namespace multisoundtest;

namespace
{

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

/// One Pentagon with the card (~10 ms: the card's GS loads its firmware): the card is built for its slot with the
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

/// Two ZX-Evo machines (~10 ms together): Q7, the card takes the YM2149 out of its socket; a config that keeps it in
/// (`ay-socket = ay`) is refused (Q8)
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
        // An explicit `ay-socket = ay` keeps the chip the card needs out: a conflict, the machine is refused (Q8)
        StagedMachine m("atm3", "ay-socket = ay\nzxbus.1 = multisound");
        EXPECT_FALSE(m.Ok());
        EXPECT_NE(m.Machine().GetInitError().find("YM2149 would have to leave its socket"), std::string::npos)
            << m.Machine().GetInitError();
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

    // IN #BFFD: the card's IORQGE hides it from the board too, and the card does not drive the data bus (its YM data
    // port is write-only): nobody drives, the read floats. Before, the board's decode answered with its AY's register
    drove = true;
    EXPECT_EQ(In(m, 0xBFFD, drove), 0xFF);
    EXPECT_FALSE(drove) << "the card leaves the data bus alone";
    EXPECT_FALSE(m.Context()->pPortDecoder->WasLastPortDecoded()) << "the board AY saw no read cycle";

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

/// The reference data's IORQGE claims follow the CPLD: for every port and both directions a claim asserting IORQGE
/// covers the port exactly when the RTL's IORQGE term is true (it has no direction term: `IN #BFFD` asserts it too).
/// Pure, no machine (~5 ms: 2 x 65536 ports)
TEST(MultiSoundSlotCard_Test, ClaimsAssertIorqgeWhereTheRtlDoes)
{
    const slots::CardDef& def = MultiSoundDef();
    const std::vector<slots::PortClaim> claims = slots::CardClaims(def, {});
    const MultiSoundLogic logic(MultiSoundSlotCard::OptionsFrom(def, {}));
    for (const slots::Dir dir : { slots::Dir::In, slots::Dir::Out })
    {
        size_t mismatches = 0;
        for (uint32_t port = 0; port <= 0xFFFF; port++)
        {
            bool claimed = false;
            for (const slots::PortClaim& claim : claims)
            {
                const bool covers = (port & claim.mask) == claim.match;
                const bool direction = claim.dir == slots::Dir::InOut || claim.dir == dir;
                claimed = claimed || (covers && direction && claim.iorqge == slots::Iorqge::Yes);
            }
            if (claimed != logic.Iorqge(static_cast<uint16_t>(port)) && mismatches++ < 4)
                ADD_FAILURE() << (dir == slots::Dir::In ? "IN #" : "OUT #") << std::hex << port << ": claim " << claimed
                              << ", RTL " << logic.Iorqge(static_cast<uint16_t>(port));
        }
        EXPECT_EQ(mismatches, 0u);
    }
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

// region <Step 4: a program plays all five sources>

namespace
{

/// Z80 code built in the test (the card's ports as a program uses them)
class Program
{
public:
    std::vector<uint8_t> code;

    void Bytes(std::initializer_list<uint8_t> bytes)
    {
        code.insert(code.end(), bytes);
    }
    /// LD BC,port; LD A,value; OUT (C),A
    void Out(uint16_t port, uint8_t value)
    {
        Bytes({ 0x01, static_cast<uint8_t>(port), static_cast<uint8_t>(port >> 8), 0x3E, value, 0xED, 0x79 });
    }
    void Ym(uint8_t reg, uint8_t value)
    {
        Out(0xFFFD, reg);
        Out(0xBFFD, value);
    }
    void Saa(uint8_t reg, uint8_t value)
    {
        Out(0x01FF, reg);
        Out(0x00FF, value);
    }
    uint16_t Here(uint16_t origin) const
    {
        return static_cast<uint16_t>(origin + code.size());
    }
};

constexpr uint16_t kOrigin = 0x8000;
constexpr uint16_t kMidiTable = 0x9000;
constexpr uint8_t kLineHigh = 0xFF;   // R14 with IOA2 = 1 (the MIDI line idle)
constexpr uint8_t kLineLow = 0xFB;    // IOA2 = 0

/// R14 values, one per MIDI bit (start 0, eight data bits LSB first, stop 1), after two idle bits
std::vector<uint8_t> MidiLine(std::initializer_list<uint8_t> bytes)
{
    std::vector<uint8_t> line = { kLineHigh, kLineHigh };
    for (uint8_t byte : bytes)
    {
        line.push_back(kLineLow);
        for (int bit = 0; bit < 8; bit++)
            line.push_back(((byte >> bit) & 1) ? kLineHigh : kLineLow);
        line.push_back(kLineHigh);
    }
    return line;
}

/// The whole program: control byte, a YM2203 FM note and an SSG tone on U4, an SAA tone, SounDrive samples, a
/// General Sound command, then a MIDI Note On bit-banged on U4's IOA2 at 31250 baud (112 T-states a bit at the
/// machine's base clock, x `clockRatio` under a turbo), DI; HALT at the end
std::vector<uint8_t> MultiSoundProgram(uint8_t midiBits, int clockRatio)
{
    Program p;
    p.Bytes({ 0xF3 });                           // DI
    // Control byte: U4 selected, register reads, FM unmuted (bit 2 = 0), SAA clock on (bit 3 = 0)
    p.Out(0xFFFD, 0xF2);
    // U4 SSG: the MIDI line idle (R14 IOA2 = 1) before IOA becomes an output - the reset latch is 0, which would
    // send a start bit - then tone A period #0100, mixer: tone A on, IOA an output, volume 15
    p.Ym(0x0E, kLineHigh);
    p.Ym(0x00, 0x00);
    p.Ym(0x01, 0x01);
    p.Ym(0x07, 0x7E);
    p.Ym(0x08, 0x0F);
    // U4 FM channel 1: algorithm 7 (four carriers), MUL 1, TL 0, AR 31, sustain at the top, block 4 F-number #26A
    for (uint8_t op : { 0x00, 0x04, 0x08, 0x0C })
    {
        p.Ym(static_cast<uint8_t>(0x30 + op), 0x01);
        p.Ym(static_cast<uint8_t>(0x40 + op), 0x00);
        p.Ym(static_cast<uint8_t>(0x50 + op), 0x1F);
        p.Ym(static_cast<uint8_t>(0x60 + op), 0x00);
        p.Ym(static_cast<uint8_t>(0x70 + op), 0x00);
        p.Ym(static_cast<uint8_t>(0x80 + op), 0x0F);
    }
    p.Ym(0xB0, 0x07);
    p.Ym(0xA4, 0x22);
    p.Ym(0xA0, 0x6A);
    p.Ym(0x28, 0xF0);                            // key on, all four operators
    // SAA: voice 0 at full amplitude, tone #80 octave 4, frequency enable, sound enable
    p.Saa(0x00, 0xFF);
    p.Saa(0x08, 0x80);
    p.Saa(0x10, 0x04);
    p.Saa(0x14, 0x01);
    p.Saa(0x1C, 0x01);
    // SounDrive channels 0 (left, #0F) and 2 (right, #4F)
    p.Out(0x000F, 0xF0);
    p.Out(0x004F, 0x10);
    // General Sound command #F3 into the board's GS mailbox
    p.Out(0x00BB, 0xF3);
    // MIDI: select R14, then one OUT per bit from the table, 112 T-states (x clockRatio) apart
    p.Out(0xFFFD, 0x0E);
    p.Bytes({ 0x01, 0xFD, 0xBF });               // LD BC,#BFFD
    p.Bytes({ 0x21, kMidiTable & 0xFF, kMidiTable >> 8 });   // LD HL,table
    p.Bytes({ 0x16, midiBits });                 // LD D,count
    const uint16_t loop = p.Here(kOrigin);
    p.Bytes({ 0x7E, 0xED, 0x79, 0x23 });         // LD A,(HL); OUT (C),A; INC HL           7 + 12 + 6
    // Per bit 49 + 7 N T-states: N = (112 x ratio - 49) / 7 times LD E,0
    const int delays = (112 * clockRatio - 49) / 7;
    for (int i = 0; i < delays; i++)
        p.Bytes({ 0x1E, 0x00 });
    const uint16_t next = static_cast<uint16_t>(p.Here(kOrigin) + 3);
    p.Bytes({ 0xC3, static_cast<uint8_t>(next), static_cast<uint8_t>(next >> 8) });   // JP next   10
    p.Bytes({ 0x15 });                           // DEC D                                   4
    p.Bytes({ 0xC2, static_cast<uint8_t>(loop), static_cast<uint8_t>(loop >> 8) });    // JP NZ,loop   10
    p.Bytes({ 0xF3, 0x76 });                     // DI; HALT
    return p.code;
}

/// The report's entry for a slot / built-in
const StateNode* ReportItem(const StateNode& report, const char* list, const char* key, const std::string& value)
{
    for (const StateNode& item : report.find(list)->items)
    {
        if (item.find(key) != nullptr && item.find(key)->s == value)
            return &item;
    }
    return nullptr;
}

/// Creates the machine with the card and the test bank, lets the SAM2695's 50 ms boot window pass, runs the
/// program and three more frames; checks the slot report, the bus effects and the five rows
void PlayAllFiveSources(const std::string& folder, const std::string& builtInState)
{
    const std::string bank = WriteTestBank();
    StagedMachine m(folder, "zxbus.1 = multisound", "[MIDI]\nBank = " + bank);
    ASSERT_TRUE(m.Ok()) << folder;
    MultiSoundSlotCard* card = m.Card();
    ASSERT_NE(card, nullptr) << folder;
    ASSERT_TRUE(card->Card().MidiBankLoaded()) << Report(*card).midi.bankError;

    // The slot report: the card fitted, the board's AY shadowed (CardWins) or out of its socket (Q7)
    const StateNode slots = DeviceState::Slots(m.Context());
    const StateNode* slot = ReportItem(slots, "slots", "slot", "zxbus.1");
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->find("card")->s, "multisound");
    EXPECT_EQ(slot->find("state")->s, "active");
    EXPECT_EQ(slot->find("fit")->s, "real") << folder;
    const StateNode* ay = ReportItem(slots, "builtIns", "id", "ay");
    ASSERT_NE(ay, nullptr);
    EXPECT_EQ(ay->find("state")->s, builtInState) << folder;
    EXPECT_EQ(ay->find("removed") != nullptr && ay->find("removed")->b, builtInState.rfind("taken out", 0) == 0);

    ParkCpu(m);
    m.Machine().RunNFrames(4);

    const EmulatorState& state = m.Context()->emulatorState;
    const int ratio = state.hw_turbo_ratio_applied > 1 ? static_cast<int>(state.hw_turbo_ratio_applied) : 1;
    const std::vector<uint8_t> midi = MidiLine({ 0x90, 0x3C, 0x64 });   // Note On, channel 1, middle C, velocity 100
    const std::vector<uint8_t> code = MultiSoundProgram(static_cast<uint8_t>(midi.size()), ratio);
    Z80* z80 = m.Context()->pCore->GetZ80();
    for (size_t i = 0; i < code.size(); i++)
        z80->DirectWrite(static_cast<uint16_t>(kOrigin + i), code[i]);
    for (size_t i = 0; i < midi.size(); i++)
        z80->DirectWrite(static_cast<uint16_t>(kMidiTable + i), midi[i]);
    z80->halted = 0;   // leave the parking HALT
    z80->pc = kOrigin;
    m.Machine().RunNFrames(3);

    const MultiSoundCardReport report = Report(*card);
    EXPECT_FALSE(report.latches.fmMuted);
    EXPECT_TRUE(report.latches.saaClock);
    EXPECT_EQ(report.ym[0].ssgRegisters[7], 0x7E);
    EXPECT_EQ(report.saa.registers[0x1C], 0x01);
    EXPECT_EQ(report.gs.commandFromHost, 0xF3) << "the command reached the board's General Sound";
    EXPECT_EQ(report.midi.bytesReceived, 3u) << folder << " clock ratio " << ratio;
    EXPECT_EQ(report.midi.framingErrors, 0u);
    EXPECT_GE(report.midi.activeVoices, 1u);

    SoundManager* sound = m.Context()->pSoundManager;
    const char* names[] = { "MS FM", "MS SSG", "MS SAA", "MS DAC", "MS MIDI" };
    for (size_t i = 0; i < std::size(kMsRows); i++)
        EXPECT_GT(sound->device(kMsRows[i])->peak, 0.005f) << folder << " " << names[i];

    std::error_code ignored;
    fs::remove(bank, ignored);
}

} // namespace

/// Pentagon 128 (CardWins): ~25 ms - seven frames of a machine with the card and its GS firmware running
TEST(MultiSoundSlotCard_Test, PentagonProgramPlaysAllFiveSources)
{
    PlayAllFiveSources("pentagon128k", "shadowed by zxbus.1");
}

/// ZX-Evo Baseconf (BoardWins, the YM2149 out of its socket): ~40 ms, as above
TEST(MultiSoundSlotCard_Test, ZxEvoProgramPlaysAllFiveSources)
{
    PlayAllFiveSources("atm3", "taken out of its socket for zxbus.1");
}

/// The matrix's conflicts between configured entries refuse the machine (owner decision Q8, 2026-10-05), with each
/// pair and its rule in the reason: a second General Sound (either order), a TurboSound FM in the socket the card
/// would shadow (a pointless pair), the ZX-Evo YM2149 kept in its socket by an explicit `ay-socket = ay`. A card the
/// machine cannot take (the 128K edge has no IORQGE) is no conflict: left out with the reason, the machine starts.
/// Five machines (~15 ms each; a refused one stops before its sound devices are built)
TEST(MultiSoundSlotCard_Test, MatrixConflictsRefuseCreation)
{
    struct Case
    {
        const char* folder;
        const char* slots;
        const char* pair;
        const char* rule;
    };
    const Case refused[] = {
        { "pentagon128k", "zxbus.1 = multisound\nzxbus.2 = gs", "zxbus.2 = gs and zxbus.1 = multisound", "D1" },
        { "pentagon128k", "zxbus.1 = gs\nzxbus.2 = multisound", "zxbus.2 = multisound and zxbus.1 = gs", "D1" },
        { "pentagon128k", "ay-socket = tsfm\nzxbus.1 = multisound", "zxbus.1 = multisound and ay-socket = tsfm", "D3" },
        { "atm3", "ay-socket = ay\nzxbus.1 = multisound", "zxbus.1 = multisound and ay-socket = ay", "Q7" },
    };
    for (const Case& c : refused)
    {
        StagedMachine m(c.folder, c.slots);
        EXPECT_FALSE(m.Ok()) << c.folder << ": " << c.slots;
        const std::string error = m.Machine().GetInitError();
        EXPECT_NE(error.find(c.pair), std::string::npos) << error;
        EXPECT_NE(error.find(std::string("(") + c.rule + ":"), std::string::npos) << error;
    }
    {
        StagedMachine m("spectrum128", "edge.1 = multisound");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Card("edge.1"), nullptr) << "the 128K edge has no IORQGE";
        bool disabled = false;
        for (const SlotManager::Slot& slot : m.Context()->pSlotManager->Current().entries)
            disabled = disabled || (slot.entry.slot == "edge.1" && slot.entry.disabled);
        EXPECT_TRUE(disabled);
    }
}

// endregion
