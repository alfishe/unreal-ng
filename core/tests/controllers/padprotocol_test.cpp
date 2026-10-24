/// @file padprotocol_test.cpp
/// @brief H4 (haptics.md §9): PadPump runs a pad protocol on its own thread: the setters only queue, the writes and
/// feature reads happen there, the protocol's Final is written when the pump ends.

#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "hid/drivers/padprotocol.h"

using namespace controllers;

namespace
{
/// Reads feature 7 once, then writes [1, low byte] whenever the low level changed; Final writes [9]
class FakeProtocol : public IPadProtocol
{
public:
    std::optional<PadOutReport> Next(uint64_t nowMs, uint64_t& wakeMs) override
    {
        (void)nowMs;
        wakeMs = 0;
        PadOutReport out;
        if (!featureAsked)
        {
            featureAsked = true;
            out.kind = PadOutReport::Kind::GetFeature;
            out.featureId = 7;
            out.featureSize = 3;
            return out;
        }
        if (level == written)
            return std::nullopt;
        written = level;
        out.bytes = {1, level};
        return out;
    }
    void OnFeature(uint8_t featureId, const std::vector<uint8_t>& data, uint64_t nowMs) override
    {
        (void)nowMs;
        feature = featureId == 7 && data.size() == 3 ? data[2] : 0xFF;
    }
    void SetRumble(const RumbleLevels& levels) override
    {
        level = static_cast<uint8_t>(levels.low * 100);
    }
    std::optional<PadOutReport> Final() override
    {
        PadOutReport out;
        out.bytes = {9};
        return out;
    }

    bool featureAsked = false;
    uint8_t feature = 0;
    uint8_t level = 0;
    uint8_t written = 0;
};
}  // namespace

TEST(PadProtocol_Test, PumpWritesOnItsThreadAndFinalLast)
{
    std::mutex mutex;
    std::vector<std::vector<uint8_t>> writes;
    std::atomic<int> features{0};
    auto protocol = std::make_unique<FakeProtocol>();
    {
        PadPump pump(
            std::move(protocol),
            [&](const std::vector<uint8_t>& report) {
                std::lock_guard<std::mutex> lock(mutex);
                writes.push_back(report);
                return true;
            },
            [&](uint8_t id, size_t size, std::vector<uint8_t>& data) {
                data.assign(size, 0);
                data[0] = id;
                data[2] = 0x42;
                ++features;
                return true;
            });
        ASSERT_TRUE(TestWait::For([&] { return pump.Written() == 1; })) << "the feature read";
        EXPECT_EQ(features.load(), 1);
        pump.SetRumble({0.5f, 0, 0, 0});
        ASSERT_TRUE(TestWait::For([&] { return pump.Written() == 2; }));
        std::lock_guard<std::mutex> lock(mutex);
        ASSERT_EQ(writes.size(), 1u);
        EXPECT_EQ(writes[0], (std::vector<uint8_t>{1, 50}));
    }
    // The pump is gone: its thread joined, then Final written
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[1], (std::vector<uint8_t>{9}));
}
