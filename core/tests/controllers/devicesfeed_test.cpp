/// @file devicesfeed_test.cpp
/// @brief The `controllers.devices` value (devicesfeed.h): the owner's poll builds the `devices` reply again only
/// after an arrival, a host verb or the status period, and reports a change only when the reply's bytes changed.

#include <gtest/gtest.h>

#include <memory>

#include "devicesfeed.h"
#include "fake/fakebackend.h"
#include "hostinputcontrol.h"
#include "hostinputservice.h"

using namespace controllers;

namespace
{
/// The Qt client's HostControllers reduced to what `devices` reads: the context's devices and the service
class DevicesOwner : public IHostInputOwner
{
public:
    ProfileStore& Profiles() override
    {
        return _store;
    }
    ProfileContext Context() const override
    {
        return context;
    }
    const ResolvedProfiles& Resolved() const override
    {
        return _resolved;
    }
    std::string UserProfileDir() const override
    {
        return {};
    }
    std::string ManualProfile() const override
    {
        return {};
    }
    void SetManualProfile(const std::string& id) override
    {
        (void)id;
    }
    void Reapply() override
    {
    }
    HostInputService* Input() override
    {
        return input;
    }
    ProfileContext context;
    HostInputService* input = nullptr;

private:
    ProfileStore _store;
    ResolvedProfiles _resolved;
};

const JsonValue* Device(const DevicesFeed& feed, const std::string& id)
{
    for (const JsonValue& item : feed.Value().Find("devices")->Items())
        if (item.Find("id")->AsString() == id)
            return &item;
    return nullptr;
}
}  // namespace

TEST(DevicesFeed_Test, BuildsOnlyAfterAnArrivalAVerbOrTheStatusPeriodAndTellsOnlyAChange)
{
    HostInputService service;
    auto owned = std::make_unique<FakeBackend>();
    FakeBackend* fake = owned.get();
    std::string reason;
    ASSERT_TRUE(service.AddBackend(std::move(owned), reason)) << reason;
    DevicesOwner owner;
    owner.input = &service;
    const HostDeviceInfo pad{"fake:1", "Pad", "dualsense", "playstation", "fake"};
    fake->SetBattery("fake:1", {true, 80, false, false, false});
    ASSERT_TRUE(fake->Connect(pad));
    owner.context.devices = {pad};

    DevicesFeed feed;
    DevicesFeed::Clock::time_point now{};
    ASSERT_TRUE(feed.Poll(owner, service.Revision(), now)) << "the first poll is a change";
    EXPECT_EQ(feed.Revision(), 1u);
    EXPECT_EQ(Device(feed, "fake:1")->Find("battery")->Find("text")->AsString(), "80 %");

    // Nothing happened: two compares, no reply built (the poll runs 4 times a second)
    for (int i = 0; i < 7; i++)
    {
        now += std::chrono::milliseconds(250);
        EXPECT_FALSE(feed.Poll(owner, service.Revision(), now));
    }
    EXPECT_EQ(feed.Builds(), 1u) << "within the status period nothing is built";

    // The battery the backend refreshed shows at the status period: built, changed
    fake->SetBattery("fake:1", {true, 40, false, true, false});
    now += std::chrono::milliseconds(250);
    ASSERT_TRUE(feed.Poll(owner, service.Revision(), now)) << "2 s since the build";
    EXPECT_EQ(feed.Builds(), 2u);
    EXPECT_EQ(feed.Revision(), 2u);
    EXPECT_EQ(Device(feed, "fake:1")->Find("battery")->Find("text")->AsString(), "charging, 40 %");

    // Built again at the next period, unchanged: not a change
    now += DevicesFeed::kStatusEvery;
    EXPECT_FALSE(feed.Poll(owner, service.Revision(), now));
    EXPECT_EQ(feed.Builds(), 3u);
    EXPECT_EQ(feed.Revision(), 2u);

    // An arrival: at once
    const HostDeviceInfo plain{"fake:2", "Plain", "generic", "generic", "fake"};
    ASSERT_TRUE(fake->Connect(plain));
    owner.context.devices = {pad, plain};
    now += std::chrono::milliseconds(250);
    ASSERT_TRUE(feed.Poll(owner, service.Revision(), now));
    EXPECT_NE(Device(feed, "fake:2"), nullptr);

    // A host verb that changes what devices says (the role): at once; a read verb builds nothing
    HostInputControl control(owner);
    ASSERT_TRUE(control.Execute({"probe", {}}).Ok());
    now += std::chrono::milliseconds(250);
    EXPECT_FALSE(feed.Poll(owner, service.Revision(), now));
    const uint64_t builds = feed.Builds();
    EXPECT_EQ(builds, 4u) << "probe only reads";
    ASSERT_TRUE(control.Execute({"role", {{"device", "fake:2"}, {"role", "disabled"}}}).Ok());
    now += std::chrono::milliseconds(250);
    ASSERT_TRUE(feed.Poll(owner, service.Revision(), now));
    EXPECT_EQ(Device(feed, "fake:2")->Find("role")->AsString(), "disabled");

    // A device that left: at once
    ASSERT_TRUE(fake->Disconnect("fake:2"));
    owner.context.devices = {pad};
    now += std::chrono::milliseconds(250);
    ASSERT_TRUE(feed.Poll(owner, service.Revision(), now));
    EXPECT_EQ(Device(feed, "fake:2"), nullptr);
    service.Stop();
}
