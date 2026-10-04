// SlotManager (core/src/emulator/slots/slotmanager.h): the slot set of an instance, planned at creation from [SLOTS]
// or from the legacy card keys (docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §5-§6, tdd.md §2.1).
//
// ShippedModels: every shipped config (data/configs/<folder>/unreal.ini) fits exactly the devices master fitted
// before the cards moved onto slots. The expected set per folder is testdata/slots/fitted-devices.txt, captured from
// the binary built from master's device code (the merge of master into zx-bus-slots before SL-4 step 1, which still
// decided every card from the legacy INI keys). Regenerate it only for a deliberate change of the shipped device set:
//   UNREAL_SLOTS_FITTED_UPDATE=1 tools/build/test.sh --gtest_filter='SlotManagerShipped_Test.*'

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "common/inifile.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/covox.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"
#include "emulator/io/network/networkspec.h"

namespace fs = std::filesystem;
using namespace slots;

namespace
{

constexpr const char* kFittedDevicesFile = "testdata/slots/fitted-devices.txt";

std::string Hex4(uint16_t value)
{
    char text[8];
    std::snprintf(text, sizeof text, "%04X", value);
    return text;
}

/// The devices one machine fits, as text: mixer rows, the sound devices, the network card bits, the port map rows
/// (every registered peripheral included), the full-decode claims and the TTD device set with each blob's size
std::string DescribeFittedDevices(EmulatorContext* context)
{
    std::ostringstream out;
    out << "model " << static_cast<int>(context->config.mem_model) << "\n";

    SoundManager* sound = context->pSoundManager;
    if (sound != nullptr)
    {
        out << "mixer";
        for (const auto& device : sound->devices())
        {
            out << " [" << device.name << "]";
        }
        out << "\n";
        ITurboSoundDevice* ts = sound->getTurboSound();
        out << "turbosound " << (ts == nullptr ? std::string("none")
                                               : std::to_string(ts->getChipCount()) + (ts->hasFm() ? " fm" : " ay"))
            << "\n";
        out << "gs " << (sound->getGeneralSound() != nullptr ? sound->generalSoundDeviceName() : std::string("none"))
            << "\n";
        out << "covox "
            << (!sound->hasCovox()                                          ? "none"
                : sound->getCovox()->fitment() == Covox::Fitment::Quad      ? "quad"
                : sound->getCovox()->fitment() == Covox::Fitment::Mode1     ? "mode1"
                : sound->getCovox()->fitment() == Covox::Fitment::Mode2     ? "mode2"
                : sound->getCovox()->fitment() == Covox::Fitment::Mode1Mono ? "mode1+mono"
                                                                            : "mono")
            << "\n";
    }
    out << "network.card " << static_cast<int>(context->config.network.card) << " comport "
        << (context->pComPort != nullptr ? 1 : 0) << "\n";

    PortDecoder* decoder = context->pPortDecoder;
    if (decoder != nullptr)
    {
        std::vector<std::string> rows;
        for (const PortMapEntry& entry : decoder->getPortMapEntries())
        {
            rows.push_back("port " + Hex4(entry.port) + " " + Hex4(entry.mask) + "/" + Hex4(entry.match) + " " +
                           (entry.device != nullptr ? entry.device : "?"));
        }
        std::sort(rows.begin(), rows.end());
        for (const std::string& row : rows)
        {
            out << row << "\n";
        }
        for (const auto& claim : decoder->GetFullDecodeClaims().Entries())
        {
            out << "fulldecode " << Hex4(claim.mask) << "/" + Hex4(claim.match) << " slot " << int(claim.slot) << "\n";
        }
    }

    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    std::string error;
    if (!ttd::RegisterMachinePeripherals(context, registry, owned, &error))
    {
        out << "ttd error " << error << "\n";
    }
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    std::map<int, size_t> sizes;
    for (const auto& [id, blob] : blobs)
    {
        sizes[id] = blob.size();
    }
    out << "ttd";
    for (const auto& [id, size] : sizes)
    {
        // The Ethernet NICs' blob carries per-process network state of variable length (its layout is guarded by
        // the TTD corpus): only its presence is compared
        if (id == static_cast<int>(ttd::PeripheralId::EthernetNics))
        {
            out << " " << id << ":var";
            continue;
        }
        out << " " << id << ":" << size;
    }
    out << " notRecorded " << registry.NotRecordedMask() << "\n";
    return out.str();
}

/// The card fields of a parsed config (for a config whose machine cannot be created in a test)
std::string DescribeConfigCards(const CONFIG& config)
{
    std::ostringstream out;
    out << "config turbosound " << static_cast<int>(config.sound.turboSoundKind) << " gs "
        << static_cast<int>(config.sound.gsTypeKind) << " moonsound " << config.sound.moonsound << " covoxFB "
        << config.sound.covoxFB << " sd " << config.sound.sd << " network.card " << int(config.network.card) << "\n";
    return out.str();
}

/// The shipped config folders (data/configs/*/unreal.ini). A fixed list: the parameter generator runs before main(),
/// where the project root is not known yet; ShippedConfigFoldersAreListed keeps it complete
const std::vector<std::string>& ShippedConfigFolders()
{
    static const std::vector<std::string> folders = {
        "atm3", "atm450", "atm710", "pentagon128k", "pentagon512k", "profi", "profi3", "profscorp", "scorpion",
        "spectrum128", "spectrum2", "spectrum2a", "spectrum3", "spectrum48", "sprinter", "ts-conf", "zx-diagnostics",
    };
    return folders;
}

/// The expected text per folder: blocks "== <folder>" followed by that folder's DescribeFittedDevices lines
std::map<std::string, std::string> ReadExpected()
{
    std::map<std::string, std::string> blocks;
    std::ifstream in(TestPathHelper::FindProjectRoot() / kFittedDevicesFile, std::ios::binary);
    std::string line;
    std::string folder;
    while (std::getline(in, line))
    {
        if (line.rfind("== ", 0) == 0)
        {
            folder = line.substr(3);
            blocks[folder];
            continue;
        }
        if (!folder.empty())
        {
            blocks[folder] += line + "\n";
        }
    }
    return blocks;
}

class SlotManagerShipped_Test : public ::testing::TestWithParam<std::string>
{
};

/// Creates the machine of one shipped config with every sound device as configured (the runner otherwise leaves
/// them out, SoundCardScope) and compares its fitted devices with master's. Slower than 50 ms (one full machine with
/// its NeoGS / MoonSound firmware per config): creating the machine is what the test checks
TEST_P(SlotManagerShipped_Test, FitsTheDevicesOfMaster)
{
    const std::string folder = GetParam();
    SoundCardScope everySound;
    const fs::path ini = TestPathHelper::FindProjectRoot() / "data" / "configs" / folder / "unreal.ini";
    Emulator emulator(LoggerLevel::LogError);
    std::string actual;
    if (folder == "zx-diagnostics")
    {
        // A 48K config for a diagnostics ROM the user supplies (64 KB images): the machine is not creatable from the
        // shipped files alone, so only the card fields its config parse produces are compared (parsed into a
        // default machine's context)
        ASSERT_TRUE(emulator.Init());
        Config config(emulator.GetContext());
        ASSERT_TRUE(config.LoadConfigFile(ini.string()));
        actual = DescribeConfigCards(emulator.GetContext()->config);
    }
    else
    {
        emulator.SetCustomConfigPath(ini.string());
        ASSERT_TRUE(emulator.Init()) << folder;
        actual = DescribeFittedDevices(emulator.GetContext());
    }
    emulator.Release();

    if (std::getenv("UNREAL_SLOTS_FITTED_UPDATE") != nullptr)
    {
        // Rewrites this folder's block, keeping the others (one test per folder)
        std::map<std::string, std::string> blocks = ReadExpected();
        blocks[folder] = actual;
        std::ofstream out(TestPathHelper::FindProjectRoot() / kFittedDevicesFile, std::ios::binary);
        for (const auto& [name, text] : blocks)
        {
            out << "== " << name << "\n" << text;
        }
        GTEST_SKIP() << "updated " << kFittedDevicesFile << " for " << folder;
    }

    const std::map<std::string, std::string> expected = ReadExpected();
    const auto found = expected.find(folder);
    ASSERT_NE(found, expected.end()) << folder << " has no block in " << kFittedDevicesFile;
    EXPECT_EQ(actual, found->second) << folder;
}

// region <Plan at creation>

constexpr uint32_t kAllGroups = ~0u;

/// A config as the parser leaves it for a model: no [SLOTS], no card (TurboSound slot empty, no GS, ...)
std::unique_ptr<CONFIG> MakeConfig(MEM_MODEL model)
{
    auto config = std::make_unique<CONFIG>();
    config->mem_model = model;
    config->sound.turboSoundKind = TurboSoundKind::None;
    config->sound.gsTypeKind = GSTypeKind::NONE;
    config->sound.gsRamKB = 128;
    config->ngs.ramKB = 2048;
    return config;
}

/// [SLOTS] lines as the INI parser hands them over
SlotConfig Slots(std::initializer_list<std::pair<std::string, std::string>> lines)
{
    SlotConfig config;
    ParseSlotsSection(std::vector<std::pair<std::string, std::string>>(lines), config);
    return config;
}

/// [SLOTS] as the INI parser leaves it in a config: parsed, and the card fields projected from it
void SetSlots(CONFIG& config, std::initializer_list<std::pair<std::string, std::string>> lines)
{
    config.slotConfig = Slots(lines);
    SlotManager::Project(config.slotConfig, config, kAllGroups);
}

const SlotManager::Slot* Entry(const SlotManager::Result& result, const std::string& slot)
{
    for (const SlotManager::Slot& entry : result.entries)
    {
        if (entry.entry.slot == slot)
        {
            return &entry;
        }
    }
    return nullptr;
}

bool LogHas(const SlotManager::Result& result, const std::string& text)
{
    return std::any_of(result.log.begin(), result.log.end(),
                       [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

TEST(SlotManager_Test, ParsesTheSlotsSection)
{
    // Options may come before the card line; option names keep their case; adapter and fit are slot keys
    const SlotConfig config = Slots({ { "zxbus.2.mode", "2" },
                                      { "ay-socket", "TSFM" },
                                      { "zxbus.2", "soundrive" },
                                      { "Zxbus.1", "neogs" },
                                      { "zxbus.1.ram", "4m" },
                                      { "edge.1", "gs" },
                                      { "edge.1.adapter", "zxbus-to-sinclair-edge" },
                                      { "edge.1.fit", "unrealistic" },
                                      { "builtin.covox", "off" },
                                      { "zxbus.0", "gs" },
                                      { "zxbus.3.ram", "1m" },
                                      { "nonsense", "1" } });
    EXPECT_TRUE(config.section);
    ASSERT_EQ(config.entries.size(), 4u);
    EXPECT_EQ(config.entries[0].slot, "zxbus.2");
    EXPECT_EQ(config.entries[0].card, "soundrive");
    EXPECT_EQ(config.entries[0].options, "mode=2");
    EXPECT_EQ(config.entries[1].slot, "ay-socket");
    EXPECT_EQ(config.entries[1].card, "tsfm") << "card ids are lower case";
    EXPECT_EQ(config.entries[2].slot, "zxbus.1");
    EXPECT_EQ(config.entries[2].options, "ram=4m");
    EXPECT_EQ(config.entries[3].adapter, "zxbus-to-sinclair-edge");
    EXPECT_TRUE(config.entries[3].fitOverride);
    EXPECT_FALSE(config.entries[0].fitOverride);
    ASSERT_EQ(config.builtIns.size(), 1u);
    EXPECT_EQ(config.builtIns[0].id, "covox");
    EXPECT_FALSE(config.builtIns[0].on);
    // zxbus.0 (slots count from 1), options without a card, an unknown key
    EXPECT_EQ(config.errors.size(), 3u);

    // The text form reads back to the same configuration
    IniFile ini;
    ini.LoadData(FormatSlotsSection(config));
    SlotConfig again;
    ParseSlotsSection(ini.GetSectionEntries("SLOTS"), again);
    ASSERT_EQ(again.entries.size(), config.entries.size());
    for (size_t i = 0; i < config.entries.size(); i++)
    {
        EXPECT_EQ(again.entries[i].slot, config.entries[i].slot);
        EXPECT_EQ(again.entries[i].card, config.entries[i].card);
        EXPECT_EQ(again.entries[i].options, config.entries[i].options);
        EXPECT_EQ(again.entries[i].adapter, config.entries[i].adapter);
        EXPECT_EQ(again.entries[i].fitOverride, config.entries[i].fitOverride);
    }
    EXPECT_TRUE(again.errors.empty());
}

TEST(SlotManager_Test, LegacyKeysTranslated)
{
    // [SOUND] GSType=NGS, TurboSound=FM, [NETWORK] Card=ZXNETUSB -> [SLOTS] entries, each logged as deprecated
    auto config = MakeConfig(MM_PENTAGON);
    config->sound.turboSoundKind = TurboSoundKind::FM;
    config->sound.gsTypeKind = GSTypeKind::NGS;
    config->network.card = networkspec::kCardZxNetUsb;
    config->slotConfig.legacyKeys = { "[SOUND] TurboSound", "[SOUND] GSType", "[SOUND] MoonSound", "[NETWORK] Card" };

    const SlotConfig translated = SlotManager::TranslateLegacy(*config);
    ASSERT_EQ(translated.entries.size(), 3u);
    EXPECT_EQ(translated.entries[0].slot, "ay-socket");
    EXPECT_EQ(translated.entries[0].card, "tsfm");
    EXPECT_EQ(translated.entries[1].slot, "zxbus.1");
    EXPECT_EQ(translated.entries[1].card, "neogs");
    EXPECT_EQ(translated.entries[1].options, "ram=2m");
    EXPECT_EQ(translated.entries[2].slot, "zxbus.2");
    EXPECT_EQ(translated.entries[2].card, "zxnetusb");
    for (const SlotConfigEntry& entry : translated.entries)
    {
        EXPECT_TRUE(entry.fitOverride) << entry.slot << ": an old INI keeps the devices it had";
    }

    const SlotManager::Result result = SlotManager::Plan(*config, kAllGroups);
    EXPECT_FALSE(result.fromSlotsSection);
    EXPECT_TRUE(LogHas(result, "deprecated key [SOUND] TurboSound -> [SLOTS] ay-socket = tsfm"));
    EXPECT_TRUE(LogHas(result, "deprecated key [SOUND] GSType -> [SLOTS] zxbus.1 = neogs (ram=2m)"));
    EXPECT_TRUE(LogHas(result, "deprecated key [NETWORK] Card -> [SLOTS] zxbus.2 = zxnetusb"));
    EXPECT_TRUE(LogHas(result, "deprecated key [SOUND] MoonSound -> no card"));
    for (const SlotManager::Slot& slot : result.entries)
    {
        EXPECT_FALSE(slot.entry.disabled) << slot.entry.slot << ": " << slot.entry.disabledReason;
        EXPECT_EQ(slot.fit, Fit::Real) << slot.entry.slot << ": the Pentagon's (retrofitted) ZX-bus takes them";
    }

    // Applied, the fields are what the keys said
    auto applied = MakeConfig(MM_PENTAGON);
    SlotManager::Apply(result, *applied, kAllGroups);
    EXPECT_EQ(applied->sound.turboSoundKind, TurboSoundKind::FM);
    EXPECT_EQ(applied->sound.gsTypeKind, GSTypeKind::NGS);
    EXPECT_EQ(applied->network.card, networkspec::kCardZxNetUsb);
}

TEST(SlotManager_Test, IniLoadUsesSamePlan)
{
    // An INI with a clash: the first card in slot order wins, the later one is disabled with the reason (no
    // replace flag at load), and the machine still gets its cards
    auto config = MakeConfig(MM_PENTAGON);
    SetSlots(*config, { { "zxbus.2", "neogs" }, { "zxbus.1", "gs" }, { "zxbus.1.ram", "512k" },
                                 { "zxbus.3", "moonsound" } });
    const SlotManager::Result result = SlotManager::Plan(*config, kAllGroups);
    EXPECT_TRUE(result.fromSlotsSection);
    ASSERT_NE(Entry(result, "zxbus.1"), nullptr);
    ASSERT_NE(Entry(result, "zxbus.2"), nullptr);
    EXPECT_FALSE(Entry(result, "zxbus.1")->entry.disabled);
    EXPECT_TRUE(Entry(result, "zxbus.2")->entry.disabled);
    EXPECT_NE(Entry(result, "zxbus.2")->entry.disabledReason.find("zxbus.1"), std::string::npos)
        << Entry(result, "zxbus.2")->entry.disabledReason;
    EXPECT_FALSE(Entry(result, "zxbus.3")->entry.disabled);
    EXPECT_TRUE(LogHas(result, "zxbus.2 = neogs"));

    SlotManager::Apply(result, *config, kAllGroups);
    EXPECT_EQ(config->sound.gsTypeKind, GSTypeKind::Z80) << "the first GS card";
    EXPECT_EQ(config->sound.gsRamKB, 512u) << "its RAM option";
    EXPECT_EQ(config->sound.moonsound, 1);
    EXPECT_EQ(config->sound.turboSoundKind, TurboSoundKind::Single) << "no ay-socket key: the machine's own AY";
}

TEST(SlotManager_Test, FitOverrideNeverDisplaces)
{
    // The 128K edge connector cannot carry a GS card: without the override it is refused, with it fitted as
    // unrealistic; a translated legacy key carries the override, so an old INI keeps its card
    auto config = MakeConfig(MM_SPECTRUM128);
    SetSlots(*config, { { "edge.1", "neogs" }, { "edge.1.adapter", "zxbus-to-sinclair-edge" } });
    SlotManager::Result result = SlotManager::Plan(*config, kAllGroups);
    ASSERT_NE(Entry(result, "edge.1"), nullptr);
    EXPECT_TRUE(Entry(result, "edge.1")->entry.disabled);

    config->slotConfig.entries[0].fitOverride = true;
    SlotManager::Project(config->slotConfig, *config, kAllGroups);
    result = SlotManager::Plan(*config, kAllGroups);
    EXPECT_FALSE(Entry(result, "edge.1")->entry.disabled) << Entry(result, "edge.1")->entry.disabledReason;
    EXPECT_EQ(Entry(result, "edge.1")->fit, Fit::Unrealistic);

    // Even with the override a second GS card is not fitted: the override never displaces the first one
    config->slotConfig.entries.push_back({ "edge.2", "gs", "", "zxbus-to-sinclair-edge", true, "[SLOTS] edge.2" });
    SlotManager::Project(config->slotConfig, *config, kAllGroups);
    result = SlotManager::Plan(*config, kAllGroups);
    EXPECT_FALSE(Entry(result, "edge.1")->entry.disabled);
    EXPECT_TRUE(Entry(result, "edge.2")->entry.disabled);
}

TEST(SlotManager_Test, HardRefusalKeptEvenWithTheOverride)
{
    // MoonSound on the Profi: #7E is the palette, the board wins - refused even from a legacy key
    auto config = MakeConfig(MM_PROFI);
    config->sound.moonsound = 1;
    const SlotManager::Result result = SlotManager::Plan(*config, kAllGroups);
    const SlotManager::Slot* moonsound = nullptr;
    for (const SlotManager::Slot& slot : result.entries)
    {
        moonsound = slot.entry.card == "moonsound" ? &slot : moonsound;
    }
    ASSERT_NE(moonsound, nullptr);
    EXPECT_TRUE(moonsound->entry.disabled);
    EXPECT_NE(moonsound->entry.disabledReason.find("palette"), std::string::npos) << moonsound->entry.disabledReason;
    EXPECT_EQ(moonsound->source, "[SOUND] MoonSound=1");
}

TEST(SlotManager_Test, NotEmulatedCardsAreDisabled)
{
    auto config = MakeConfig(MM_PENTAGON);
    SetSlots(*config, { { "zxbus.1", "multisound" }, { "zxbus.2", "zx-wifi" }, { "zxbus.2.port", "ee" },
                                 { "zxbus.3", "covox-fb" }, { "zxbus.3.decode", "a2" }, { "zxbus.4", "nosuchcard" } });
    const SlotManager::Result result = SlotManager::Plan(*config, kAllGroups);
    for (const char* slot : { "zxbus.1", "zxbus.2", "zxbus.3", "zxbus.4" })
    {
        ASSERT_NE(Entry(result, slot), nullptr) << slot;
        EXPECT_TRUE(Entry(result, slot)->entry.disabled) << slot;
    }
    EXPECT_NE(Entry(result, "zxbus.1")->entry.disabledReason.find("not emulated"), std::string::npos);
    EXPECT_NE(Entry(result, "zxbus.4")->entry.disabledReason.find("unknown card"), std::string::npos);
}

TEST(SlotManager_Test, AFieldChangedAfterTheIniWins)
{
    // The test runner's sound policy (and a snapshot transfer) set card fields after the INI was read: with
    // [SLOTS] the changed group follows the field, the others keep the section
    auto config = MakeConfig(MM_PENTAGON);
    SetSlots(*config, { { "ay-socket", "tsfm" }, { "zxbus.1", "neogs" }, { "zxbus.2", "moonsound" } });
    SlotManager::Project(config->slotConfig, *config, kAllGroups);
    EXPECT_EQ(config->sound.gsTypeKind, GSTypeKind::NGS);
    config->sound.gsTypeKind = GSTypeKind::NONE;            // what the policy does
    config->sound.turboSoundKind = TurboSoundKind::None;

    const SlotManager::Result result = SlotManager::Plan(*config, kAllGroups);
    EXPECT_EQ(result.FindGroup(SlotCardGroup::GeneralSound), nullptr);
    ASSERT_NE(result.FindSlot("ay-socket"), nullptr);
    EXPECT_EQ(result.FindSlot("ay-socket")->entry.card, "none");
    ASSERT_NE(result.FindGroup(SlotCardGroup::MoonSound), nullptr) << "unchanged: still the section's card";
    SlotManager::Apply(result, *config, kAllGroups);
    EXPECT_EQ(config->sound.gsTypeKind, GSTypeKind::NONE);
    EXPECT_EQ(config->sound.turboSoundKind, TurboSoundKind::None);
    EXPECT_EQ(config->sound.moonsound, 1);
}

/// A shipped config with its [SLOTS] section replaced (any legacy card keys are then ignored), staged in the
/// per-process scratch folder; the machine is created from it with every sound device as configured
class StagedMachine
{
public:
    StagedMachine(const std::string& folder, const std::string& slotsSection)
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
        text += "\n[SLOTS]\n" + slotsSection + "\n";
        _path = TestPathHelper::GetUniqueTestScratchPath("slots-" + folder + ".ini");
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

private:
    SoundCardScope _everySound;
    fs::path _path;
    std::unique_ptr<Emulator> _emulator;
    bool _ok = false;
};

/// What the AY socket holds: "none", "<chips> ay", "<chips> fm"
std::string SocketDevice(EmulatorContext* context)
{
    ITurboSoundDevice* ts = context->pSoundManager->getTurboSound();
    return ts == nullptr ? std::string("none") : std::to_string(ts->getChipCount()) + (ts->hasFm() ? " fm" : " ay");
}

/// Step 2: the AY socket's content comes from the slot. Four machines (about 20 ms each): the slot's effect is
/// what is checked, on the device SoundManager builds
TEST(SlotManager_Test, SocketBoardComesFromTheSlot)
{
    {
        StagedMachine m("spectrum48", "ay-socket = tsfm");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(SocketDevice(m.Context()), "2 fm") << "the 48K's retrofitted AY socket takes a TurboSound FM";
    }
    {
        StagedMachine m("pentagon128k", "ay-socket = ay");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(SocketDevice(m.Context()), "1 ay") << "the machine's own AY (the ini's TurboSound=FM ignored)";
    }
    {
        StagedMachine m("pentagon128k", "ay-socket = none\nzxbus.1 = moonsound");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(SocketDevice(m.Context()), "none");
        const StateNode report = DeviceState::Slots(m.Context());
        bool socketEmpty = false;
        for (const StateNode& builtIn : report.find("builtIns")->items)
        {
            socketEmpty = socketEmpty || (builtIn.find("id")->s == "ay" && builtIn.find("state")->s == "socket empty");
        }
        EXPECT_TRUE(socketEmpty);
    }
    {
        StagedMachine m("scorpion", "zxbus.1 = neogs");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(SocketDevice(m.Context()), "1 ay") << "[SLOTS] without the socket: the machine's own chip";
    }
}

/// Step 3: the General Sound personality comes from the slot (the runtime switch stays until SL-6). Five machines
/// (about 20 ms each)
TEST(SlotManager_Test, GeneralSoundComesFromTheSlot)
{
    {
        StagedMachine m("pentagon128k", "zxbus.1 = gs\nzxbus.1.ram = 512k");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Context()->config.sound.gsTypeKind, GSTypeKind::Z80) << "the ini's GSType=NGS ignored";
        EXPECT_EQ(m.Context()->config.sound.gsRamKB, 512u) << "the card's RAM option";
        EXPECT_NE(m.Context()->pSoundManager->getGeneralSound(), nullptr);
    }
    {
        StagedMachine m("scorpion", "zxbus.2 = gs-lw");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Context()->config.sound.gsTypeKind, GSTypeKind::LW);
    }
    {
        StagedMachine m("pentagon128k", "zxbus.1 = moonsound");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Context()->pSoundManager->getGeneralSound(), nullptr) << "no GS card in the slots";
    }
    {
        // The 128K edge has no IORQGE: refused without the override ...
        StagedMachine m("spectrum128", "edge.1 = neogs\nedge.1.adapter = zxbus-to-sinclair-edge");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Context()->pSoundManager->getGeneralSound(), nullptr);
    }
    {
        // ... fitted as unrealistic with it
        StagedMachine m("spectrum128", "edge.1 = neogs\nedge.1.adapter = zxbus-to-sinclair-edge\nedge.1.fit = unrealistic");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Context()->config.sound.gsTypeKind, GSTypeKind::NGS);
        EXPECT_NE(m.Context()->pSoundManager->getGeneralSound(), nullptr);
    }
}

/// The report's entry for a card; nullptr when the report has none
const StateNode* ReportSlot(const StateNode& report, const std::string& card)
{
    for (const StateNode& slot : report.find("slots")->items)
    {
        if (slot.find("card")->s == card)
        {
            return &slot;
        }
    }
    return nullptr;
}

/// Step 4: MoonSound through the slot. The Profi's #7E palette is a fixed built-in the board wins, so the card is
/// refused with that reason (it used to be an INI comment), even with the fit override (the legacy key too:
/// HardRefusalKeptEvenWithTheOverride). Three machines (~20 ms each)
TEST(SlotManager_Test, MoonSoundComesFromTheSlot)
{
    {
        StagedMachine m("pentagon128k", "zxbus.1 = moonsound");
        ASSERT_TRUE(m.Ok());
        EXPECT_NE(m.Context()->pSoundManager->getMoonSound(), nullptr);
    }
    {
        StagedMachine m("pentagon128k", "zxbus.1 = neogs");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Context()->pSoundManager->getMoonSound(), nullptr) << "the ini's MoonSound=1 ignored";
    }
    {
        StagedMachine m("profi", "ay-socket = ay\nprofi-bus.1 = moonsound\nprofi-bus.1.fit = unrealistic");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Context()->pSoundManager->getMoonSound(), nullptr);
        EXPECT_EQ(m.Context()->config.sound.moonsound, 0);
        const StateNode report = DeviceState::Slots(m.Context());
        const StateNode* slot = ReportSlot(report, "moonsound");
        ASSERT_NE(slot, nullptr);
        EXPECT_EQ(slot->find("state")->s, "disabled");
        EXPECT_NE(slot->find("reason")->s.find("palette"), std::string::npos) << slot->find("reason")->s;
    }
}

/// The Covox fitment SoundManager builds from a config's card fields: "none", "mono", "quad", ...
std::string CovoxOf(const CONFIG& config)
{
    if (!config.sound.covoxFB && !config.sound.sd)
    {
        return "none";
    }
    if (!config.sound.sd)
    {
        return "mono";
    }
    return config.sound.sdMode == 1 ? (config.sound.covoxFB ? "mode1+mono" : "mode1")
           : config.sound.sdMode == 2 ? "mode2"
                                      : "quad";
}

std::string PlannedCovox(MEM_MODEL model, std::initializer_list<std::pair<std::string, std::string>> lines)
{
    auto config = MakeConfig(model);
    SetSlots(*config, lines);
    const SlotManager::Result result = SlotManager::Plan(*config, kAllGroups);
    SlotManager::Apply(result, *config, kAllGroups);
    return CovoxOf(*config);
}

/// Step 5: covox-fb and soundrive cards (the SounDrive mode switch an option) and the board Covox of the machines
/// that have one, switched by builtin.covox
TEST(SlotManager_Test, CovoxCardsAndTheBoardCovoxComeFromTheSlots)
{
    EXPECT_EQ(PlannedCovox(MM_PENTAGON, { { "zxbus.1", "covox-fb" } }), "mono");
    EXPECT_EQ(PlannedCovox(MM_PENTAGON, { { "zxbus.1", "soundrive" } }), "mode1") << "the card's default: mode 1";
    EXPECT_EQ(PlannedCovox(MM_PENTAGON, { { "zxbus.1", "soundrive" }, { "zxbus.1.mode", "2" } }), "mode2");
    EXPECT_EQ(PlannedCovox(MM_PENTAGON, { { "zxbus.1", "soundrive" }, { "zxbus.1.mode", "both" } }), "quad");
    EXPECT_EQ(PlannedCovox(MM_PENTAGON, { { "zxbus.1", "soundrive" }, { "zxbus.2", "covox-fb" } }), "mode1+mono")
        << "mode 1 leaves #FB to a Covox card";
    EXPECT_EQ(PlannedCovox(MM_PENTAGON, { { "zxbus.1", "soundrive" }, { "zxbus.1.mode", "2" }, { "zxbus.2", "covox-fb" } }),
              "mode2") << "mode 2 is #FB too: the Covox card in the later slot is disabled";
    EXPECT_EQ(PlannedCovox(MM_PENTAGON, { { "builtin.covox", "on" } }), "none") << "the Pentagon has no board Covox";

    // The board Covox: on by default with [SLOTS], off by its switch
    EXPECT_EQ(PlannedCovox(MM_ATM3, { { "zxbus.1", "neogs" } }), "mono");
    EXPECT_EQ(PlannedCovox(MM_ATM3, { { "builtin.covox", "off" } }), "none");
    EXPECT_EQ(PlannedCovox(MM_PROFI, { { "builtin.covox", "on" } }), "mono");
    EXPECT_EQ(PlannedCovox(MM_TSL, { { "zxbus.1", "covox-fb" } }), "mono") << "TS-Conf: #FB is the board's, the card refused";

    // Legacy keys: CovoxFB=1 alone switches the board Covox where there is one (TS-Conf), a card elsewhere
    auto tsl = MakeConfig(MM_TSL);
    tsl->sound.covoxFB = 1;
    SlotManager::Result result = SlotManager::Plan(*tsl, kAllGroups);
    ASSERT_NE(result.FindBuiltIn("covox"), nullptr);
    EXPECT_EQ(result.FindBuiltIn("covox")->state, "active");
    EXPECT_EQ(result.FindBuiltIn("covox")->source, "[SOUND] CovoxFB=1");
    SlotManager::Apply(result, *tsl, kAllGroups);
    EXPECT_EQ(CovoxOf(*tsl), "mono");
    auto pentagon = MakeConfig(MM_PENTAGON);
    pentagon->sound.covoxFB = 1;
    pentagon->sound.sd = 1;
    result = SlotManager::Plan(*pentagon, kAllGroups);
    SlotManager::Apply(result, *pentagon, kAllGroups);
    EXPECT_EQ(CovoxOf(*pentagon), "quad") << "SD=1: the SounDrive card in the emulator decode (mode=both)";
}

/// Step 5 on a machine: the SounDrive mode reaches the Covox module's decode (~25 ms)
TEST(SlotManager_Test, SoundriveModeReachesTheCovox)
{
    StagedMachine m("pentagon128k", "zxbus.1 = soundrive\nzxbus.1.mode = 2");
    ASSERT_TRUE(m.Ok());
    ASSERT_TRUE(m.Context()->pSoundManager->hasCovox());
    EXPECT_EQ(m.Context()->pSoundManager->getCovox()->fitment(), Covox::Fitment::Mode2);
    EXPECT_FALSE(m.Context()->pSoundManager->getCovox()->tryClaimOut(0x001F, 0x55)) << "mode 1 port, card in mode 2";
    EXPECT_TRUE(m.Context()->pSoundManager->getCovox()->tryClaimOut(0x00F1, 0x55));
}

/// Step 6: the ZX-bus network cards come from the slots; NetworkManager keeps the virtual network (host access off
/// here: nothing leaves the process). Two machines (~25 ms each)
TEST(SlotManager_Test, NetworkCardsComeFromTheSlots)
{
    // Plan level: the ZX-Evo's own #EF port refuses the ZX-WiFi #EF build (it used to be a NetworkManager note)
    auto evo = MakeConfig(MM_ATM3);
    SetSlots(*evo, { { "zxbus.1", "zx-wifi" }, { "zxbus.2", "zxnetusb" } });
    SlotManager::Result result = SlotManager::Plan(*evo, kAllGroups);
    ASSERT_NE(Entry(result, "zxbus.1"), nullptr);
    EXPECT_TRUE(Entry(result, "zxbus.1")->entry.disabled);
    SlotManager::Apply(result, *evo, kAllGroups);
    EXPECT_EQ(evo->network.card, networkspec::kCardZxNetUsb);

    // The legacy key keeps ATM2IOESP (not a ZX-bus card: the ATM Turbo 2+ INTERNAL connector)
    auto atm = MakeConfig(MM_ATM710);
    atm->network.card = networkspec::kCardAtm2IoEsp | networkspec::kCardZxNetUsb;
    result = SlotManager::Plan(*atm, kAllGroups);
    SlotManager::Apply(result, *atm, kAllGroups);
    EXPECT_EQ(atm->network.card, networkspec::kCardAtm2IoEsp | networkspec::kCardZxNetUsb)
        << "ZXNETUSB behind the ATM CPU-socket adapter";

    {
        StagedMachine m("pentagon128k", "zxbus.1 = zxnetusb\n[NETWORK]\nHostAccess=0");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Context()->config.network.card, networkspec::kCardZxNetUsb);
        EXPECT_NE(m.Context()->pZxNetUsb, nullptr);
    }
    {
        StagedMachine m("pentagon128k", "zxbus.1 = neogs\n[NETWORK]\nHostAccess=0");
        ASSERT_TRUE(m.Ok());
        EXPECT_EQ(m.Context()->config.network.card, 0);
        EXPECT_EQ(m.Context()->pZxNetUsb, nullptr);
    }
}

TEST(SlotManager_Test, ReportListsSlotsAndBuiltIns)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());
    const StateNode report = DeviceState::Slots(emulator.GetContext());
    ASSERT_NE(report.find("available"), nullptr);
    EXPECT_TRUE(report.find("available")->b);
    EXPECT_EQ(report.find("model")->s, "PENTAGON");
    const StateNode* buses = report.find("buses");
    ASSERT_NE(buses, nullptr);
    bool retrofit = false;
    for (const StateNode& bus : buses->items)
    {
        retrofit = retrofit || (bus.find("id")->s == "zxbus" && bus.find("retrofit")->b);
    }
    EXPECT_TRUE(retrofit) << "the Pentagon 128's ZX-bus is bolted on, and the report says so (R-BUS-1a)";
    const StateNode* slotList = report.find("slots");
    ASSERT_NE(slotList, nullptr);
    ASSERT_FALSE(slotList->items.empty());
    EXPECT_EQ(slotList->items[0].find("slot")->s, "ay-socket");
    ASSERT_NE(report.find("builtIns"), nullptr);
    EXPECT_FALSE(report.find("builtIns")->items.empty());
    emulator.Release();
}

// endregion

TEST(SlotManagerShippedList_Test, ShippedConfigFoldersAreListed)
{
    std::vector<std::string> onDisk;
    for (const auto& entry : fs::directory_iterator(TestPathHelper::FindProjectRoot() / "data" / "configs"))
    {
        if (fs::exists(entry.path() / "unreal.ini"))
        {
            onDisk.push_back(entry.path().filename().string());
        }
    }
    std::sort(onDisk.begin(), onDisk.end());
    EXPECT_EQ(onDisk, ShippedConfigFolders());
}

INSTANTIATE_TEST_SUITE_P(Configs, SlotManagerShipped_Test, ::testing::ValuesIn(ShippedConfigFolders()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                             std::string name = info.param;
                             std::replace(name.begin(), name.end(), '-', 'X');
                             return name;
                         });

} // namespace
