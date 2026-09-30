// DNS query parsing and answer building (network adapters TDD §5.2)

#include <gtest/gtest.h>

#include "common/network/dnsmessage.h"
#include "common/network/nettypes.h"

namespace
{
std::vector<uint8_t> Query(const std::string& name, uint16_t qtype = dns::kTypeA)
{
    std::vector<uint8_t> q = {0x11, 0x22, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    size_t start = 0;
    while (start <= name.size())
    {
        size_t dot = name.find('.', start);
        if (dot == std::string::npos)
            dot = name.size();
        q.push_back(static_cast<uint8_t>(dot - start));
        q.insert(q.end(), name.begin() + static_cast<std::ptrdiff_t>(start), name.begin() + static_cast<std::ptrdiff_t>(dot));
        start = dot + 1;
    }
    q.push_back(0);
    q.insert(q.end(), {uint8_t(qtype >> 8), uint8_t(qtype), 0, 1});
    return q;
}
}  // namespace

TEST(DnsMessage_Test, ParsesTheZxdbQuery)
{
    auto q = Query("Next.ZXArt.ee");
    dns::Question question;
    ASSERT_TRUE(dns::ParseQuery(q.data(), q.size(), question));
    EXPECT_EQ(question.id, 0x1122);
    EXPECT_EQ(question.name, "next.zxart.ee");
    EXPECT_EQ(question.qtype, dns::kTypeA);
    EXPECT_EQ(question.questionEnd, q.size());
}

TEST(DnsMessage_Test, AnswerCarriesTheAddresses)
{
    auto q = Query("next.zxart.ee");
    dns::Question question;
    ASSERT_TRUE(dns::ParseQuery(q.data(), q.size(), question));
    auto a = dns::BuildAnswer(q.data(), q.size(), question, {NetIp(1, 2, 3, 4), NetIp(5, 6, 7, 8)}, dns::kRcodeNoError);
    ASSERT_EQ(a.size(), q.size() + 2 * 16);
    EXPECT_EQ(a[2] & 0x80, 0x80) << "QR";
    EXPECT_EQ(a[7], 2) << "ANCOUNT";
    EXPECT_EQ(a[q.size()], 0xC0) << "name pointer";
    EXPECT_EQ(a[q.size() + 12], 1);
    EXPECT_EQ(a[q.size() + 15], 4);
}

TEST(DnsMessage_Test, NxDomainHasNoAnswer)
{
    auto q = Query("nope.invalid");
    dns::Question question;
    ASSERT_TRUE(dns::ParseQuery(q.data(), q.size(), question));
    auto a = dns::BuildAnswer(q.data(), q.size(), question, {}, dns::kRcodeNxDomain);
    EXPECT_EQ(a[3] & 0x0F, dns::kRcodeNxDomain);
    EXPECT_EQ(a[7], 0);
}

TEST(DnsMessage_Test, NonAQuestionGetsAnEmptyAnswer)
{
    auto q = Query("example.com", 28);  // AAAA
    dns::Question question;
    ASSERT_TRUE(dns::ParseQuery(q.data(), q.size(), question));
    auto a = dns::BuildAnswer(q.data(), q.size(), question, {NetIp(1, 2, 3, 4)}, dns::kRcodeNoError);
    EXPECT_EQ(a[7], 0);
}

TEST(DnsMessage_Test, RejectsResponsesAndTruncatedQueries)
{
    auto q = Query("a.b");
    dns::Question question;
    q[2] |= 0x80;
    EXPECT_FALSE(dns::ParseQuery(q.data(), q.size(), question));
    q[2] &= 0x7F;
    EXPECT_FALSE(dns::ParseQuery(q.data(), 14, question));
}

TEST(NetTypes_Test, IpTextRoundTrip)
{
    uint32_t ip = 0;
    ASSERT_TRUE(NetIpFromString("192.168.1.177", ip));
    EXPECT_EQ(ip, NetIp(192, 168, 1, 177));
    EXPECT_EQ(NetIpToString(ip), "192.168.1.177");
    EXPECT_FALSE(NetIpFromString("256.1.1.1", ip));
    EXPECT_FALSE(NetIpFromString("1.2.3", ip));
    EXPECT_FALSE(NetIpFromString("a.b.c.d", ip));
}
