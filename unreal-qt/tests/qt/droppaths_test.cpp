// The files a drag carries (media/droppaths.h): Finder's file references that Qt leaves empty are read off the drag
// pasteboard and resolved, by the system or by their inode when Finder's volume id is stale; a drag with no file that
// can be opened says why instead of showing nothing and then "file not found: ''"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

#include <sys/stat.h>

#include "media/droppaths.h"

namespace
{
/// Raw URLs that must not be read: Qt's own were all files
QStringList NotRead()
{
    ADD_FAILURE() << "the drag pasteboard was read although Qt gave every file";
    return {};
}
}  // namespace

TEST(DropPaths_Test, QtsLocalFilesPassAsTheyAre)
{
    const QList<QUrl> urls{QUrl::fromLocalFile("/disk/a.trd"), QUrl::fromLocalFile("/disk/b.trd")};
    const DropPaths::Result result = DropPaths::Collect(urls, &NotRead, [](const QString&) { return QString(); });
    EXPECT_EQ(result.paths, (QStringList{"/disk/a.trd", "/disk/b.trd"}));
    EXPECT_TRUE(result.refusal.isEmpty());
}

TEST(DropPaths_Test, AFinderReferenceQtLeftEmptyIsResolvedFromTheDragPasteboard)
{
    // What Qt 6.9 gave for a Finder drag of a .trd: one empty URL; the pasteboard had the file reference
    int reads = 0;
    QStringList asked;
    const DropPaths::Result result = DropPaths::Collect(
        {QUrl()},
        [&]() {
            reads++;
            return QStringList{"file:///.file/id=7594310.23750028"};
        },
        [&](const QString& raw) {
            asked << raw;
            return QStringLiteral("/Volumes/TB4-4Tb/testdata/across_the_edge_by_demarche.trd");
        });
    EXPECT_EQ(result.paths, QStringList{"/Volumes/TB4-4Tb/testdata/across_the_edge_by_demarche.trd"});
    EXPECT_TRUE(result.refusal.isEmpty());
    EXPECT_EQ(reads, 1);
    EXPECT_EQ(asked, QStringList{"file:///.file/id=7594310.23750028"});
}

TEST(DropPaths_Test, AReferenceNothingResolvesSaysWhyInsteadOfAnEmptyPath)
{
    const DropPaths::Result result = DropPaths::Collect(
        {QUrl()}, []() { return QStringList{"file:///.file/id=7594310.23750028"}; },
        [](const QString&) { return QString(); });
    EXPECT_TRUE(result.paths.isEmpty()) << "an empty path once made the drop say \"file not found: ''\"";
    EXPECT_TRUE(result.refusal.contains("cannot find the dragged file")) << result.refusal.toStdString();
    EXPECT_TRUE(result.refusal.contains("Finder")) << result.refusal.toStdString();
}

TEST(DropPaths_Test, ANonFileUrlIsRefusedByName)
{
    const DropPaths::Result result = DropPaths::Collect({QUrl("https://example.org/game.trd")}, &NotRead,
                                                        [](const QString&) { return QString(); });
    EXPECT_TRUE(result.paths.isEmpty());
    EXPECT_TRUE(result.refusal.contains("not a file on this computer: https://example.org/game.trd"))
        << result.refusal.toStdString();
}

TEST(DropPaths_Test, TheFilesThatResolveGoTheOthersAreLeftOutWithoutARefusal)
{
    const DropPaths::Result result = DropPaths::Collect(
        {QUrl::fromLocalFile("/disk/a.trd"), QUrl(), QUrl()},
        []() { return QStringList{"file:///disk/a.trd", "file:///.file/id=1.2", "file:///.file/id=1.3"}; },
        [](const QString& raw) { return raw.endsWith(".3") ? QStringLiteral("/disk/c.trd") : QString(); });
    EXPECT_EQ(result.paths, (QStringList{"/disk/a.trd", "/disk/c.trd"}));
    EXPECT_TRUE(result.refusal.isEmpty());
}

TEST(DropPaths_Test, ADropTakesTheFirst32Files)
{
    QList<QUrl> urls;
    for (int i = 0; i < 40; i++)
        urls << QUrl::fromLocalFile(QString("/disk/%1.trd").arg(i));
    const DropPaths::Result result = DropPaths::Collect(urls, &NotRead, [](const QString&) { return QString(); });
    ASSERT_EQ(result.paths.size(), DropPaths::kMaxFiles);
    EXPECT_EQ(result.paths.last(), "/disk/31.trd");
}

TEST(DropPaths_Test, AFileReferencesVolumeAndInode)
{
    uint64_t volume = 0;
    uint64_t inode = 0;
    ASSERT_TRUE(DropPaths::ParseFileReference("file:///.file/id=7594310.23750028", &volume, &inode));
    EXPECT_EQ(volume, 7594310u);
    EXPECT_EQ(inode, 23750028u);
    ASSERT_TRUE(DropPaths::ParseFileReference("file:///.file/id=6571367.2/", &volume, &inode)) << "a folder's";
    EXPECT_EQ(inode, 2u);
    for (const char* bad : {"file:///Volumes/x.trd", "file:///.file/id=", "file:///.file/id=12", "file:///.file/id=.5",
                            "file:///.file/id=5.", "file:///.file/id=a.b", "https://x/.file/id=1.2"})
        EXPECT_FALSE(DropPaths::ParseFileReference(bad, &volume, &inode)) << bad;
}

TEST(DropPaths_Test, AnInodeFoundOnSeveralVolumesOrNoneIsNoMatch)
{
    EXPECT_EQ(DropPaths::UniqueMatch({}), QString());
    EXPECT_EQ(DropPaths::UniqueMatch({"/Volumes/A/x.trd"}), "/Volumes/A/x.trd");
    EXPECT_EQ(DropPaths::UniqueMatch({"/Volumes/A/x.trd", "/Volumes/B/y.trd"}), QString()) << "never a guess";
}

TEST(DropPaths_Test, AMimeDataOfLocalFilesGivesItsPaths)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath("game.trd");
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(file)});
    EXPECT_EQ(DropPaths::FromMime(&mime).paths, QStringList{file});
    QMimeData text;
    text.setText("not a drag of files");
    EXPECT_TRUE(DropPaths::FromMime(&text).paths.isEmpty());
    EXPECT_TRUE(DropPaths::FromMime(&text).refusal.isEmpty()) << "no files at all: nothing to refuse";
}

#ifdef Q_OS_MACOS
TEST(DropPaths_Test, MacResolvesAPathUrlAndAStaleReferenceByItsInode)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = QFileInfo(dir.filePath("game.trd")).absoluteFilePath();
    {
        QFile out(file);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        out.write("TRD");
    }
    const QString canonical = QFileInfo(file).canonicalFilePath();
    EXPECT_EQ(QFileInfo(DropPaths::Resolve(QUrl::fromLocalFile(file).toString())).canonicalFilePath(), canonical);

    struct stat st{};
    ASSERT_EQ(stat(file.toUtf8().constData(), &st), 0);
    // Volume id 1 is no volume's: the system cannot resolve it, the inode finds the file
    const QString stale = QString("file:///.file/id=1.%1").arg(static_cast<qulonglong>(st.st_ino));
    EXPECT_EQ(QFileInfo(DropPaths::Resolve(stale)).canonicalFilePath(), canonical);

    EXPECT_EQ(DropPaths::Resolve("file:///.file/id=1.18446744073709551000"), QString()) << "no such inode";
    EXPECT_EQ(DropPaths::Resolve(QUrl::fromLocalFile(dir.filePath("gone.trd")).toString()), QString());
    EXPECT_EQ(DropPaths::Resolve(""), QString());
}
#endif

// A floppy image dropped into one drive while another holds it (media/floppymove.h): that drive gives it up

#include <array>
#include <string>

#include "media/floppymove.h"

TEST(FloppyMove_Test, TheDriveHoldingTheSameImageIsFoundAndNeverTheTargetItself)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string game = dir.filePath("game.trd").toStdString();
    const std::string other = dir.filePath("other.trd").toStdString();
    for (const std::string& path : {game, other})
    {
        QFile out(QString::fromStdString(path));
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    }
    // The drop that went to D, then the same file dropped into A
    std::string drives[4] = {"", "", "", game};
    EXPECT_EQ(FloppyMove::DriveHolding(drives, 0, game), 3);
    EXPECT_EQ(FloppyMove::DriveHolding(drives, 3, game), -1) << "already in the target: nothing to move";
    EXPECT_EQ(FloppyMove::DriveHolding(drives, 0, other), -1);

    // Another spelling of the same file (a ./ segment, a link) is the same image
    const std::string spelled = dir.filePath("./game.trd").toStdString();
    EXPECT_EQ(FloppyMove::DriveHolding(drives, 1, spelled), 3);
#ifndef Q_OS_WIN  // a Windows link is a .lnk shortcut, no file system link
    const std::string link = dir.filePath("link.trd").toStdString();
    ASSERT_TRUE(QFile::link(QString::fromStdString(game), QString::fromStdString(link)));
    EXPECT_EQ(FloppyMove::DriveHolding(drives, 1, link), 3);
#endif

    const std::array<std::string, 4> empty{};
    EXPECT_EQ(FloppyMove::DriveHolding(empty, 0, game), -1) << "empty drives hold nothing";
    EXPECT_FALSE(FloppyMove::SameFile("", ""));
}
