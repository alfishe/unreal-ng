// What the shared recording options mean (unreal-qt/src/widgets/recordingplan.h), over every format and option the
// recording panel and the full Video Recording dialog offer: both build their lists and their free controls from
// these functions and start from this plan, so these checks are the consistency of the two.

#include <gtest/gtest.h>

#include <QSet>
#include <QSettings>
#include <QTemporaryDir>

#include "widgets/recordingplan.h"

using namespace recordingplan;

namespace
{
std::vector<Environment> Environments()
{
    std::vector<Environment> all;
    for (int engine = 0; engine <= 2; ++engine)
        for (int bits = 0; bits < 16; ++bits)
            for (int codecs = 0; codecs < 3; ++codecs)
            {
                Environment env;
                env.engine = engine;
                env.nativeAvailable = bits & 1;
                env.ffmpegAvailable = bits & 2;
                env.ffmpegHasWebP = (bits & 4) && env.ffmpegAvailable;
                env.mac = bits & 8;
                if (codecs == 1)
                    env.nativeCodecs = {"h264"};
                if (codecs == 2)
                    env.nativeCodecs = {"h264", "h265", "av1"};
                all.push_back(env);
            }
    return all;
}

QString Describe(const Environment& env, const QString& container, const QString& codec)
{
    return QStringLiteral("engine %1 native %2 ffmpeg %3 webp %4 mac %5 | %6 / %7")
        .arg(env.engine).arg(env.nativeAvailable).arg(env.ffmpegAvailable).arg(env.ffmpegHasWebP).arg(env.mac)
        .arg(container, codec);
}
}  // namespace

TEST(RecordingPlan_Test, EveryEngineOffersContainersAndEachContainerACodecThatNamesItselfBack)
{
    for (const Environment& env : Environments())
    {
        const QStringList containers = Containers(env);
        ASSERT_FALSE(containers.isEmpty());
        EXPECT_EQ(QSet<QString>(containers.begin(), containers.end()).size(), containers.size()) << "no repeats";
        for (const QString& container : containers)
        {
            const QStringList codecs = VideoCodecIds(env, container);
            ASSERT_FALSE(codecs.isEmpty()) << Describe(env, container, "").toStdString();
            EXPECT_EQ(QSet<QString>(codecs.begin(), codecs.end()).size(), codecs.size()) << "no repeats";
            for (const QString& codec : codecs)
            {
                EXPECT_EQ(CodecIdOf(CodecLabel(codec)), codec) << Describe(env, container, codec).toStdString();
                // ProRes 4444 is the Mac's, in a MOV, with ffmpeg to write it
                if (codec == "prores")
                {
                    EXPECT_TRUE(env.mac && env.ffmpegAvailable && container.toUpper() == "MOV");
                    EXPECT_TRUE(WindowSourceTakes(container, codec)) << "ProRes goes through the window recorder";
                }
            }
        }
    }
}

TEST(RecordingPlan_Test, EveryFormatOfTheCatalogMakesAPlanThatIsSelfConsistent)
{
    const std::vector<Environment> envs = Environments();
    const QStringList profiles = {"native", "4k"};
    int plans = 0;
    for (const Environment& env : envs)
    {
        if (env.nativeCodecs.size() == 1)  // the middle capability set adds nothing to this loop
            continue;
        for (const QString& container : Containers(env))
            for (const QString& codec : VideoCodecIds(env, container))
                for (bool window : {false, true})
                    for (const QString& profile : profiles)
                        for (bool audio : {false, true})
                            for (const QString& wanted :
                                 QStringList{"", "AAC", "PCM", "Opus", "bogus"})
                            {
                                RecordingSettings settings;
                                settings.backend = env.engine;
                                settings.container = container;
                                settings.videoCodec = codec;
                                settings.windowSource = window;
                                settings.profile = profile;
                                settings.includeAudio = audio;
                                settings.audioCodec = wanted;
                                const Plan plan = MakePlan(settings, env.engine == 1);
                                const std::string at = Describe(env, container, codec).toStdString() +
                                                       " window " + std::to_string(window) + " " + profile.toStdString();
                                ++plans;

                                EXPECT_TRUE(plan.problem.isEmpty()) << at << ": " << plan.problem.toStdString();
                                EXPECT_FALSE(plan.audioOnly);
                                EXPECT_EQ(plan.container, container.toUpper());
                                EXPECT_EQ(plan.videoCodec, codec) << at;
                                // The window as source: only where the window recorder writes the format; ProRes always
                                EXPECT_EQ(plan.windowSource,
                                          WindowSourceTakes(container, codec) && (window || codec == "prores"))
                                    << at;
                                if (codec == "prores")
                                    EXPECT_TRUE(plan.windowSource) << at;
                                // A fixed frame only for the formats that take it
                                EXPECT_EQ(plan.profile, ProfileTakes(codec) ? profile : QString("native")) << at;
                                // The sound: none when off or when the container carries none; else one the container
                                // takes (the first when the choice does not fit)
                                const QStringList takes = AudioCodecsFor(container, env.engine == 1);
                                if (!audio || takes.isEmpty())
                                    EXPECT_TRUE(plan.audioCodec.isEmpty()) << at;
                                else
                                {
                                    QStringList names;
                                    for (const QString& name : takes)
                                        names << (name.toLower() == "pcm" ? QString("pcm_s16le") : name.toLower());
                                    EXPECT_TRUE(names.contains(plan.audioCodec)) << at << " got " << plan.audioCodec.toStdString();
                                    if (takes.contains(wanted, Qt::CaseInsensitive))
                                        EXPECT_EQ(plan.audioCodec,
                                                  wanted.toLower() == "pcm" ? QString("pcm_s16le") : wanted.toLower())
                                            << at << ": the choice is kept when the container takes it";
                                    else
                                        EXPECT_EQ(plan.audioCodec, names.value(0)) << at << ": the default otherwise";
                                }

                                // The free controls agree with the plan
                                const Controls free = ControlsFor(container, codec, window, profile, true);
                                EXPECT_EQ(free.windowSource, plan.windowSource) << at;
                                EXPECT_EQ(free.profile, ProfileTakes(codec)) << at;
                                if (free.windowSource)
                                {
                                    EXPECT_FALSE(free.capture) << at;
                                    EXPECT_FALSE(free.size) << at;
                                }
                                else
                                    EXPECT_TRUE(free.capture) << at;
                                if (!free.profile)
                                    EXPECT_TRUE(free.size) << at << ": no profile, the size factor is free";
                                EXPECT_EQ(free.source, WindowSourceTakes(container, codec) && codec != "prores") << at;
                                // Without the window recorder nothing is a window source
                                EXPECT_FALSE(ControlsFor(container, codec, window, profile, false).windowSource) << at;
                            }
    }
    EXPECT_GT(plans, 5000) << "the loop covered the catalog";
}

TEST(RecordingPlan_Test, TheQualityStepsAreDistinctAndRiseAndTheRegionAndScaleCarryOver)
{
    int previous = 0;
    for (int quality = 0; quality <= 4; ++quality)
    {
        RecordingSettings settings;
        settings.quality = quality;
        const int preset = MakePlan(settings, false).qualityPreset;
        EXPECT_GT(preset, previous) << "Fastest .. Best are distinct and rising";
        EXPECT_LE(preset, 10);
        previous = preset;
    }
    // A saved value out of range does not escape
    RecordingSettings wild;
    wild.quality = 99;
    wild.scaleFactor = 99;
    wild.captureRegion = 7;
    const Plan plan = MakePlan(wild, false);
    EXPECT_EQ(plan.qualityPreset, 9);
    EXPECT_EQ(plan.scale, 8);
    EXPECT_FALSE(plan.mainScreenOnly);
    for (int region = 0; region <= 1; ++region)
        for (int scale = 1; scale <= 8; ++scale)
        {
            RecordingSettings settings;
            settings.captureRegion = region;
            settings.scaleFactor = scale;
            EXPECT_EQ(MakePlan(settings, false).mainScreenOnly, region == 1);
            EXPECT_EQ(MakePlan(settings, false).scale, scale);
        }
}

TEST(RecordingPlan_Test, AudioOnlyFormatsAreWhatTheirNameSaysAndTheDialogsOwnAreSaidSo)
{
    struct Row { const char* container; const char* codec; bool problem; };
    for (const Row& row : {Row{"WAV", "pcm_s16le", false}, Row{"MP3", "mp3", false}, Row{"FLAC", "flac", false},
                           Row{"OGG", "", true}, Row{"DSD", "", true}})
    {
        RecordingSettings settings;
        settings.container = row.container;
        settings.audioBitrate = 256;
        const Plan plan = MakePlan(settings, false);
        EXPECT_TRUE(plan.audioOnly) << row.container;
        EXPECT_EQ(plan.audioCodec, QString(row.codec)) << row.container;
        EXPECT_EQ(!plan.problem.isEmpty(), row.problem) << row.container;
        EXPECT_EQ(plan.audioBitrate, QString(row.container) == "MP3" ? 256 : 0) << row.container;
        EXPECT_TRUE(plan.videoCodec.isEmpty());
    }
}

TEST(RecordingPlan_Test, ProResOutsideAMovIsRefusedWithAReason)
{
    RecordingSettings settings;
    settings.container = "MP4";
    settings.videoCodec = "prores";
    const Plan plan = MakePlan(settings, false);
    EXPECT_FALSE(plan.problem.isEmpty());
}

TEST(RecordingPlan_Test, EveryOptionSurvivesTheSharedStoreBothUisUse)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath("recording.ini");
    for (const Environment& env : Environments())
    {
        if (env.nativeCodecs.size() != 3)
            continue;
        for (const QString& container : Containers(env))
            for (const QString& codec : VideoCodecIds(env, container))
            {
                RecordingSettings in;
                in.backend = env.engine;
                in.container = container;
                in.videoCodec = codec;
                in.quality = (container.size() + codec.size()) % 5;
                in.captureRegion = codec.size() % 2;
                in.scaleFactor = 1 + codec.size() % 8;
                in.profile = codec.size() % 2 ? "4k" : "native";
                in.includeAudio = container.size() % 2;
                in.audioCodec = "AAC";
                in.windowSource = container.size() % 2;
                {
                    QSettings store(file, QSettings::IniFormat);
                    in.save(store);
                }
                QSettings store(file, QSettings::IniFormat);
                RecordingSettings out;
                out.load(store);
                EXPECT_EQ(out.backend, in.backend);
                EXPECT_EQ(out.container, in.container);
                EXPECT_EQ(out.videoCodec, in.videoCodec);
                EXPECT_EQ(out.quality, in.quality);
                EXPECT_EQ(out.captureRegion, in.captureRegion);
                EXPECT_EQ(out.scaleFactor, in.scaleFactor);
                EXPECT_EQ(out.profile, in.profile);
                EXPECT_EQ(out.includeAudio, in.includeAudio);
                EXPECT_EQ(out.audioCodec, in.audioCodec);
                EXPECT_EQ(out.windowSource, in.windowSource);
                // and means the same to both UIs
                const Plan a = MakePlan(in, env.engine == 1);
                const Plan b = MakePlan(out, env.engine == 1);
                EXPECT_EQ(a.videoCodec, b.videoCodec);
                EXPECT_EQ(a.audioCodec, b.audioCodec);
                EXPECT_EQ(a.windowSource, b.windowSource);
                EXPECT_EQ(a.qualityPreset, b.qualityPreset);
            }
    }
}
