#include "stdafx.h"
#include "pch.h"

#include <cctype>
#include <set>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/state/devicecatalog.h"

/// The device catalog: the reports a machine has, the list a debugger's device view and every plane read

namespace
{
std::set<std::string> Ids(const StateNode& catalog)
{
    std::set<std::string> ids;
    if (const StateNode* reports = catalog.find("reports"))
        for (const StateNode& r : reports->items)
            ids.insert(r.find("id")->s);
    return ids;
}

const StateNode* Report(const StateNode& catalog, const std::string& id)
{
    for (const StateNode& r : catalog.find("reports")->items)
        if (r.find("id")->s == id)
            return &r;
    return nullptr;
}
}  // namespace

TEST(DeviceCatalog_Test, PentagonWithTurboSoundFmAndNoGeneralSound)
{
    // The standard Pentagon config has TurboSound=FM; the runner leaves the GS slot empty without a scope for it
    SoundCardScope cards(TestSound::TurboSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    const StateNode catalog = DeviceState::Catalog(emulator->GetContext());
    EmulatorTestHelper::CleanupEmulator(emulator);

    ASSERT_TRUE(catalog.find("available")->b);
    const std::set<std::string> ids = Ids(catalog);
    for (const char* id : {"execution", "screen", "slots", "ay", "fm", "fdc"})
        EXPECT_EQ(ids.count(id), 1u) << id;
    for (const char* id : {"gs", "moonsound", "next", "sprinter", "tsconf", "profi", "isa"})
        EXPECT_EQ(ids.count(id), 0u) << id << " is not on this machine";

    const StateNode* ay = Report(catalog, "ay");
    ASSERT_NE(ay, nullptr);
    EXPECT_NE(ay->find("summary")->s.find("TurboSound FM"), std::string::npos) << ay->find("summary")->s;
    EXPECT_EQ(ay->find("route")->s, "state/audio/ay");
    EXPECT_EQ(ay->find("cli")->s, "state audio ay");
    EXPECT_EQ(ay->find("script")->s, "audio_ay_state");
    EXPECT_EQ(ay->find("mcp")->s, "audio_ay");
}

TEST(DeviceCatalog_Test, GeneralSoundAppearsWithTheCard)
{
    SoundCardScope cards(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    const StateNode catalog = DeviceState::Catalog(emulator->GetContext());
    EmulatorTestHelper::CleanupEmulator(emulator);

    EXPECT_EQ(Ids(catalog).count("gs"), 1u);
}

TEST(DeviceCatalog_Test, AnEmptyAySocketIsNoDevice)
{
    // The runner leaves the TurboSound slot empty: the AY report is available with no chip, the catalog leaves it out
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    const StateNode catalog = DeviceState::Catalog(emulator->GetContext());
    const StateNode all = DeviceState::Catalog(emulator->GetContext(), true);
    EmulatorTestHelper::CleanupEmulator(emulator);

    EXPECT_EQ(Ids(catalog).count("ay"), 0u);
    const StateNode* ay = Report(all, "ay");
    ASSERT_NE(ay, nullptr);
    EXPECT_FALSE(ay->find("available")->b);
    EXPECT_EQ(ay->find("description")->s, "not on this machine");
}

TEST(DeviceCatalog_Test, TheAllFormSaysWhyEachIsAbsent)
{
    SoundCardScope cards(TestSound::TurboSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    const StateNode catalog = DeviceState::Catalog(emulator->GetContext());
    const StateNode all = DeviceState::Catalog(emulator->GetContext(), true);
    EmulatorTestHelper::CleanupEmulator(emulator);

    // Every entry of the table, in its order; the present ones are exactly the short form's
    ASSERT_EQ(all.find("reports")->size(), DeviceState::CatalogEntries().size());
    std::set<std::string> present;
    for (size_t i = 0; i < all.find("reports")->items.size(); i++)
    {
        const StateNode& r = all.find("reports")->items[i];
        EXPECT_EQ(r.find("id")->s, DeviceState::CatalogEntries()[i].id);
        if (r.find("available")->b)
            present.insert(r.find("id")->s);
        else
            EXPECT_FALSE(r.find("description")->s.empty()) << r.find("id")->s << " says why";
    }
    EXPECT_EQ(present, Ids(catalog));
    EXPECT_NE(Report(all, "gs")->find("description")->s.find("not fitted"), std::string::npos);
    EXPECT_NE(Report(all, "next")->find("description")->s.find("Next"), std::string::npos);

    const std::string text = DeviceState::CatalogText(all);
    EXPECT_NE(text.find("absent: General Sound card not fitted"), std::string::npos) << text;
}

/// Every model the test tree can boot: the listed reports are exactly the available ones, the ids unique
class DeviceCatalogModels_Test : public ::testing::TestWithParam<const char*>
{
};

TEST_P(DeviceCatalogModels_Test, ListsExactlyThePresentReports)
{
    SoundCardScope cards(TestSound::All);  // every card the model's configuration fits
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(GetParam(), LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr) << GetParam();
    EmulatorContext* context = emulator->GetContext();
    const StateNode catalog = DeviceState::Catalog(context);
    const std::set<std::string> listed = Ids(catalog);
    std::set<std::string> seen;
    for (const DeviceState::ReportEntry& e : DeviceState::CatalogEntries())
    {
        EXPECT_TRUE(seen.insert(e.id).second) << "id twice: " << e.id;
        const StateNode report = e.build(context);
        const StateNode* available = report.find("available");
        const bool on = available != nullptr && available->b && (e.present == nullptr || e.present(report));
        EXPECT_EQ(listed.count(e.id) == 1, on) << GetParam() << ": " << e.id;
    }
    for (const char* always : {"execution", "screen", "slots"})
        EXPECT_EQ(listed.count(always), 1u) << GetParam() << ": " << always;
    EmulatorTestHelper::CleanupEmulator(emulator);
}

INSTANTIATE_TEST_SUITE_P(Models, DeviceCatalogModels_Test,
                         ::testing::Values("48K", "128K", "PENTAGON", "SCORPION", "PROFSCORP", "TSCONF", "SPRINTER",
                                           "NEXT"),
                         [](const ::testing::TestParamInfo<const char*>& info) {
                             std::string name = info.param;
                             for (char& c : name)
                                 if (!std::isalnum(static_cast<unsigned char>(c)))
                                     c = '_';
                             return name;
                         });

TEST(DeviceCatalog_Test, WithoutAMachineSaysSo)
{
    const StateNode catalog = DeviceState::Catalog(nullptr);
    EXPECT_FALSE(catalog.find("available")->b);
    EXPECT_EQ(DeviceState::CatalogText(catalog), "unavailable: No machine\n");
}

TEST(DeviceCatalog_Test, TextNamesEachReportAndItsCommand)
{
    StateNode catalog = StateNode::Object();
    catalog["available"] = true;
    StateNode reports = StateNode::Array();
    StateNode ay = StateNode::Object();
    ay["id"] = "ay";
    ay["title"] = "AY sound chips";
    ay["summary"] = "TurboSound FM";
    ay["cli"] = "state audio ay";
    reports.push(ay);
    StateNode fdc = StateNode::Object();
    fdc["id"] = "fdc";
    fdc["title"] = "Disk controller";
    fdc["summary"] = "";
    fdc["cli"] = "state fdc";
    reports.push(fdc);
    catalog["reports"] = reports;

    EXPECT_EQ(DeviceState::CatalogText(catalog),
              "ay   AY sound chips   TurboSound FM  [state audio ay]\n"
              "fdc  Disk controller  [state fdc]\n");
}
