// The sjasmplus codec (decision D-10): detection by sjasmplus-only directives, the dialect recorded, the byte-exact
// round trip, and the original code page kept on encoding (string literals are program bytes).

#include <gtest/gtest.h>

#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "testdata.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;

TEST(SjasmplusCodec_Test, DetectionByItsOwnDirectives)
{
    const codecs::SjasmplusCodec codec;
    EXPECT_GE(codec.Detect(ReadTestData("sjasmplus/hello.asm"), {}), 76) << "DEVICE + SAVESNA";
    EXPECT_GE(codec.Detect(ReadTestData("sjasmplus/module.asm"), {}), 86) << "DEVICE, MODULE, ENDMODULE, OUTPUT, OUTEND";
    EXPECT_LE(codec.Detect(ReadTestData("sjasmplus/plain.asm"), {}), 30);
    std::vector<uint8_t> binary = {0, 1, 2, 3, 0, 0, 0xFF};
    EXPECT_EQ(codec.Detect(binary, {}), 0);
}

TEST(SjasmplusCodec_Test, DecodeRecordsTheDialectAndRoundTrips)
{
    const codecs::SjasmplusCodec codec;
    for (const char* file : {"sjasmplus/hello.asm", "sjasmplus/module.asm", "text/source-cp866-crlf.asm"})
    {
        const auto bytes = ReadTestData(file);
        const DecodeResult decoded = codec.Decode(bytes, {});
        ASSERT_TRUE(decoded.ok) << file;
        EXPECT_EQ(decoded.document.dialect, "sjasmplus");
        EXPECT_EQ(decoded.document.format, "sjasmplus");
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, bytes) << file;
    }
}

TEST(SjasmplusCodec_Test, KeepsTheOriginalCodePageOfAnotherCodecsDocument)
{
    // A document decoded by another codec (here: text, CP866) written as sjasmplus keeps CP866 by default
    const codecs::TextCodec text;
    const codecs::SjasmplusCodec sjasmplus;
    const auto bytes = ReadTestData("text/source-cp866-lf.asm");
    const DecodeResult decoded = text.Decode(bytes, {});
    EXPECT_EQ(sjasmplus.Encode(decoded.document, {}).bytes, bytes);
}
