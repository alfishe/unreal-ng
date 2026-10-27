// The switched-off symbol bundles remembered by the GUI (SymbolBundlePreferences): the Sets tab saves the switch of a
// bundle set in QSettings, and a later instance with the same bundle gets it switched off again; the core keeps nothing.

#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "debugger/labels/labelmanager.h"
#include "debugger/labels/symbolcontrol.h"
#include "debugger/symbolbundlepreferences.h"
#include "debugger/symbolsetspanel.h"
#include "emulator/emulatorcontext.h"

namespace
{
const char* const kPage = "00000000000000000000000000000000000000000000000000000000000000aa";

/// A bundle folder with one bundle "test:rom" matching kPage
void WriteBundle(const QTemporaryDir& folder)
{
    std::ofstream(folder.filePath("manifest.json").toStdString(), std::ios::binary)
        << R"({"format":"unreal-symbols-manifest","version":1,"bundles":[)"
        << R"({"id":"test:rom","title":"Test ROM","file":"test.sym","match":{"page_sha256":[")" << kPage << R"("]}}]})";
    std::ofstream(folder.filePath("test.sym").toStdString(), std::ios::binary) << "0038 MASKINT\n";
}

bool Enabled(LabelManager& labels, const std::string& id)
{
    const SymbolReply reply = SymbolControl(&labels).Execute({"sets", {}});
    for (const StateNode& set : reply.body.find("sets")->items)
        if (set.find("id")->s == id)
            return set.find("enabled")->b;
    ADD_FAILURE() << "no set " << id;
    return false;
}
}  // namespace

TEST(SymbolBundlePreferences_Test, ABundleSwitchedOffStaysOffInTheNextInstance)
{
    QTemporaryDir settingsFolder;
    QTemporaryDir bundles;
    ASSERT_TRUE(settingsFolder.isValid() && bundles.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsFolder.path());
    WriteBundle(bundles);

    {
        EmulatorContext context(LoggerLevel::LogError);
        LabelManager labels(&context);
        ASSERT_EQ(labels.ApplyBundles(bundles.path().toStdString(), {kPage}).size(), 1u);
        EXPECT_EQ(SymbolBundlePreferences::ApplySaved(&labels), 0) << "nothing remembered yet";

        SymbolSetsPanel panel(&labels);
        auto* table = panel.findChild<QTableWidget*>();
        ASSERT_NE(table, nullptr);
        ASSERT_EQ(table->rowCount(), 1);
        ASSERT_EQ(table->item(0, 2)->text(), "bundle:test:rom");
        table->item(0, 0)->setCheckState(Qt::Unchecked);
        EXPECT_FALSE(Enabled(labels, "bundle:test:rom"));
        EXPECT_EQ(SymbolBundlePreferences::Disabled(), QStringList{"bundle:test:rom"});
    }

    // The next session: the core makes the bundle on, the GUI switches it off
    {
        EmulatorContext context(LoggerLevel::LogError);
        LabelManager labels(&context);
        ASSERT_EQ(labels.ApplyBundles(bundles.path().toStdString(), {kPage}).size(), 1u);
        EXPECT_TRUE(Enabled(labels, "bundle:test:rom"));
        EXPECT_EQ(SymbolBundlePreferences::ApplySaved(&labels), 1);
        EXPECT_FALSE(Enabled(labels, "bundle:test:rom"));
        EXPECT_EQ(SymbolBundlePreferences::ApplySaved(&labels), 0) << "already off";

        // Switched on again in the Sets tab: forgotten
        SymbolSetsPanel panel(&labels);
        panel.findChild<QTableWidget*>()->item(0, 0)->setCheckState(Qt::Checked);
        EXPECT_TRUE(SymbolBundlePreferences::Disabled().isEmpty());
    }
}

TEST(SymbolBundlePreferences_Test, OnlyBundleSetsAreRemembered)
{
    QTemporaryDir settingsFolder;
    ASSERT_TRUE(settingsFolder.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsFolder.path());

    SymbolBundlePreferences::Save("file:/x/game.sym", false);
    SymbolBundlePreferences::Save("user", false);
    EXPECT_TRUE(SymbolBundlePreferences::Disabled().isEmpty());
    SymbolBundlePreferences::Save("bundle:rom:48k", false);
    SymbolBundlePreferences::Save("bundle:rom:48k", false);
    EXPECT_EQ(SymbolBundlePreferences::Disabled(), QStringList{"bundle:rom:48k"}) << "once";
}
