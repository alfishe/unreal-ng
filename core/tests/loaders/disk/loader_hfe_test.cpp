#include <gtest/gtest.h>

#include "loaders/disk/loader_hfe.h"
#include "emulator/io/fdc/diskimage.h"

#include <cstring>
#include <memory>
#include <vector>

class LoaderHFE_Test : public ::testing::Test
{
protected:
    std::vector<uint8_t> createMinimalHfeV1(uint8_t tracks = 80, uint8_t sides = 2, uint8_t encoding = 0)
    {
        std::vector<uint8_t> data(LoaderHFE::HEADER_SIZE, 0);

        std::memcpy(data.data(), LoaderHFE::SIGNATURE_V1, LoaderHFE::SIGNATURE_LEN);
        data[0x08] = 0;
        data[0x09] = tracks;
        data[0x0A] = sides;
        data[0x0B] = encoding;
        data[0x0C] = 250 & 0xFF;
        data[0x0D] = 250 >> 8;
        data[0x0E] = 300 & 0xFF;
        data[0x0F] = 300 >> 8;
        data[0x10] = 7;
        data[0x12] = 1;
        data[0x14] = 1;
        data[0x15] = 0;

        const size_t lutStart = LoaderHFE::HEADER_SIZE;
        const size_t lutSize = static_cast<size_t>(tracks) * 4;
        data.resize(lutStart + ((lutSize + 511) / 512) * 512, 0);

        const size_t trackDataStart = data.size() / 512;
        const size_t bitsPerTrack = 50000;
        const size_t bytesPerSide = (bitsPerTrack + 7) / 8;
        const size_t trackLength = ((bytesPerSide * 2 + 511) / 512) * 512;

        for (uint8_t t = 0; t < tracks; t++)
        {
            const size_t offset = trackDataStart + t * (trackLength / 512);
            uint8_t* entry = data.data() + lutStart + t * 4;
            entry[0] = static_cast<uint8_t>(offset & 0xFF);
            entry[1] = static_cast<uint8_t>(offset >> 8);
            entry[2] = static_cast<uint8_t>(trackLength & 0xFF);
            entry[3] = static_cast<uint8_t>(trackLength >> 8);
        }

        data.resize(trackDataStart * 512 + tracks * trackLength, 0x4E);

        return data;
    }

    std::vector<uint8_t> createMinimalHfeV3(uint8_t tracks = 80, uint8_t sides = 2)
    {
        std::vector<uint8_t> data = createMinimalHfeV1(tracks, sides);
        std::memcpy(data.data(), LoaderHFE::SIGNATURE_V3, LoaderHFE::SIGNATURE_LEN);
        return data;
    }
};

TEST_F(LoaderHFE_Test, Detect_V1)
{
    std::vector<uint8_t> data(16, 0);
    std::memcpy(data.data(), LoaderHFE::SIGNATURE_V1, LoaderHFE::SIGNATURE_LEN);

    EXPECT_EQ(LoaderHFE::detect(data.data(), data.size()), 1);
}

TEST_F(LoaderHFE_Test, Detect_V3)
{
    std::vector<uint8_t> data(16, 0);
    std::memcpy(data.data(), LoaderHFE::SIGNATURE_V3, LoaderHFE::SIGNATURE_LEN);

    EXPECT_EQ(LoaderHFE::detect(data.data(), data.size()), 3);
}

TEST_F(LoaderHFE_Test, Detect_NotHfe)
{
    std::vector<uint8_t> data(16, 0);
    std::memcpy(data.data(), "NOTANHFE", 8);

    EXPECT_EQ(LoaderHFE::detect(data.data(), data.size()), 0);
}

TEST_F(LoaderHFE_Test, Detect_TooShort)
{
    std::vector<uint8_t> data(4, 0);
    std::memcpy(data.data(), "HXCP", 4);

    EXPECT_EQ(LoaderHFE::detect(data.data(), data.size()), 0);
}

TEST_F(LoaderHFE_Test, Parse_MinimalV1_CreatesImage)
{
    std::vector<uint8_t> data = createMinimalHfeV1(40, 2);
    std::vector<std::string> warnings;

    LoaderHFE loader;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);

    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->getCylinders(), 40);
    EXPECT_EQ(image->getSides(), 2);

    delete image;
}

TEST_F(LoaderHFE_Test, Parse_MinimalV3_CreatesImage)
{
    std::vector<uint8_t> data = createMinimalHfeV3(80, 2);
    std::vector<std::string> warnings;

    LoaderHFE loader;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);

    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->getCylinders(), 80);
    EXPECT_EQ(image->getSides(), 2);

    delete image;
}

TEST_F(LoaderHFE_Test, Parse_FmEncoding_SetsFmMode)
{
    std::vector<uint8_t> data = createMinimalHfeV1(40, 1, LoaderHFE::ENCODING_ISOIBM_FM);
    std::vector<std::string> warnings;

    LoaderHFE loader;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);

    ASSERT_NE(image, nullptr);

    DiskImage::Track* track = image->getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->encoding(), DiskImage::Encoding::FM);

    delete image;
}

TEST_F(LoaderHFE_Test, Parse_TruncatedHeader_Fails)
{
    std::vector<uint8_t> data(256, 0);
    std::memcpy(data.data(), LoaderHFE::SIGNATURE_V1, LoaderHFE::SIGNATURE_LEN);
    std::vector<std::string> warnings;

    LoaderHFE loader;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);

    EXPECT_EQ(image, nullptr);
    EXPECT_FALSE(warnings.empty());
}

TEST_F(LoaderHFE_Test, Parse_ZeroTracks_Fails)
{
    std::vector<uint8_t> data = createMinimalHfeV1(0, 2);
    std::vector<std::string> warnings;

    LoaderHFE loader;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);

    EXPECT_EQ(image, nullptr);
    EXPECT_FALSE(warnings.empty());
}

TEST_F(LoaderHFE_Test, Parse_TooManyTracks_Fails)
{
    std::vector<uint8_t> data = createMinimalHfeV1(100, 2);
    std::vector<std::string> warnings;

    LoaderHFE loader;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);

    EXPECT_EQ(image, nullptr);
    EXPECT_FALSE(warnings.empty());
}

TEST_F(LoaderHFE_Test, Serialize_V3_RoundTrip)
{
    DiskImage image(40, 2);

    for (uint8_t c = 0; c < 40; c++)
    {
        for (uint8_t s = 0; s < 2; s++)
        {
            DiskImage::Track* track = image.getTrackForCylinderAndSide(c, s);
            DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::trdos(nullptr, 16);
            track->formatTrack(c, s, spec);
        }
    }

    LoaderHFE saver;
    saver.setDiskImage(&image);
    std::vector<uint8_t> buffer;
    std::vector<std::string> warnings;

    ASSERT_TRUE(saver.serialize(&image, buffer, warnings));
    ASSERT_GT(buffer.size(), LoaderHFE::HEADER_SIZE);

    LoaderHFE loader;
    DiskImage* loaded = loader.parse(buffer.data(), buffer.size(), warnings);

    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->getCylinders(), 40);
    EXPECT_EQ(loaded->getSides(), 2);

    delete loaded;
}

TEST_F(LoaderHFE_Test, Serialize_V1_RoundTrip)
{
    DiskImage image(40, 2);

    for (uint8_t c = 0; c < 40; c++)
    {
        for (uint8_t s = 0; s < 2; s++)
        {
            DiskImage::Track* track = image.getTrackForCylinderAndSide(c, s);
            DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::trdos(nullptr, 16);
            track->formatTrack(c, s, spec);
        }
    }

    LoaderHFE saver;
    std::vector<uint8_t> buffer;
    std::vector<std::string> warnings;

    ASSERT_TRUE(saver.serializeV1(&image, buffer, warnings));
    ASSERT_GT(buffer.size(), LoaderHFE::HEADER_SIZE);

    EXPECT_EQ(LoaderHFE::detect(buffer.data(), buffer.size()), 1);

    LoaderHFE loader;
    DiskImage* loaded = loader.parse(buffer.data(), buffer.size(), warnings);

    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->getCylinders(), 40);
    EXPECT_EQ(loaded->getSides(), 2);

    delete loaded;
}

TEST_F(LoaderHFE_Test, Serialize_FromTrdFormat_KeepsGeometry)
{
    DiskImage trdImage(80, 2);

    for (uint8_t c = 0; c < 80; c++)
    {
        for (uint8_t s = 0; s < 2; s++)
        {
            DiskImage::Track* track = trdImage.getTrackForCylinderAndSide(c, s);
            DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::trdos(nullptr, 16);
            track->formatTrack(c, s, spec);
        }
    }

    LoaderHFE hfeLoader;
    std::vector<uint8_t> hfeBuffer;
    std::vector<std::string> warnings;

    ASSERT_TRUE(hfeLoader.serialize(&trdImage, hfeBuffer, warnings));

    DiskImage* hfeImage = hfeLoader.parse(hfeBuffer.data(), hfeBuffer.size(), warnings);

    ASSERT_NE(hfeImage, nullptr);
    EXPECT_EQ(hfeImage->getCylinders(), 80);
    EXPECT_EQ(hfeImage->getSides(), 2);

    delete hfeImage;
}

/// region <Round trips: the tracks come back byte for byte>

namespace
{
void FormatAll(DiskImage& image, bool fm, int fmCylinders = -1)
{
    for (uint8_t c = 0; c < image.getCylinders(); c++)
    {
        for (uint8_t s = 0; s < image.getSides(); s++)
        {
            const bool trackFm = fm || c < fmCylinders;
            DiskImage::TrackFormatSpec spec =
                trackFm ? DiskImage::TrackFormatSpec::ibm3740() : DiskImage::TrackFormatSpec::trdos(nullptr, 16);
            image.getTrackForCylinderAndSide(c, s)->formatTrack(c, s, spec);
        }
    }
}

void ExpectSameTracks(DiskImage& expected, DiskImage& actual)
{
    ASSERT_EQ(actual.getCylinders(), expected.getCylinders());
    ASSERT_EQ(actual.getSides(), expected.getSides());
    for (uint8_t c = 0; c < expected.getCylinders(); c++)
    {
        for (uint8_t s = 0; s < expected.getSides(); s++)
        {
            DiskImage::Track* want = expected.getTrackForCylinderAndSide(c, s);
            DiskImage::Track* got = actual.getTrackForCylinderAndSide(c, s);
            ASSERT_EQ(got->encoding(), want->encoding()) << "cylinder " << int(c) << " side " << int(s);
            ASSERT_EQ(got->rawSize(), want->rawSize()) << "cylinder " << int(c) << " side " << int(s);
            ASSERT_EQ(0, std::memcmp(got->rawData(), want->rawData(), want->rawSize()))
                << "cylinder " << int(c) << " side " << int(s);
            ASSERT_EQ(got->clockBitmap(), want->clockBitmap()) << "cylinder " << int(c) << " side " << int(s);
            ASSERT_EQ(got->sectorCount(), want->sectorCount()) << "cylinder " << int(c) << " side " << int(s);
        }
    }
}

DiskImage* SaveAndLoad(DiskImage& image, int version, std::vector<uint8_t>* file = nullptr)
{
    LoaderHFE saver;
    std::vector<uint8_t> buffer;
    std::vector<std::string> warnings;
    const bool saved = version == 3 ? saver.serialize(&image, buffer, warnings) : saver.serializeV1(&image, buffer, warnings);
    EXPECT_TRUE(saved);
    if (!saved) return nullptr;
    if (file) *file = buffer;
    LoaderHFE loader;
    return loader.parse(buffer.data(), buffer.size(), warnings);
}
}  // namespace

TEST_F(LoaderHFE_Test, RoundTrip_V3_TrdosTracksByteExact)
{
    DiskImage image(80, 2);
    FormatAll(image, false);
    ASSERT_EQ(image.getTrackForCylinderAndSide(0, 0)->sectorCount(), 16u);

    std::unique_ptr<DiskImage> loaded(SaveAndLoad(image, 3));
    ASSERT_NE(loaded, nullptr);
    ExpectSameTracks(image, *loaded);
}

TEST_F(LoaderHFE_Test, RoundTrip_V1_TrdosTracksByteExact)
{
    DiskImage image(40, 2);
    FormatAll(image, false);

    std::unique_ptr<DiskImage> loaded(SaveAndLoad(image, 1));
    ASSERT_NE(loaded, nullptr);
    ExpectSameTracks(image, *loaded);
}

TEST_F(LoaderHFE_Test, RoundTrip_FmTracksStayFm)
{
    DiskImage image(40, 1);
    FormatAll(image, true);

    std::vector<uint8_t> file;
    std::unique_ptr<DiskImage> loaded(SaveAndLoad(image, 3, &file));
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(file[0x0B], LoaderHFE::ENCODING_ISOIBM_FM);
    ExpectSameTracks(image, *loaded);
}

TEST_F(LoaderHFE_Test, RoundTrip_MixedFmAndMfmKeepsEachTrack)
{
    DiskImage image(40, 2);
    FormatAll(image, false, 2);  // cylinders 0 and 1 FM, the rest MFM

    for (int version : {3, 1})
    {
        std::vector<uint8_t> file;
        std::unique_ptr<DiskImage> loaded(SaveAndLoad(image, version, &file));
        ASSERT_NE(loaded, nullptr) << "v" << version;
        EXPECT_EQ(file[0x0B], LoaderHFE::ENCODING_ISOIBM_MFM);
        ExpectSameTracks(image, *loaded);
    }
}

TEST_F(LoaderHFE_Test, RoundTrip_V3_KeepsWeakBytes)
{
    DiskImage image(40, 2);
    FormatAll(image, false);
    DiskImage::Track* track = image.getTrackForCylinderAndSide(5, 1);
    const size_t firstWeak = track->getDataForSector(3) - track->rawData() + 10;
    for (size_t b = firstWeak; b < firstWeak + 20; b++) track->setWeakByte(b, true);

    std::unique_ptr<DiskImage> loaded(SaveAndLoad(image, 3));
    ASSERT_NE(loaded, nullptr);
    DiskImage::Track* got = loaded->getTrackForCylinderAndSide(5, 1);
    ASSERT_EQ(got->rawSize(), track->rawSize());
    for (size_t b = firstWeak; b < firstWeak + 20; b++) EXPECT_TRUE(got->weakByte(b)) << b;
    EXPECT_FALSE(got->weakByte(firstWeak - 2));
    EXPECT_FALSE(got->weakByte(firstWeak + 22));
    EXPECT_EQ(got->sectorCount(), 16u);
    EXPECT_FALSE(loaded->getTrackForCylinderAndSide(5, 0)->hasWeakBits());
}

TEST_F(LoaderHFE_Test, Parse_V3Opcodes_GreaseweazleSemantics)
{
    // One side: SETBITRATE 72, SETINDEX, NOP, SKIPBITS 3 + 0x15 (cells 10101), RAND (8 weak cells), SKIPBITS 5 + RAND
    // (3 weak cells), then data 0x5A. 5 + 8 + 3 + 8 = 24 cells
    const std::vector<uint8_t> stream = {LoaderHFE::OP_SETBITRATE, 72, LoaderHFE::OP_SETINDEX, LoaderHFE::OP_NOP,
                                         LoaderHFE::OP_SKIPBITS,    3,  0x15,
                                         LoaderHFE::OP_RAND,        LoaderHFE::OP_SKIPBITS, 5, LoaderHFE::OP_RAND,
                                         0x5A};
    std::vector<uint8_t> bits, weak;
    unrealdisk::LoaderHFECUT::processV3Opcodes(stream, bits, weak);

    const std::vector<uint8_t> wantBits = {1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0};
    const std::vector<uint8_t> wantWeak = {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(bits, wantBits);
    EXPECT_EQ(weak, wantWeak);
}

/// endregion </Round trips: the tracks come back byte for byte>
