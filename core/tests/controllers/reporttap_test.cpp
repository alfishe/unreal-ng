/// @file reporttap_test.cpp
/// @brief The raw report tap of the test panel (ui-ux.md §3.4): nothing is kept until someone reads, then the newest
/// 16 reports oldest first (64 bytes of each, the size kept), the tap stops kIdleMs after the last read; and the
/// reports verb over the fake backend.

#include <gtest/gtest.h>

#include "_helpers/testpathhelper.h"
#include "fake/fakebackend.h"
#include "hid/reporttap.h"
#include "hostinputcontrol.h"
#include "hostinputservice.h"

using namespace controllers;

TEST(ReportTap_Test, KeepsReportsOnlyWhileRead)
{
    ReportTap tap;
    const uint8_t report[] = {0x20, 0x00, 0x1c};
    EXPECT_FALSE(tap.Armed());
    tap.Record(report, sizeof(report), 100);
    EXPECT_TRUE(tap.Read(100).empty()) << "nothing before the first read";
    EXPECT_TRUE(tap.Armed());
    tap.Record(report, sizeof(report), 150);
    std::vector<RawReport> reports = tap.Read(200);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(RawReportHex(reports[0]), "20 00 1c");
    EXPECT_EQ(reports[0].size, 3u);
    EXPECT_EQ(reports[0].timeMs, 150u);

    std::vector<uint8_t> big(80);
    for (size_t i = 0; i < big.size(); i++)
        big[i] = static_cast<uint8_t>(i);
    for (int n = 0; n < 20; n++)
    {
        big[0] = static_cast<uint8_t>(n);
        tap.Record(big.data(), big.size(), 300 + static_cast<uint64_t>(n));
    }
    reports = tap.Read(400);
    ASSERT_EQ(reports.size(), ReportTap::kReports);
    EXPECT_EQ(reports.front().bytes[0], 4) << "the oldest kept: 20 recorded, the last 16 stay";
    EXPECT_EQ(reports.back().bytes[0], 19);
    EXPECT_EQ(reports.back().bytes.size(), ReportTap::kBytes);
    EXPECT_EQ(reports.back().size, 80u);

    tap.Record(report, sizeof(report), 400 + ReportTap::kIdleMs + 1);
    EXPECT_FALSE(tap.Armed()) << "idle: the tap stops";
    EXPECT_EQ(tap.Read(5000).size(), ReportTap::kReports) << "what it had stays readable";
}

TEST(ReportTap_Test, ReportsAndProbeVerbsOverTheFakeBackend)
{
    HostInputService service;
    auto owned = std::make_unique<FakeBackend>();
    FakeBackend* fake = owned.get();
    std::string reason;
    ASSERT_TRUE(service.AddBackend(std::move(owned), reason)) << reason;
    const HostDeviceInfo pad{"fake:1", "Pad", "generic", "generic", "fake"};
    ASSERT_TRUE(fake->Connect(pad));
    fake->SetHasLight("fake:1", true);

    class Owner : public IHostInputOwner
    {
    public:
        explicit Owner(HostInputService& service) : _service(service)
        {
            _context.devices = {{"fake:1", "Pad", "generic", "generic", "fake"}};
        }
        ProfileStore& Profiles() override
        {
            return _store;
        }
        ProfileContext Context() const override
        {
            return _context;
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
        void SetManualProfile(const std::string&) override
        {
        }
        void Reapply() override
        {
        }
        HostInputService* Input() override
        {
            return &_service;
        }

    private:
        HostInputService& _service;
        ProfileStore _store;
        ProfileContext _context;
        ResolvedProfiles _resolved;
    } owner(service);
    HostInputControl control(owner);

    HostReply reply = control.Execute({"reports", {{"device", "fake:1"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(reply.body.Find("reports")->Items().empty()) << "the first read starts the tap";
    fake->Report("fake:1", {0x01, 0x80, 0x7f});
    reply = control.Execute({"reports", {{"device", "fake:1"}}});
    ASSERT_EQ(reply.body.Find("reports")->Items().size(), 1u);
    EXPECT_EQ(reply.body.Find("reports")->Items()[0].Find("hex")->AsString(), "01 80 7f");
    EXPECT_EQ(control.Execute({"reports", {{"device", "fake:9"}}}).error, "unknown-device");

    reply = control.Execute({"probe", {{"device", "fake:1"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.Find("backends")->Items()[0].Find("name")->AsString(), "fake");
    const std::vector<JsonValue>& endpoints = reply.body.Find("endpoints")->Items();
    ASSERT_EQ(endpoints.size(), 1u) << "a backend that lists none: the device as one endpoint";
    EXPECT_EQ(endpoints[0].Find("role")->AsString(), "input");
    EXPECT_TRUE(endpoints[0].Find("light")->AsBool());
    EXPECT_TRUE(endpoints[0].Find("rumble")->AsBool()) << "the fake backend rumbles every device";

    EXPECT_EQ(control.Execute({"player", {{"device", "fake:1"}, {"connector", "kempston"}}}).error, "unavailable")
        << "an owner that does not assign connectors says so";
    reply = control.Execute({"player", {{"device", "fake:1"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_FALSE(reply.body.Find("chosen")->AsBool());
}
