// SlotManager and time travel (core/src/emulator/slots/slotttd.cpp; ZX-bus slots SL-5,
// docs/inprogress/2026-10-03-zx-bus-slots/tdd.md §2.4, architecture.md §8): the slot set in the session's
// configuration fingerprint, the slot-set guard on a session load, slot cards keeping their blob ids, device
// instances named by slot, two instances of one module refused by the v1 registry, no slot change while recording.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/engine/ttdconfigfingerprint.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdconfigcapture.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/soundmanager.h"

namespace fs = std::filesystem;
using ttd::PeripheralId;

namespace
{

/// A config as the parser leaves it for a model, with [SLOTS] parsed and projected (no emulator)
std::unique_ptr<CONFIG> PlannedConfig(MEM_MODEL model, std::initializer_list<std::pair<std::string, std::string>> lines)
{
    auto config = std::make_unique<CONFIG>();
    config->mem_model = model;
    config->sound.turboSoundKind = TurboSoundKind::None;
    config->sound.gsTypeKind = GSTypeKind::NONE;
    config->sound.gsRamKB = 128;
    config->ngs.ramKB = 2048;
    ParseSlotsSection(std::vector<std::pair<std::string, std::string>>(lines), config->slotConfig);
    SlotManager::Project(config->slotConfig, *config);
    return config;
}

SlotManager::Result PlanOf(MEM_MODEL model, std::initializer_list<std::pair<std::string, std::string>> lines)
{
    return SlotManager::Plan(*PlannedConfig(model, lines));
}

/// The fingerprint fields as a fingerprint
ttd::TTDConfigFingerprint FingerprintOf(const SlotManager::Result& result)
{
    ttd::TTDConfigFingerprint fp;
    for (const auto& [name, value] : SlotManager::TtdFingerprintFields(result))
        fp.Add(name, value, true);
    return fp;
}

std::vector<std::string> DiffFields(const ttd::TTDConfigFingerprint& a, const ttd::TTDConfigFingerprint& b)
{
    std::vector<std::string> names;
    for (const ttd::TTDFingerprintDiff& d : ttd::Compare(a, b))
        names.push_back(d.field);
    return names;
}

SlotManager::TtdDeviceSet Devices(std::initializer_list<PeripheralId> ids, uint64_t notRecorded = 0)
{
    SlotManager::TtdDeviceSet set;
    for (PeripheralId id : ids)
        set.ids.push_back(static_cast<uint8_t>(id));
    set.notRecorded = notRecorded;
    return set;
}

uint64_t Bit(PeripheralId id)
{
    return uint64_t(1) << static_cast<uint8_t>(id);
}

/// A shipped config's machine with its [SLOTS] replaced, every sound card as configured, time travel available
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
        _path = TestPathHelper::GetUniqueTestScratchPath("slotttd-" + folder + ".ini");
        std::ofstream out(_path, std::ios::binary);
        out << text;
        out.close();
        _emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
        _emulator->SetCustomConfigPath(_path.string());
        _ok = _emulator->Init();
        if (_ok)
        {
            _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
            _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
            _emulator->GetContext()->pMemory->UpdateFeatureCache();
        }
    }
    ~StagedMachine()
    {
        _emulator->Release();
        std::error_code ignored;
        fs::remove(_path, ignored);
    }
    bool Ok() const { return _ok; }
    Emulator* Get() const { return _emulator.get(); }
    EmulatorContext* Context() const { return _emulator->GetContext(); }
    ttd::TimeTravelManager* Ttd() const { return _emulator->GetContext()->pTimeTravelManager; }

private:
    SoundCardScope _everySound;
    fs::path _path;
    std::unique_ptr<Emulator> _emulator;
    bool _ok = false;
};

/// A registry stand-in for one chip module (the SAA1099 two cards could carry)
class FakeModule final : public ttd::TTDSerializable
{
public:
    explicit FakeModule(PeripheralId id) : _id(id) {}
    size_t TTDStateSize() const override { return 4; }
    void TTDSaveState(uint8_t* dst) const override { std::memset(dst, 0x5A, 4); }
    void TTDLoadState(const uint8_t*) override {}
    std::string TTDDeviceName() const override { return "Saa1099"; }
    PeripheralId TTDPeripheralId() const override { return _id; }

private:
    PeripheralId _id;
};

}  // namespace

/// R-NF-2: every fitted card (with its options and adapter) and every switchable built-in is a fingerprint field
/// that affects a restore; a disabled card is not fitted. Pure plans, then one live machine pair (~40 ms)
TEST(TtdSlots_Test, FingerprintHasSlotSet)
{
    const SlotManager::Result base =
        PlanOf(MM_PENTAGON, {{"ay-socket", "tsfm"}, {"zxbus.1", "gs"}, {"zxbus.1.ram", "512k"}, {"zxbus.2", "moonsound"}});
    const ttd::TTDConfigFingerprint fp = FingerprintOf(base);
    for (const char* field : {"slots.ay-socket", "slots.zxbus.1", "slots.zxbus.2"})
        EXPECT_NE(fp.Find(field), nullptr) << field;

    // An option of one card: that slot's field only
    const ttd::TTDConfigFingerprint ram =
        FingerprintOf(PlanOf(MM_PENTAGON, {{"ay-socket", "tsfm"}, {"zxbus.1", "gs"}, {"zxbus.1.ram", "256k"},
                                           {"zxbus.2", "moonsound"}}));
    EXPECT_EQ(DiffFields(fp, ram), std::vector<std::string>{"slots.zxbus.1"});
    // Another card in a slot, a card moved to another slot
    const ttd::TTDConfigFingerprint moved =
        FingerprintOf(PlanOf(MM_PENTAGON, {{"ay-socket", "ay"}, {"zxbus.1", "gs"}, {"zxbus.1.ram", "512k"},
                                           {"zxbus.3", "moonsound"}}));
    EXPECT_EQ(DiffFields(fp, moved), (std::vector<std::string>{"slots.ay-socket", "slots.zxbus.2", "slots.zxbus.3"}));

    // A card disabled by the plan (the later of two GS cards) is not part of the slot set
    const SlotManager::Result clash = PlanOf(MM_PENTAGON, {{"zxbus.1", "gs"}, {"zxbus.2", "neogs"}});
    EXPECT_NE(FingerprintOf(clash).Find("slots.zxbus.1"), nullptr);
    EXPECT_EQ(FingerprintOf(clash).Find("slots.zxbus.2"), nullptr);

    // A switchable built-in (the ZX-Evo's board Covox)
    const ttd::TTDConfigFingerprint covoxOn = FingerprintOf(PlanOf(MM_ATM3, {{"builtin.covox", "on"}}));
    const ttd::TTDConfigFingerprint covoxOff = FingerprintOf(PlanOf(MM_ATM3, {{"builtin.covox", "off"}}));
    ASSERT_NE(covoxOn.Find("slots.builtin.covox"), nullptr);
    EXPECT_EQ(covoxOn.Find("slots.builtin.covox")->value, 1u);
    EXPECT_EQ(DiffFields(covoxOn, covoxOff), std::vector<std::string>{"slots.builtin.covox"});

    // The live machine's fingerprint carries them, each affecting a restore
    StagedMachine a("pentagon128k", "ay-socket = tsfm\nzxbus.1 = gs\nzxbus.1.ram = 512k");
    StagedMachine b("pentagon128k", "ay-socket = tsfm\nzxbus.1 = gs\nzxbus.1.ram = 256k");
    ASSERT_TRUE(a.Ok() && b.Ok());
    const ttd::TTDConfigFingerprint liveA = ttd::CaptureConfigFingerprint(*a.Context(), 0);
    const ttd::TTDConfigFingerprint liveB = ttd::CaptureConfigFingerprint(*b.Context(), 0);
    ASSERT_NE(liveA.Find("slots.zxbus.1"), nullptr);
    EXPECT_TRUE(liveA.Find("slots.zxbus.1")->affectsRestore);
    // The GS RAM also had a field of its own before the slots (sound.gs_ram_kb, replay only)
    const std::vector<ttd::TTDFingerprintDiff> diffs = ttd::Compare(liveA, liveB);
    const auto slot = std::find_if(diffs.begin(), diffs.end(),
                                   [](const ttd::TTDFingerprintDiff& d) { return d.field == "slots.zxbus.1"; });
    ASSERT_NE(slot, diffs.end());
    EXPECT_TRUE(slot->affectsRestore);
    for (const ttd::TTDFingerprintDiff& d : diffs)
        EXPECT_TRUE(d.field == "slots.zxbus.1" || d.field == "sound.gs_ram_kb") << d.field;
}

/// The guard's decision per position: the AY socket's two kinds both ways, an empty socket, a session carrying both
/// socket ids (no healthy writer stores that), the General Sound personality (the lightweight card is fitted but not
/// recorded), MoonSound present on one side only. Every difference listed, each under the plan's slot
TEST(TtdSlots_Test, SessionMismatchListsEveryDifference)
{
    const SlotManager::Result plan =
        PlanOf(MM_PENTAGON, {{"ay-socket", "tsfm"}, {"zxbus.1", "neogs"}, {"zxbus.2", "moonsound"}});
    const auto live = Devices({PeripheralId::TSFM, PeripheralId::NeoGS, PeripheralId::MoonSound, PeripheralId::Covox});
    std::string why;
    EXPECT_TRUE(SlotManager::TtdSlotSetMatches(plan, live, live, why)) << why;

    why.clear();
    EXPECT_FALSE(SlotManager::TtdSlotSetMatches(
        plan, Devices({PeripheralId::TurboSound, PeripheralId::GeneralSound, PeripheralId::Covox}), live, why));
    EXPECT_NE(why.find("ay-socket: recorded ay / ts, this machine tsfm"), std::string::npos) << why;
    EXPECT_NE(why.find("zxbus.1: recorded gs, this machine neogs"), std::string::npos) << why;
    EXPECT_NE(why.find("zxbus.2: recorded none, this machine moonsound"), std::string::npos) << why;

    // The lightweight GS: named by the not-recorded mask, not by a blob
    why.clear();
    EXPECT_FALSE(SlotManager::TtdSlotSetMatches(
        plan, Devices({PeripheralId::TSFM, PeripheralId::MoonSound}, Bit(PeripheralId::GeneralSoundLightweight)), live,
        why));
    EXPECT_NE(why.find("zxbus.1: recorded gs-lw, this machine neogs"), std::string::npos) << why;

    // An empty socket on this machine, a session with a socket device: the AY ports answered there, not here
    const auto emptySocket = Devices({PeripheralId::NeoGS, PeripheralId::MoonSound});
    why.clear();
    EXPECT_FALSE(SlotManager::TtdSlotSetMatches(plan, live, emptySocket, why));
    EXPECT_NE(why.find("ay-socket: recorded tsfm, this machine none"), std::string::npos) << why;

    // Both socket ids in one session: refused, not guessed
    why.clear();
    EXPECT_FALSE(SlotManager::TtdSlotSetMatches(
        plan, Devices({PeripheralId::TurboSound, PeripheralId::TSFM, PeripheralId::NeoGS, PeripheralId::MoonSound}),
        live, why));
    EXPECT_NE(why.find("recorded ay / ts + tsfm"), std::string::npos) << why;

    // Without a plan (a model with no slot declaration) the positions keep their names
    why.clear();
    EXPECT_FALSE(SlotManager::TtdSlotSetMatches({}, Devices({PeripheralId::GeneralSound}), Devices({}), why));
    EXPECT_NE(why.find("General Sound card: recorded gs, this machine none"), std::string::npos) << why;
}

/// tdd.md §2.4: a session recorded with one slot set, loaded into a machine with another, is refused with the
/// difference listed; the same slot set loads. Three machines and a 3-frame recording (~70 ms: machine creation
/// with NeoGS and MoonSound fitted dominates)
TEST(TtdSlots_Test, SessionMismatchRefused)
{
    std::stringstream session;
    {
        StagedMachine recorder("pentagon128k", "ay-socket = tsfm\nzxbus.1 = gs");
        ASSERT_TRUE(recorder.Ok());
        ASSERT_TRUE(recorder.Ttd()->StartRecording());
        EmulatorTestHelper::RunFramesFast(recorder.Get(), 3);
        recorder.Ttd()->StopRecording();
        std::string err;
        ASSERT_TRUE(recorder.Ttd()->SerializeSession(session, err)) << err;
    }

    {
        StagedMachine other("pentagon128k", "ay-socket = ay\nzxbus.1 = neogs\nzxbus.2 = moonsound");
        ASSERT_TRUE(other.Ok());
        session.seekg(0);
        std::string err;
        EXPECT_FALSE(other.Ttd()->DeserializeSession(session, err));
        EXPECT_NE(err.find("slot set differs from the recording"), std::string::npos) << err;
        EXPECT_NE(err.find("ay-socket: recorded tsfm, this machine ay / ts"), std::string::npos) << err;
        EXPECT_NE(err.find("zxbus.1: recorded gs, this machine neogs"), std::string::npos) << err;
        EXPECT_NE(err.find("zxbus.2: recorded none, this machine moonsound"), std::string::npos) << err;
    }
    {
        // The same cards in other slot numbers are still the same devices for a v1 file (one blob per id); the
        // engine's fingerprint and device keys tell the slots apart
        StagedMachine same("pentagon128k", "ay-socket = tsfm\nzxbus.1 = gs");
        ASSERT_TRUE(same.Ok());
        session.clear();
        session.seekg(0);
        std::string err;
        EXPECT_TRUE(same.Ttd()->DeserializeSession(session, err)) << err;
    }
}

/// tdd.md §2.4: the cards that moved onto slots in SL-4 keep the blob ids and layouts the corpus was recorded with.
/// The Pentagon fixture with TurboSound FM and the classic GS (testdata/ttd, recorded before the slots) loads into a
/// machine planned from [SLOTS]; its slot cards' blobs carry the ids the live devices register under, each of the
/// live device's state size. The engine names them by slot. ~40 ms: one machine and a real 300-frame fixture
TEST(TtdSlots_Test, MigratedCardsKeepBlobIds)
{
    const fs::path fixture = TestPathHelper::FindProjectRoot() / "testdata/ttd/tsfm_tech_support.ttd";
    ttd::TTDFileInfo info;
    std::string err;
    ASSERT_TRUE(ttd::ReadTTDFileInfo(fixture.string(), info, err)) << err;
    const uint64_t mask = info.machine.peripheralMask;
    ASSERT_TRUE(mask & Bit(PeripheralId::TSFM)) << "the fixture records the TurboSound FM under id 4";
    ASSERT_TRUE(mask & Bit(PeripheralId::GeneralSound)) << "the fixture records the classic GS under id 5";

    std::string slots = "ay-socket = tsfm\nzxbus.1 = gs";
    if (mask & Bit(PeripheralId::MoonSound))
        slots += "\nzxbus.2 = moonsound";
    StagedMachine machine("pentagon128k", slots);
    ASSERT_TRUE(machine.Ok());
    machine.Get()->GetFeatureManager()->setFeature(Features::kSoundHQ, true);
    machine.Get()->GetFeatureManager()->setFeature(Features::kScreenHQ, true);

    std::ifstream in(fixture, std::ios::binary);
    ASSERT_TRUE(machine.Ttd()->DeserializeSession(in, err)) << err;
    const ttd::TTDCheckpoint* baseline = machine.Ttd()->GetCheckpoint(0);
    ASSERT_NE(baseline, nullptr);

    const ttd::TTDPeripheralRegistry& registry = machine.Ttd()->GetPeripheralRegistry();
    const SlotManager* slotManager = machine.Context()->pSlotManager;
    ASSERT_NE(slotManager, nullptr);
    struct Card
    {
        PeripheralId id;
        SlotCardGroup group;
        const char* module;
    };
    std::vector<Card> cards = {{PeripheralId::TSFM, SlotCardGroup::Socket, "tsfm"},
                               {PeripheralId::GeneralSound, SlotCardGroup::GeneralSound, "generalsound"}};
    if (mask & Bit(PeripheralId::MoonSound))
        cards.push_back({PeripheralId::MoonSound, SlotCardGroup::MoonSound, "moonsound"});
    const std::vector<ttd::TTDDeviceEntry> entries = registry.DeviceEntries();
    for (const Card& card : cards)
    {
        const uint8_t id = static_cast<uint8_t>(card.id);
        ttd::TTDSerializable* device = registry.GetDevice(card.id);
        ASSERT_NE(device, nullptr) << "id " << int(id);
        const auto blob = baseline->peripheralBlobs.find(id);
        ASSERT_NE(blob, baseline->peripheralBlobs.end()) << "id " << int(id);
        const std::vector<uint8_t> state = ttd::TTDPeripheralRegistry::DecodeBlob(id, blob->second);
        EXPECT_EQ(state.size(), device->TTDStateSize()) << device->TTDDeviceName() << ": layout changed";

        // The engine's device key: the module named by its slot
        const std::string expected = slotManager->TtdInstance(card.group, device->TTDDeviceName());
        EXPECT_FALSE(expected.empty()) << device->TTDDeviceName();
        const auto entry = std::find_if(entries.begin(), entries.end(),
                                        [&](const ttd::TTDDeviceEntry& e) { return e.descriptor.legacyId == card.id; });
        ASSERT_NE(entry, entries.end());
        EXPECT_EQ(entry->descriptor.instance, expected);
        EXPECT_EQ(entry->descriptor.instance.rfind('.'), entry->descriptor.instance.size() - std::strlen(card.module) - 1)
            << entry->descriptor.instance;
    }
}

/// Item 4 (architecture.md §8): two cards carrying one module type. The engine's device table keys devices by
/// {type, instance}, the instance named by slot ("zxbus.1.saa"); the v1 registry is keyed by the blob id alone and
/// used to let the second device silently replace the first. It now refuses the second and names both, so recording
/// is refused instead of losing one card's state. No card pair of the catalog fits two of one module today (the
/// MultiSound is not emulated yet), hence the registry-level test
TEST(TtdSlots_Test, TwoInstancesOfOneModuleAreRefusedByName)
{
    FakeModule first(PeripheralId::Saa1099);
    FakeModule second(PeripheralId::Saa1099);
    ttd::TTDPeripheralRegistry registry;
    EXPECT_TRUE(registry.Register(PeripheralId::Saa1099, &first, "zxbus.1.saa"));
    EXPECT_FALSE(registry.Register(PeripheralId::Saa1099, &second, "zxbus.2.saa"));
    EXPECT_EQ(registry.GetDevice(PeripheralId::Saa1099), &first) << "the first stays";

    const std::vector<ttd::TTDDeviceEntry> entries = registry.DeviceEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].descriptor.instance, "zxbus.1.saa");

    std::string error;
    EXPECT_FALSE(registry.CheckDeviceTable(error));
    EXPECT_NE(error.find("zxbus.1.saa and zxbus.2.saa"), std::string::npos) << error;

    // Registering the same device again is no duplicate; a cleared registry forgets the refusal
    EXPECT_TRUE(registry.Register(PeripheralId::Saa1099, &first, "zxbus.1.saa"));
    registry.Clear();
    EXPECT_TRUE(registry.Register(PeripheralId::Saa1099, &second, "zxbus.2.saa"));
    EXPECT_TRUE(registry.CheckDeviceTable(error)) << error;
    EXPECT_EQ(registry.DeviceEntries()[0].descriptor.instance, "zxbus.2.saa");
}

/// The registry checked against the plan: a card the plan fits has its device and the other way round; the socket's
/// board decides the blob id, the General Sound card's personality too (SL-6: the runtime switch moves the plan)
TEST(TtdSlots_Test, RegistryFollowsThePlan)
{
    const SlotManager::Result plan =
        PlanOf(MM_PENTAGON, {{"ay-socket", "tsfm"}, {"zxbus.1", "gs"}, {"zxbus.2", "moonsound"}});
    std::string why;
    EXPECT_TRUE(SlotManager::TtdDevicesMatchPlan(
        plan, Devices({PeripheralId::TSFM, PeripheralId::GeneralSound, PeripheralId::MoonSound}), why))
        << why;
    // SL-6: the General Sound personality is the plan's card (a runtime switch moves the plan with it)
    why.clear();
    EXPECT_FALSE(SlotManager::TtdDevicesMatchPlan(
        plan, Devices({PeripheralId::TSFM, PeripheralId::MoonSound}, Bit(PeripheralId::GeneralSoundLightweight)), why));
    EXPECT_NE(why.find("zxbus.1: the plan fits gs, the device is gs-lw"), std::string::npos) << why;
    const SlotManager::Result lightweight =
        PlanOf(MM_PENTAGON, {{"ay-socket", "tsfm"}, {"zxbus.1", "gs-lw"}, {"zxbus.2", "moonsound"}});
    EXPECT_TRUE(SlotManager::TtdDevicesMatchPlan(
        lightweight, Devices({PeripheralId::TSFM, PeripheralId::MoonSound}, Bit(PeripheralId::GeneralSoundLightweight)),
        why))
        << why;

    why.clear();
    EXPECT_FALSE(SlotManager::TtdDevicesMatchPlan(plan, Devices({PeripheralId::TurboSound, PeripheralId::GeneralSound}),
                                                  why));
    EXPECT_NE(why.find("ay-socket: the plan fits tsfm, the device is ay / ts"), std::string::npos) << why;
    EXPECT_NE(why.find("zxbus.2: the plan fits moonsound, the device is none"), std::string::npos) << why;

    const SlotManager::Result empty = PlanOf(MM_PENTAGON, {{"ay-socket", "none"}});
    why.clear();
    EXPECT_FALSE(SlotManager::TtdDevicesMatchPlan(empty, Devices({PeripheralId::TurboSound, PeripheralId::NeoGS}), why));
    EXPECT_NE(why.find("ay-socket: the plan fits none, the device is ay / ts"), std::string::npos) << why;
    EXPECT_NE(why.find("General Sound card: the plan fits none, the device is neogs"), std::string::npos) << why;
}

/// SL-6: the frame-boundary personality switch of a running machine moves the plan's GS slot and the TTD fingerprint
/// with it (the fingerprint a machine created with that card has), so a recording after the switch registers against
/// a plan naming the active card; a switch the plan refuses is refused at the request. One machine (~20 ms)
TEST(TtdSlots_Test, RuntimePersonalitySwitchMovesThePlanAndTheFingerprint)
{
    StagedMachine machine("pentagon128k", "zxbus.1 = gs\nzxbus.1.ram = 128k");
    ASSERT_TRUE(machine.Ok());
    SlotManager* slotManager = machine.Context()->pSlotManager;
    SoundManager* sound = machine.Context()->pSoundManager;
    ASSERT_NE(sound->getGeneralSound(), nullptr);
    ttd::TTDConfigFingerprint before;
    slotManager->AddTtdFingerprint(before);

    EXPECT_EQ(slotManager->GeneralSoundSwitchRefusal(GSTypeKind::NGS), "");
    ASSERT_TRUE(sound->switchGeneralSoundCard(GSTypeKind::NGS));
    const SlotManager::Slot* gs = slotManager->Current().FindSlot("zxbus.1");
    ASSERT_NE(gs, nullptr);
    EXPECT_EQ(gs->entry.card, "neogs");

    ttd::TTDConfigFingerprint after;
    slotManager->AddTtdFingerprint(after);
    EXPECT_EQ(DiffFields(before, after), std::vector<std::string>{"slots.zxbus.1"});
    const SlotManager::Result created = PlanOf(MM_PENTAGON, {{"zxbus.1", "neogs"}, {"zxbus.1.ram", "2m"}});
    EXPECT_TRUE(DiffFields(after, FingerprintOf(created)).empty()) << "as if the machine had been created with it";

    // Recording registers the NeoGS against a plan that names it
    ASSERT_TRUE(machine.Ttd()->StartRecording());
    machine.Ttd()->StopRecording();

    // No General Sound card fitted: nothing to switch
    StagedMachine bare("pentagon128k", "zxbus.1 = zxnetusb");
    ASSERT_TRUE(bare.Ok());
    EXPECT_NE(bare.Context()->pSlotManager->GeneralSoundSwitchRefusal(GSTypeKind::NGS).find("no General Sound card"),
              std::string::npos);
}

/// R-OP-7: no slot change while a user recording runs, the refusal names the session; allowed once it stopped.
/// One machine (~20 ms)
TEST(TtdSlots_Test, RefusedWhileTtdRecords)
{
    StagedMachine machine("pentagon128k", "zxbus.1 = gs");
    ASSERT_TRUE(machine.Ok());
    const SlotManager* slotManager = machine.Context()->pSlotManager;
    ASSERT_NE(slotManager, nullptr);
    EXPECT_EQ(slotManager->ChangeRefusal(), "");

    ASSERT_TRUE(machine.Ttd()->StartRecording());
    const std::string refusal = slotManager->ChangeRefusal();
    EXPECT_NE(refusal.find("Cannot change the slot set while TTD is recording session #1"), std::string::npos)
        << refusal;
    machine.Ttd()->StopRecording();
    EXPECT_EQ(slotManager->ChangeRefusal(), "");

    ASSERT_TRUE(machine.Ttd()->StartRecording());
    EXPECT_NE(slotManager->ChangeRefusal().find("session #2"), std::string::npos) << "a new recording, a new session";
    machine.Ttd()->StopRecording();
}
