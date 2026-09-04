#include <gtest/gtest.h>
#include "_helpers/testpathhelper.h"
#include "_helpers/tzxtapebuilder.h"
#include "common/filehelper.h"
#include "common/subprocess.h"
#include "emulator/io/tape/tapecatalog.h"
#include "ffmpeg_probe.h"
#include "loaders/tape/loader_tape.h"
#include "tapeaudio/tapeaudioimporter.h"
#include "tapeaudio/tapeaudiorenderer.h"

#include <cstdint>
#include <string>
#include <vector>

/// Tape-audio import round trips (tape-audio-bridge design §8.2, the
/// fidelity contract): TZX → WAV → recognize → TZX/TAP with byte-identical
/// payloads, the re-rendered WAV byte-identical to the original, ffmpeg-
/// gated FLAC/MP3 paths, the TAP export gate and the error paths.

namespace
{
    /// flag 0x00 + a genuine 19-byte ZX header (type Program, name, length,
    /// params), XOR checksum — the exact shape the catalog's header decode
    /// and pairing expect (flag + 17-byte body + checksum)
    std::vector<uint8_t> HeaderBlock()
    {
        std::vector<uint8_t> bytes = {0x00, 0x00, 'r', 'o', 'u', 'n', 'd', 't', 'r', 'i', 'p', ' ',
                                      0x20, 0x00, 0x00, 0x00, 0x80, 0x00};
        uint8_t parity = 0;
        for (uint8_t byte : bytes)
        {
            parity ^= byte;
        }
        bytes.push_back(parity);
        return bytes;
    }

    std::vector<uint8_t> DataBlock(size_t length)
    {
        std::vector<uint8_t> bytes;
        bytes.push_back(0xFF);
        for (size_t i = 0; i < length; i++)
        {
            bytes.push_back(static_cast<uint8_t>((i * 31 + 7) & 0xFF));
        }
        uint8_t parity = 0;
        for (uint8_t byte : bytes)
        {
            parity ^= byte;
        }
        bytes.push_back(parity);
        return bytes;
    }

    std::string WriteTwoBlockTape(const std::string& name)
    {
        TzxTapeBuilder builder;
        builder.AddStandardBlock(1000, HeaderBlock());
        builder.AddStandardBlock(1000, DataBlock(300));
        const std::string path = TestPathHelper::GetTestScratchPath("tapeaudio-import/" + name);
        EXPECT_TRUE(TzxTapeBuilder::WriteToFile(builder.Bytes(), path));
        return path;
    }

    TapeRenderResult RenderWav(const std::string& source, const std::string& out)
    {
        TapeRenderRequest request;
        request.sourcePath = source;
        request.outputPath = out;
        request.sampleRate = 44100;
        return RenderTapeToAudio(request);
    }

    std::vector<uint8_t> ReadFileBytes(const std::string& path)
    {
        const size_t size = FileHelper::GetFileSize(path);
        std::vector<uint8_t> bytes(size);
        if (size > 0)
        {
            FileHelper::ReadFileToBuffer(path, bytes.data(), size);
        }
        return bytes;
    }

    /// Load through the production registry and run the catalog pass — the
    /// same loader → TapeCatalogParser::Build pipeline tape.cpp applies
    TapeImage LoadTape(const std::string& path, TapeLoadStatus* status = nullptr)
    {
        const std::vector<uint8_t> buffer = ReadFileBytes(path);
        LoaderTapeBase* loader = TapeLoaderRegistry::Instance().Select(buffer, path);
        if (loader == nullptr)
        {
            return {};
        }
        TapeImage image = loader->Load(buffer, path);
        if (status != nullptr)
        {
            *status = image.status;
        }
        image.descriptors = TapeCatalogParser::Build(image);
        return image;
    }
}

TEST(TapeAudioImporter_Test, TzxWavTzxRoundTripPreservesPayloads)
{
    const std::string source = WriteTwoBlockTape("round.tzx");
    const std::string wav = TestPathHelper::GetTestScratchPath("tapeaudio-import/round.wav");
    ASSERT_TRUE(RenderWav(source, wav).ok);

    TapeImportRequest request;
    request.sourcePath = wav;
    const TapeImportResult imported = ImportAudioToTape(request);
    ASSERT_TRUE(imported.ok) << imported.errorText;
    EXPECT_EQ(imported.decoderUsed, "tinywav(wav)");
    ASSERT_GE(imported.image.blocks.size(), 2u);

    const std::string saved = TestPathHelper::GetTestScratchPath("tapeaudio-import/round-back.tzx");
    const TapeSaveResult savedResult = SaveTapeImage(imported.image, saved);
    ASSERT_TRUE(savedResult.ok) << savedResult.errorText;

    // Reload through the production loader and compare byte payloads
    const TapeImage roundTripped = LoadTape(saved);
    ASSERT_TRUE(roundTripped.IsUsable());
    ASSERT_EQ(roundTripped.blocks.size(), 2u);
    EXPECT_EQ(roundTripped.blocks[0].data, HeaderBlock());
    EXPECT_EQ(roundTripped.blocks[1].data, DataBlock(300));
    EXPECT_EQ(roundTripped.descriptors[0].kind, TapeBlockKindEnum::Header);
    EXPECT_EQ(roundTripped.descriptors[0].timing.pauseMs, 1000u);
    EXPECT_EQ(roundTripped.descriptors[1].kind, TapeBlockKindEnum::Data);
    EXPECT_EQ(roundTripped.descriptors[1].timing.pauseMs, 1000u);
    EXPECT_TRUE(roundTripped.descriptors[0].checksumValid);
    EXPECT_TRUE(roundTripped.descriptors[1].checksumValid);
}

TEST(TapeAudioImporter_Test, ReRenderedWavIsByteIdentical)
{
    // The fidelity contract (design §8.3): original render vs re-render of
    // the recognized-and-resaved image — same pulses, same silence, same
    // header; anything but byte equality is a fidelity loss.
    const std::string source = WriteTwoBlockTape("fidelity.tzx");
    const std::string wavA = TestPathHelper::GetTestScratchPath("tapeaudio-import/fidelity-a.wav");
    const std::string wavB = TestPathHelper::GetTestScratchPath("tapeaudio-import/fidelity-b.wav");
    ASSERT_TRUE(RenderWav(source, wavA).ok);

    TapeImportRequest request;
    request.sourcePath = wavA;
    const TapeImportResult imported = ImportAudioToTape(request);
    ASSERT_TRUE(imported.ok) << imported.errorText;

    const std::string saved = TestPathHelper::GetTestScratchPath("tapeaudio-import/fidelity-back.tzx");
    ASSERT_TRUE(SaveTapeImage(imported.image, saved).ok);

    ASSERT_TRUE(RenderWav(saved, wavB).ok);
    EXPECT_EQ(ReadFileBytes(wavA), ReadFileBytes(wavB));
}

TEST(TapeAudioImporter_Test, TapExportGateRefusesNonRomContent)
{
    // Turbo block content: TAP has no fields for it — the gate must refuse
    TzxTapeBuilder builder;
    builder.AddTurboBlock(2000, 600, 700, 700, 1400, 10, 8, 1000, DataBlock(50));
    const std::string source = TestPathHelper::GetTestScratchPath("tapeaudio-import/turbo.tzx");
    ASSERT_TRUE(TzxTapeBuilder::WriteToFile(builder.Bytes(), source));

    const std::string wav = TestPathHelper::GetTestScratchPath("tapeaudio-import/turbo.wav");
    ASSERT_TRUE(RenderWav(source, wav).ok);

    TapeImportRequest request;
    request.sourcePath = wav;
    const TapeImportResult imported = ImportAudioToTape(request);
    ASSERT_TRUE(imported.ok) << imported.errorText;

    const TapeSaveResult tap = SaveTapeImage(imported.image,
                                             TestPathHelper::GetTestScratchPath("tapeaudio-import/turbo.tap"));
    ASSERT_FALSE(tap.ok);
    EXPECT_NE(tap.errorText.find("tzx"), std::string::npos) << tap.errorText;

    const TapeSaveResult tzx = SaveTapeImage(imported.image,
                                             TestPathHelper::GetTestScratchPath("tapeaudio-import/turbo-back.tzx"));
    ASSERT_TRUE(tzx.ok) << tzx.errorText;
    const TapeImage roundTripped = LoadTape(TestPathHelper::GetTestScratchPath("tapeaudio-import/turbo-back.tzx"));
    ASSERT_TRUE(roundTripped.IsUsable());
    ASSERT_EQ(roundTripped.blocks.size(), 1u);
    EXPECT_EQ(roundTripped.blocks[0].data, DataBlock(50));
    ASSERT_TRUE(roundTripped.blocks[0].timing.has_value());
    EXPECT_EQ(roundTripped.blocks[0].timing->profile, TapeSpeedProfileEnum::Custom);
}

TEST(TapeAudioImporter_Test, RomTapeExportsToTapAndBack)
{
    const std::string source = WriteTwoBlockTape("rom.tzx");
    const std::string wav = TestPathHelper::GetTestScratchPath("tapeaudio-import/rom.wav");
    ASSERT_TRUE(RenderWav(source, wav).ok);

    TapeImportRequest request;
    request.sourcePath = wav;
    const TapeImportResult imported = ImportAudioToTape(request);
    ASSERT_TRUE(imported.ok) << imported.errorText;

    const std::string tapPath = TestPathHelper::GetTestScratchPath("tapeaudio-import/rom-back.tap");
    const TapeSaveResult tap = SaveTapeImage(imported.image, tapPath);
    ASSERT_TRUE(tap.ok) << tap.errorText;

    const TapeImage roundTripped = LoadTape(tapPath);
    ASSERT_TRUE(roundTripped.IsUsable());
    ASSERT_EQ(roundTripped.blocks.size(), 2u);
    EXPECT_EQ(roundTripped.blocks[0].data, HeaderBlock());
    EXPECT_EQ(roundTripped.blocks[1].data, DataBlock(300));
}

TEST(TapeAudioImporter_Test, FlacRoundTripOrHonestSkip)
{
    if (!FFmpegProbe::isAvailable())
    {
        GTEST_SKIP() << "ffmpeg not available on this machine";
    }

    const std::string source = WriteTwoBlockTape("flac.tzx");
    const std::string wav = TestPathHelper::GetTestScratchPath("tapeaudio-import/flac.wav");
    const std::string flac = TestPathHelper::GetTestScratchPath("tapeaudio-import/flac.flac");
    ASSERT_TRUE(RenderWav(source, wav).ok);

    // Encode FLAC with the same external tool the decode path trusts;
    // -y because scratch artifacts persist between test runs and ffmpeg
    // would otherwise block on an overwrite prompt nobody answers
    Subprocess encoder;
    ASSERT_TRUE(encoder.spawn(FFmpegProbe::findFFmpeg(),
                              {"-hide_banner", "-loglevel", "error", "-y", "-i", wav, "-ac", "1", flac}));
    ASSERT_EQ(encoder.waitForFinished(30000), 0) << encoder.readAllStderr();

    TapeImportRequest request;
    request.sourcePath = flac;
    const TapeImportResult imported = ImportAudioToTape(request);
    ASSERT_TRUE(imported.ok) << imported.errorText;
    EXPECT_EQ(imported.decoderUsed, "ffmpeg(flac)");

    const std::string saved = TestPathHelper::GetTestScratchPath("tapeaudio-import/flac-back.tzx");
    ASSERT_TRUE(SaveTapeImage(imported.image, saved).ok);
    const TapeImage roundTripped = LoadTape(saved);
    ASSERT_TRUE(roundTripped.IsUsable());
    ASSERT_GE(roundTripped.blocks.size(), 2u);
    EXPECT_EQ(roundTripped.blocks[0].data, HeaderBlock());
    EXPECT_EQ(roundTripped.blocks[1].data, DataBlock(300));
}

TEST(TapeAudioImporter_Test, Mp3ImportSurvivesLossyEncoding)
{
    if (!FFmpegProbe::isAvailable())
    {
        GTEST_SKIP() << "ffmpeg not available on this machine";
    }

    const std::string source = WriteTwoBlockTape("mp3.tzx");
    const std::string wav = TestPathHelper::GetTestScratchPath("tapeaudio-import/mp3.wav");
    const std::string mp3 = TestPathHelper::GetTestScratchPath("tapeaudio-import/mp3.mp3");
    ASSERT_TRUE(RenderWav(source, wav).ok);

    Subprocess encoder;
    ASSERT_TRUE(encoder.spawn(FFmpegProbe::findFFmpeg(),
                              {"-hide_banner", "-loglevel", "error", "-y", "-i", wav, "-b:a", "320k", mp3}));
    ASSERT_EQ(encoder.waitForFinished(30000), 0) << encoder.readAllStderr();

    TapeImportRequest request;
    request.sourcePath = mp3;
    const TapeImportResult imported = ImportAudioToTape(request);
    ASSERT_TRUE(imported.ok) << imported.errorText;
    EXPECT_EQ(imported.decoderUsed, "ffmpeg(mp3)");

    const std::string saved = TestPathHelper::GetTestScratchPath("tapeaudio-import/mp3-back.tzx");
    ASSERT_TRUE(SaveTapeImage(imported.image, saved).ok);
    const TapeImage roundTripped = LoadTape(saved);
    ASSERT_TRUE(roundTripped.IsUsable());
    ASSERT_GE(roundTripped.blocks.size(), 2u);
    EXPECT_EQ(roundTripped.blocks[0].data, HeaderBlock());
    EXPECT_EQ(roundTripped.blocks[1].data, DataBlock(300));
}

TEST(TapeAudioImporter_Test, SilenceOnlyInputIsAnError)
{
    TzxTapeBuilder builder;
    builder.AddPause(50);
    const std::string source = TestPathHelper::GetTestScratchPath("tapeaudio-import/silence.tzx");
    ASSERT_TRUE(TzxTapeBuilder::WriteToFile(builder.Bytes(), source));

    const std::string wav = TestPathHelper::GetTestScratchPath("tapeaudio-import/silence.wav");
    ASSERT_TRUE(RenderWav(source, wav).ok);

    TapeImportRequest request;
    request.sourcePath = wav;
    const TapeImportResult imported = ImportAudioToTape(request);
    EXPECT_FALSE(imported.ok);
    EXPECT_NE(imported.errorText.find("no tape signal"), std::string::npos);
}

TEST(TapeAudioImporter_Test, ErrorPathsAreStructured)
{
    TapeImportRequest missing;
    missing.sourcePath = TestPathHelper::GetTestScratchPath("tapeaudio-import/does-not-exist.wav");
    const TapeImportResult r1 = ImportAudioToTape(missing);
    EXPECT_FALSE(r1.ok);
    EXPECT_NE(r1.errorText.find("not readable"), std::string::npos);

    TzxTapeBuilder builder;
    builder.AddPause(10);
    const std::string source = TestPathHelper::GetTestScratchPath("tapeaudio-import/err.tzx");
    ASSERT_TRUE(TzxTapeBuilder::WriteToFile(builder.Bytes(), source));
    TapeImportRequest badExt;
    badExt.sourcePath = source;  // a .tzx is not audio
    const TapeImportResult r2 = ImportAudioToTape(badExt);
    EXPECT_FALSE(r2.ok);
    EXPECT_NE(r2.errorText.find("unsupported input extension"), std::string::npos);

    TapeImage image;
    image.blocks.push_back(TapeBlock{});
    const TapeSaveResult r3 = SaveTapeImage(image,
                                            TestPathHelper::GetTestScratchPath("tapeaudio-import/err.ogg"));
    EXPECT_FALSE(r3.ok);
    EXPECT_NE(r3.errorText.find("unsupported output extension"), std::string::npos);
}
