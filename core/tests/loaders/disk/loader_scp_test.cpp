#include <gtest/gtest.h>

#include "loaders/disk/loader_scp.h"
#include "emulator/io/fdc/diskimage.h"

#include <cstring>
#include <vector>

class LoaderSCP_Test : public ::testing::Test
{
protected:
    std::vector<uint8_t> createMinimalScp(uint8_t cylinders = 40, uint8_t sides = 2, uint8_t revolutions = 1)
    {
        const uint8_t startTrack = 0;
        const uint8_t endTrack = static_cast<uint8_t>((cylinders - 1) * 2 + (sides - 1));

        std::vector<uint8_t> data;

        data.push_back('S');
        data.push_back('C');
        data.push_back('P');
        data.push_back(0x20);
        data.push_back(0);
        data.push_back(revolutions);
        data.push_back(startTrack);
        data.push_back(endTrack);
        data.push_back(LoaderSCP::FLAG_INDEX | LoaderSCP::FLAG_NORMALISED);
        data.push_back(0);
        data.push_back(sides == 1 ? LoaderSCP::HEADS_SIDE0 : LoaderSCP::HEADS_BOTH);
        data.push_back(0);

        for (int i = 0; i < 4; i++) data.push_back(0);

        const size_t trackTableStart = data.size();
        for (size_t i = 0; i < LoaderSCP::TRACK_TABLE_ENTRIES; i++)
        {
            data.push_back(0);
            data.push_back(0);
            data.push_back(0);
            data.push_back(0);
        }

        for (uint8_t c = 0; c < cylinders; c++)
        {
            for (uint8_t s = 0; s < sides; s++)
            {
                const uint8_t trackNumber = c * 2 + s;
                const size_t trackOffset = data.size();

                const size_t tableEntry = trackTableStart + trackNumber * 4;
                data[tableEntry] = static_cast<uint8_t>(trackOffset & 0xFF);
                data[tableEntry + 1] = static_cast<uint8_t>((trackOffset >> 8) & 0xFF);
                data[tableEntry + 2] = static_cast<uint8_t>((trackOffset >> 16) & 0xFF);
                data[tableEntry + 3] = static_cast<uint8_t>((trackOffset >> 24) & 0xFF);

                data.push_back('T');
                data.push_back('R');
                data.push_back('K');
                data.push_back(trackNumber);

                const uint32_t indexTime = 5000000 / 25;
                const uint32_t fluxCount = 100;
                const uint32_t dataOffset = LoaderSCP::REV_HEADER_SIZE;

                data.push_back(static_cast<uint8_t>(indexTime & 0xFF));
                data.push_back(static_cast<uint8_t>((indexTime >> 8) & 0xFF));
                data.push_back(static_cast<uint8_t>((indexTime >> 16) & 0xFF));
                data.push_back(static_cast<uint8_t>((indexTime >> 24) & 0xFF));
                data.push_back(static_cast<uint8_t>(fluxCount & 0xFF));
                data.push_back(static_cast<uint8_t>((fluxCount >> 8) & 0xFF));
                data.push_back(static_cast<uint8_t>((fluxCount >> 16) & 0xFF));
                data.push_back(static_cast<uint8_t>((fluxCount >> 24) & 0xFF));
                data.push_back(static_cast<uint8_t>(dataOffset & 0xFF));
                data.push_back(static_cast<uint8_t>((dataOffset >> 8) & 0xFF));
                data.push_back(static_cast<uint8_t>((dataOffset >> 16) & 0xFF));
                data.push_back(static_cast<uint8_t>((dataOffset >> 24) & 0xFF));

                for (uint32_t i = 0; i < fluxCount; i++)
                {
                    data.push_back(0);
                    data.push_back(80);
                }
            }
        }

        return data;
    }
};

TEST_F(LoaderSCP_Test, Detect_Valid)
{
    std::vector<uint8_t> data(16, 0);
    std::memcpy(data.data(), LoaderSCP::SIGNATURE, LoaderSCP::SIGNATURE_LEN);

    EXPECT_TRUE(LoaderSCP::detect(data.data(), data.size()));
}

TEST_F(LoaderSCP_Test, Detect_NotScp)
{
    std::vector<uint8_t> data(16, 0);
    std::memcpy(data.data(), "HFE", 3);

    EXPECT_FALSE(LoaderSCP::detect(data.data(), data.size()));
}

TEST_F(LoaderSCP_Test, Detect_TooShort)
{
    std::vector<uint8_t> data(2, 0);
    std::memcpy(data.data(), "SC", 2);

    EXPECT_FALSE(LoaderSCP::detect(data.data(), data.size()));
}

TEST_F(LoaderSCP_Test, Parse_Minimal_CreatesImage)
{
    std::vector<uint8_t> data = createMinimalScp(40, 2);
    std::vector<std::string> warnings;

    LoaderSCP loader;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);

    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->getCylinders(), 40);
    EXPECT_EQ(image->getSides(), 2);

    delete image;
}

TEST_F(LoaderSCP_Test, Parse_SingleSided_CreatesImage)
{
    std::vector<uint8_t> data = createMinimalScp(20, 1);
    std::vector<std::string> warnings;

    LoaderSCP loader;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);

    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->getCylinders(), 20);
    EXPECT_EQ(image->getSides(), 1);

    delete image;
}

TEST_F(LoaderSCP_Test, Parse_TruncatedHeader_Fails)
{
    std::vector<uint8_t> data(32, 0);
    std::memcpy(data.data(), LoaderSCP::SIGNATURE, LoaderSCP::SIGNATURE_LEN);
    std::vector<std::string> warnings;

    LoaderSCP loader;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);

    EXPECT_EQ(image, nullptr);
    EXPECT_FALSE(warnings.empty());
}

TEST_F(LoaderSCP_Test, Parse_ZeroRevolutions_Fails)
{
    std::vector<uint8_t> data = createMinimalScp(40, 2, 0);
    data[5] = 0;
    std::vector<std::string> warnings;

    LoaderSCP loader;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);

    EXPECT_EQ(image, nullptr);
    EXPECT_FALSE(warnings.empty());
}

TEST_F(LoaderSCP_Test, Serialize_RoundTrip)
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

    LoaderSCP saver;
    std::vector<uint8_t> buffer;
    std::vector<std::string> warnings;

    ASSERT_TRUE(saver.serialize(&image, buffer, warnings));
    ASSERT_GT(buffer.size(), LoaderSCP::HEADER_SIZE);

    EXPECT_TRUE(LoaderSCP::detect(buffer.data(), buffer.size()));

    LoaderSCP loader;
    DiskImage* loaded = loader.parse(buffer.data(), buffer.size(), warnings);

    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->getCylinders(), 40);
    EXPECT_EQ(loaded->getSides(), 2);

    delete loaded;
}

TEST_F(LoaderSCP_Test, Serialize_80Track_Sets96TpiFlag)
{
    DiskImage image(80, 2);

    for (uint8_t c = 0; c < 80; c++)
    {
        for (uint8_t s = 0; s < 2; s++)
        {
            DiskImage::Track* track = image.getTrackForCylinderAndSide(c, s);
            DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::trdos(nullptr, 16);
            track->formatTrack(c, s, spec);
        }
    }

    LoaderSCP saver;
    std::vector<uint8_t> buffer;
    std::vector<std::string> warnings;

    ASSERT_TRUE(saver.serialize(&image, buffer, warnings));

    EXPECT_TRUE(buffer[8] & LoaderSCP::FLAG_96TPI);
}

TEST_F(LoaderSCP_Test, Serialize_FmTrack_DetectedOnLoad)
{
    DiskImage image(40, 2);

    for (uint8_t c = 0; c < 40; c++)
    {
        for (uint8_t s = 0; s < 2; s++)
        {
            DiskImage::Track* track = image.getTrackForCylinderAndSide(c, s);
            DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::ibm3740();
            track->formatTrack(c, s, spec);
        }
    }

    LoaderSCP saver;
    std::vector<uint8_t> buffer;
    std::vector<std::string> warnings;

    ASSERT_TRUE(saver.serialize(&image, buffer, warnings));

    LoaderSCP loader;
    DiskImage* loaded = loader.parse(buffer.data(), buffer.size(), warnings);

    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->getCylinders(), 40);
    EXPECT_EQ(loaded->getSides(), 2);

    DiskImage::Track* track = loaded->getTrackForCylinderAndSide(0, 0);
    EXPECT_EQ(track->encoding(), DiskImage::Encoding::FM);

    delete loaded;
}
