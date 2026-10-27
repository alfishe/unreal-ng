// The codec registry and detection (architecture.md DT-1)

#include <gtest/gtest.h>

#include "testdata.h"
#include "unrealasm/registry.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;

TEST(Registry_Test, BuiltinCodecs)
{
    const CodecRegistry& registry = CodecRegistry::Builtin();
    ASSERT_NE(registry.Find("text"), nullptr);
    ASSERT_NE(registry.Find("sjasmplus"), nullptr);
    EXPECT_EQ(registry.Find("nonexistent"), nullptr);
    EXPECT_EQ(registry.Find("sjasmplus")->Info().dialect, "sjasmplus");
}

TEST(Registry_Test, DetectionChoosesOrExplains)
{
    const CodecRegistry& registry = CodecRegistry::Builtin();
    const DetectResult sjasm = registry.Detect(ReadTestData("sjasmplus/hello.asm"));
    ASSERT_NE(sjasm.chosen, nullptr) << sjasm.reason;
    EXPECT_EQ(sjasm.chosen->Info().id, "sjasmplus");

    const DetectResult plain = registry.Detect(ReadTestData("sjasmplus/plain.asm"));
    ASSERT_NE(plain.chosen, nullptr) << plain.reason;
    EXPECT_EQ(plain.chosen->Info().id, "text") << "no sjasmplus-only directive: plain text";

    std::vector<uint8_t> binary(512);
    for (size_t i = 0; i < binary.size(); ++i)
        binary[i] = static_cast<uint8_t>(i * 7);
    const DetectResult none = registry.Detect(binary);
    EXPECT_EQ(none.chosen, nullptr);
    EXPECT_FALSE(none.reason.empty());
}
