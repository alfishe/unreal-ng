// HistoryControl: the history verbs every automation plane forwards to (debugger/history/historycontrol.h). A read
// with a cursor (`since`) answers the records after it with their numbers and next_since / missed / restarted; a read
// without one answers as it always did

#include "debugger/history/historycontrol.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "debugger/history/devicehistory.h"
#include "emulator/emulatorcontext.h"

namespace
{

struct TestRecord
{
    int value = 0;

    void ToState(StateNode& node) const
    {
        node["value"] = value;
    }
};

class HistoryControl_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = std::make_unique<EmulatorContext>(LoggerLevel::LogError);
        _history = std::make_unique<history::Registered<TestRecord>>(&_context->histories, "test.dev", "things");
        std::string error;
        ASSERT_TRUE((*_history)->Configure({ .mode = history::Mode::Ring, .limit = 4 }, error)) << error;
        for (int i = 1; i <= 6; i++)
            (*_history)->Push({ i });
    }
    void TearDown() override
    {
        _history.reset();
        _context.reset();
    }

    HistoryReply Read(size_t last, std::optional<uint64_t> since)
    {
        HistoryRequest request;
        request.verb = "read";
        request.device = "test.dev";
        request.last = last;
        request.since = since;
        return HistoryControl::Execute(_context.get(), request);
    }

    std::unique_ptr<EmulatorContext> _context;
    std::unique_ptr<history::Registered<TestRecord>> _history;
};

}  // namespace

TEST_F(HistoryControl_Test, ReadWithoutSinceIsUnchanged)
{
    const HistoryReply reply = Read(2, std::nullopt);
    ASSERT_EQ(reply.httpStatus, 200);
    EXPECT_EQ(reply.body.find("next_since"), nullptr);
    EXPECT_EQ(reply.body.find("missed"), nullptr);
    EXPECT_EQ(reply.body.find("restarted"), nullptr);
    const StateNode* records = reply.body.find("records");
    ASSERT_NE(records, nullptr);
    ASSERT_EQ(records->items.size(), 2u);
    EXPECT_EQ(records->items[0].find("value")->i, 5) << "the newest two";
    EXPECT_EQ(records->items[0].find("seq"), nullptr) << "no numbers without a cursor";
    EXPECT_EQ(reply.body.find("written")->i, 6);
}

TEST_F(HistoryControl_Test, ReadSinceAnswersTheCursor)
{
    // Seq 1 and 2 were evicted from the ring of four: a reader at 1 missed one
    HistoryReply reply = Read(2, 1);
    ASSERT_EQ(reply.httpStatus, 200);
    const StateNode* records = reply.body.find("records");
    ASSERT_NE(records, nullptr);
    ASSERT_EQ(records->items.size(), 2u);
    EXPECT_EQ(records->items[0].find("seq")->i, 3);
    EXPECT_EQ(records->items[0].find("value")->i, 3);
    EXPECT_EQ(records->items[1].find("seq")->i, 4);
    EXPECT_EQ(reply.body.find("next_since")->i, 4);
    EXPECT_EQ(reply.body.find("missed")->i, 1);
    EXPECT_FALSE(reply.body.find("restarted")->b);
    EXPECT_EQ(reply.body.find("kept")->i, 4) << "the stats as before";

    reply = Read(0, 99);
    EXPECT_TRUE(reply.body.find("restarted")->b) << "a cursor past the newest record";
    EXPECT_EQ(reply.body.find("records")->items.size(), 4u) << "from the oldest kept";
    EXPECT_EQ(reply.body.find("next_since")->i, 6);
}
