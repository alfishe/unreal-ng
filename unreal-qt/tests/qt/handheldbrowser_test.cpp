// The handheld menu's file browser (HandheldBrowser): a slot that takes a host folder (an SD card, a hard disk) is
// offered the folder shown as well as its files. Without it the menu could not put a folder on a card: R-Type VDAC2
// reads its .PAC files from the SD card its own folder is.

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "handheld/handheldbrowser.h"

namespace
{
/// A game folder with a program in it, where the browser opens next (the last folder it was used in)
QString GameFolder(const QTemporaryDir& root)
{
    QDir(root.path()).mkdir(QStringLiteral("R-Type"));
    const QString folder = QDir(root.path()).filePath(QStringLiteral("R-Type"));
    QFile program(QDir(folder).filePath(QStringLiteral("rtype.spg")));
    program.open(QIODevice::WriteOnly);
    program.write("SpectrumProg");
    program.close();
    QSettings settings;
    settings.setValue(QStringLiteral("Handheld/lastFolder"), folder);
    settings.setValue(QStringLiteral("Handheld/lastRoot"), root.path());
    settings.remove(QStringLiteral("Handheld/lastFile"));
    return QDir(folder).absolutePath();
}
}  // namespace

TEST(HandheldBrowser_Test, AFolderSlotIsOfferedTheFolderShown)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString folder = GameFolder(root);

    HandheldBrowser browser;
    browser.Open(HandheldBrowser::Filter::All, true);
    ASSERT_GE(browser.entries().size(), 3u);
    EXPECT_EQ(browser.entries()[0].kind, HandheldBrowser::Kind::Up);
    EXPECT_EQ(browser.entries()[1].kind, HandheldBrowser::Kind::ThisFolder);
    EXPECT_EQ(browser.entries()[1].path, folder);
    EXPECT_EQ(browser.Activate(1), folder) << "the folder itself is the answer";
    EXPECT_EQ(QSettings().value(QStringLiteral("Handheld/lastFolder")).toString(), folder) << "the browser opens there again";
}

TEST(HandheldBrowser_Test, AFileSlotListsOnlyFoldersAndFiles)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    GameFolder(root);

    HandheldBrowser browser;
    browser.Open(HandheldBrowser::Filter::All);
    for (const HandheldBrowser::Entry& entry : browser.entries())
        EXPECT_NE(entry.kind, HandheldBrowser::Kind::ThisFolder) << entry.name.toStdString();
}
