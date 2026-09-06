#include <gtest/gtest.h>

#include "loaders/disk/loader_hfe.h"
#include "emulator/io/fdc/diskimage.h"

#include <cstring>
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
