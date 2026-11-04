// The recording panel's shared settings: the picture source (raw frame buffer or the window as drawn) survives a
// save / load and defaults to the raw frame buffer. Each test reads and writes an ini file of its own.

#include <gtest/gtest.h>

#include <QSettings>
#include <QTemporaryDir>

#include "widgets/recordingsettings.h"
#include "widgets/recordingsettingsbus.h"

class RecordingSettings_Test : public ::testing::Test
{
protected:
    void SetUp() override { ASSERT_TRUE(_dir.isValid()); }

    QString File() const { return _dir.filePath(QStringLiteral("recording.ini")); }

    QTemporaryDir _dir;
};

TEST_F(RecordingSettings_Test, TheSourceDefaultsToTheRawFrameBuffer)
{
    QSettings store(File(), QSettings::IniFormat);
    RecordingSettings settings;
    settings.load(store);
    EXPECT_FALSE(settings.windowSource);
}

TEST_F(RecordingSettings_Test, TheWindowSourceSurvivesSaveAndLoad)
{
    {
        QSettings store(File(), QSettings::IniFormat);
        RecordingSettings settings;
        settings.windowSource = true;
        settings.save(store);
    }
    QSettings store(File(), QSettings::IniFormat);
    RecordingSettings loaded;
    loaded.load(store);
    EXPECT_TRUE(loaded.windowSource);

    loaded.windowSource = false;
    loaded.save(store);
    RecordingSettings again;
    again.load(store);
    EXPECT_FALSE(again.windowSource);
}

// The recording panel and the full dialog show the same options: a listener hears every save of the shared
// settings, and not after its owner is gone
TEST_F(RecordingSettings_Test, EveryRecordingUiHearsAChangeOfTheSharedSettingsWhileItLives)
{
    int heard = 0;
    auto* owner = new QObject();
    recordingsettingsbus::Subscribe(owner, [&heard] { ++heard; });
    int other = 0;
    QObject second;
    recordingsettingsbus::Subscribe(&second, [&other] { ++other; });

    recordingsettingsbus::Notify();
    EXPECT_EQ(heard, 1);
    EXPECT_EQ(other, 1);

    delete owner;
    recordingsettingsbus::Notify();
    EXPECT_EQ(heard, 1) << "a closed UI is not called";
    EXPECT_EQ(other, 2);
}
