// The CD drive's audio side (cdaudioplayer.h): the head moving 75 frames per
// second of emulated time (whole frames and inside a frame), play / pause /
// resume / stop, the status codes 11h-15h, a play into a data track, SOTC, and
// the renderer: sample-exact at a 44100 Hz mixer, page 0Eh routing and volume,
// another mixer rate, nothing rendered while nothing plays.

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/cdtestdisc.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/ide/ata/cdaudioplayer.h"
#include "emulator/io/storage/cd/cdimageformats.h"

using namespace cdtest;

namespace
{
    constexpr uint32_t kPentagonFrame = 71680;  ///< T-states of a Pentagon frame
    constexpr int64_t kUnits = CdAudioPlayer::kUnitsPerSample;

    class CdAudioPlayer_Test : public ::testing::Test
    {
    protected:
        std::unique_ptr<ScratchFolder> _folder;
        std::unique_ptr<CdImage> _disc;
        CdAudioPlayer _player;
        uint32_t _elapsed = 0;  ///< the fake frame clock: base T-states into the frame

        void SetUp() override
        {
            _folder = std::make_unique<ScratchFolder>("cd-player");
            std::string error;
            _disc = CdImageFormats::Open(WriteFixtureDisc(_folder->Path()), &error);
            ASSERT_NE(_disc, nullptr) << error;
            _player.SetDisc(_disc.get());
            _player.SetClock([this] { return _elapsed; });
        }

        /// One frame of `frameT` T-states: the clock reads 0 again afterwards
        void Frame(uint32_t frameT = kPentagonFrame)
        {
            _elapsed = 0;
            _player.FrameEnd(frameT);
        }

        /// The disc sample (left, right) the head plays at sample `index` from LBA 0
        std::pair<int16_t, int16_t> DiscSample(uint64_t index)
        {
            int16_t frame[kSamples * 2];
            _disc->ReadAudio(static_cast<uint32_t>(index / kSamples), frame);
            const size_t at = (index % kSamples) * 2;
            return {frame[at], frame[at + 1]};
        }
    };
}  // namespace

TEST_F(CdAudioPlayer_Test, HeadMovesSeventyFiveFramesPerEmulatedSecond)
{
    // A disc with 3-second tracks: track 2 is LBA 166-390
    ScratchFolder folder("cd-player-music");
    std::string error;
    auto music = CdImageFormats::Open(WriteMusicDisc(folder.Path(), 2, 3, 16), &error);
    ASSERT_NE(music, nullptr) << error;
    _player.SetDisc(music.get());
    _player.Play(166, 391);
    EXPECT_EQ(_player.HeadSample(), 166u * kSamples);
    // 50 frames of 70000 T-states: 3.5 million T, one second: 44100 samples, 75 frames
    for (int i = 0; i < 25; i++)
        Frame(70000);
    EXPECT_EQ(_player.HeadSample(), 166u * kSamples + 22050);
    for (int i = 0; i < 25; i++)
        Frame(70000);
    EXPECT_EQ(_player.HeadLba(), 166u + 75) << "one emulated second: 75 frames";
    // Inside a frame the head follows the clock: 35000 T is 441 samples
    _elapsed = 35000;
    EXPECT_EQ(_player.HeadSample(), 166u * kSamples + 44100 + 441);
    EXPECT_EQ(_player.HeadLba(), (166u * kSamples + 44541) / kSamples);

    // A Pentagon frame (71680 T, 48.83 Hz): the exact integer count, no rounding drift
    _elapsed = 0;
    _player.Play(166, 391);
    for (int i = 0; i < 97; i++)
        Frame(kPentagonFrame);
    EXPECT_EQ(_player.HeadSample(), 166u * kSamples + static_cast<uint64_t>(97) * kPentagonFrame * 44100 / 3500000);
    _player.SetDisc(_disc.get());
}

TEST_F(CdAudioPlayer_Test, PlayStartedMidFrameCountsFromThatInstant)
{
    _elapsed = 20000;
    _player.Play(8, 16);
    EXPECT_EQ(_player.HeadSample(), 8u * kSamples) << "the head starts where PLAY arrived";
    _elapsed = 20000 + 3500;  // 1 ms later
    EXPECT_EQ(_player.HeadSample(), 8u * kSamples + 44);
    _player.FrameEnd(kPentagonFrame);  // the clock now counts the next frame from 0
    _elapsed = 0;
    EXPECT_EQ(_player.HeadSample(), 8u * kSamples + (kPentagonFrame - 20000) * 44100ull / 3500000);
}

TEST_F(CdAudioPlayer_Test, StatusCodesPauseResumeStop)
{
    EXPECT_EQ(_player.TakeStatusCode(), 0x15) << "no play yet";
    EXPECT_FALSE(_player.Pause()) << "nothing to pause";
    EXPECT_FALSE(_player.Resume());

    _player.Play(18, 22);  // track 3: 4 frames, 0.053 s
    EXPECT_EQ(_player.TakeStatusCode(), 0x11);
    Frame();
    ASSERT_TRUE(_player.Pause());
    const uint64_t paused = _player.HeadSample();
    EXPECT_EQ(_player.TakeStatusCode(), 0x12);
    Frame();
    Frame();
    EXPECT_EQ(_player.HeadSample(), paused) << "a paused head stays";
    _elapsed = 1000;
    ASSERT_TRUE(_player.Resume());
    EXPECT_EQ(_player.HeadSample(), paused) << "resumed: it moves on from where it stopped";
    Frame();
    Frame();
    // Past LBA 22: completed, reported once
    EXPECT_EQ(_player.Status(), CdAudioStatus::Completed);
    EXPECT_EQ(_player.HeadLba(), 21u) << "the head rests on the last frame played";
    EXPECT_EQ(_player.PeekStatusCode(), 0x13) << "a peek does not consume it";
    EXPECT_EQ(_player.TakeStatusCode(), 0x13);
    EXPECT_EQ(_player.TakeStatusCode(), 0x15);

    _player.Play(8, 16);
    Frame();
    _player.Stop();
    const uint64_t stopped = _player.HeadSample();
    Frame();
    EXPECT_EQ(_player.HeadSample(), stopped);
    EXPECT_EQ(_player.TakeStatusCode(), 0x15);
    EXPECT_FALSE(_player.Resume()) << "a stopped play does not resume";
}

TEST_F(CdAudioPlayer_Test, PlayIntoADataTrackStopsWithAnError)
{
    // Audio first, then data: the play range crosses into the data track
    ScratchFolder folder("cd-player-data");
    WriteFile(folder.Path() / "a.bin", RampPcm(kRampA, 3) + DataFrames(3, 3, true));
    WriteFile(folder.Path() / "a.cue",
              "FILE a.bin BINARY\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n TRACK 02 MODE1/2352\n  INDEX 01 00:00:03\n");
    std::string error;
    auto disc = CdImageFormats::Open(Utf8(folder.Path() / "a.cue"), &error);
    ASSERT_NE(disc, nullptr) << error;
    _player.SetDisc(disc.get());
    _player.Play(0, 6);
    EXPECT_EQ(_player.State().endLba, 3u) << "the play ends at the data track";
    for (int i = 0; i < 3; i++)
        Frame();
    EXPECT_EQ(_player.TakeStatusCode(), 0x14);
    EXPECT_EQ(_player.TakeStatusCode(), 0x15);
    _player.SetDisc(_disc.get());
}

TEST_F(CdAudioPlayer_Test, StopOnTrackCrossing)
{
    _player.MutableState().sotc = 1;
    _player.Play(8, 22);
    EXPECT_EQ(_player.State().endLba, 16u) << "SOTC: the end of the starting track";
    _player.MutableState().sotc = 0;
    _player.Play(8, 22);
    EXPECT_EQ(_player.State().endLba, 22u);
}

TEST_F(CdAudioPlayer_Test, RendersTheTrackSampleExactAtFortyFourKilohertz)
{
    _player.Play(4, 16);  // track 2 from its pregap: silence, then ramp A at LBA 8
    uint64_t accumulator = 0;
    std::vector<int16_t> stream;
    for (int frame = 0; frame < 7; frame++)
    {
        // The mixer's own sample count (SoundManager: exact T-state accumulator)
        accumulator += static_cast<uint64_t>(kPentagonFrame) * 44100;
        const size_t samples = static_cast<size_t>(accumulator / 3500000);
        accumulator %= 3500000;
        Frame();
        _player.Render(samples, 44100);
        const int16_t* out = _player.Buffer();
        ASSERT_NE(out, nullptr) << "frame " << frame;
        stream.insert(stream.end(), out, out + samples * 2);
    }
    ASSERT_GT(stream.size() / 2, 8u * kSamples);
    for (size_t i = 0; i < stream.size() / 2; i++)
    {
        const auto [left, right] = DiscSample(4 * kSamples + i);
        ASSERT_EQ(stream[2 * i], left) << "sample " << i;
        ASSERT_EQ(stream[2 * i + 1], right) << "sample " << i;
    }
    // The ramp itself: the first sample of LBA 8 is ramp A sample 0
    EXPECT_EQ(stream[4 * kSamples * 2 + 2], kRampA.Left(1));
}

TEST_F(CdAudioPlayer_Test, PageZeroEhRoutesAndScalesTheChannels)
{
    CdAudioState& state = _player.MutableState();
    state.portSelect[0] = 2;  // left output plays the right channel
    state.portSelect[1] = 1;  // and the other way round
    state.portVolume[0] = 0xFF;
    state.portVolume[1] = 128;
    _player.Play(8, 16);
    Frame();
    _player.Render(903, 44100);  // a Pentagon frame: 903.17 samples
    const int16_t* out = _player.Buffer();
    ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < 903; i++)
    {
        const auto [left, right] = DiscSample(8 * kSamples + i);
        ASSERT_EQ(out[2 * i], right);
        ASSERT_EQ(out[2 * i + 1], static_cast<int16_t>(left * 128 / 255));
    }

    state.portSelect[0] = 3;  // both channels mixed
    state.portSelect[1] = 0;  // muted
    Frame();
    _player.Render(903, 44100);
    out = _player.Buffer();
    for (size_t i = 0; i < 903; i++)
    {
        const auto [left, right] = DiscSample(8 * kSamples + 903 + i);
        ASSERT_EQ(out[2 * i], static_cast<int16_t>((left + right) / 2));
        ASSERT_EQ(out[2 * i + 1], 0);
    }
}

TEST_F(CdAudioPlayer_Test, NothingPlaysNothingRenders)
{
    _player.Render(900, 44100);
    EXPECT_EQ(_player.Buffer(), nullptr) << "no play: the mixer skips the source";
    EXPECT_FALSE(_player.HadSoundLastFrame());

    _player.Play(16, 18);  // track 3's PREGAP: silence
    Frame();
    _player.Render(900, 44100);
    EXPECT_NE(_player.Buffer(), nullptr);
    EXPECT_FALSE(_player.HadSoundLastFrame()) << "silence is no activity";
}

TEST_F(CdAudioPlayer_Test, OtherMixerRatesInterpolate)
{
    _player.Play(8, 16);
    Frame();
    _player.Render(980, 48000);  // 71680 T at 48 kHz: 983 samples
    const int16_t* out = _player.Buffer();
    ASSERT_NE(out, nullptr);
    // Ramp A rises 64 per sample on the left: at 48 kHz, 64 x 44100 / 48000 = 58.8 per output sample,
    // except around its wrap from +32767 to -32768 (interpolated across, once every 1024 source samples)
    size_t onSlope = 0;
    for (size_t i = 1; i < 980; i++)
    {
        const int step = out[2 * i] - out[2 * (i - 1)];
        if (step >= 57 && step <= 61)
            onSlope++;
    }
    EXPECT_GE(onSlope, 975u);
}
