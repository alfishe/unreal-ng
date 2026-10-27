// CompanionEventHub: /api/v1/companions/events (companion protocol §4.1), driven without a network: each "connection"
// is a send and a close function, the view of `me` a fake the test changes

#include "pch.h"

#include "companion-event-hub.h"

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/testwaithelper.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/platform.h"

#include <json/json.h>

#include <chrono>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr const char* kToken = "unc_events-secret-0123456789";

/// What one client received, parsed, and whether it was closed
class Inbox
{
public:
    CompanionEventHub::Send Sender()
    {
        return [this](const std::string& text) {
            Json::Value value;
            Json::CharReaderBuilder reader;
            std::string errors;
            std::istringstream stream(text);
            Json::parseFromStream(reader, stream, &value, &errors);
            std::lock_guard<std::mutex> lock(_mutex);
            _messages.push_back(value);
        };
    }
    CompanionEventHub::Close Closer()
    {
        return [this] {
            std::lock_guard<std::mutex> lock(_mutex);
            _closed = true;
        };
    }
    std::vector<Json::Value> Messages()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _messages;
    }
    Json::Value Last()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _messages.empty() ? Json::Value() : _messages.back();
    }
    bool Closed()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _closed;
    }

private:
    std::mutex _mutex;
    std::vector<Json::Value> _messages;
    bool _closed = false;
};

Json::Value Entry(const std::string& id, const std::string& state)
{
    Json::Value e;
    e["id"] = id;
    e["state"] = state;
    e["actions"]["step"]["available"] = state == "paused";
    return e;
}
}  // namespace

class CompanionEventHub_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Registry().ResetForTesting();
        ASSERT_EQ(Registry().AddFromSpec(std::string("panel:") + kToken), 1u);
        _hub.SetSnapshot([this](const companion::CompanionInfo& companion) {
            std::lock_guard<std::mutex> lock(_viewMutex);
            Json::Value me;
            me["companion"]["name"] = companion.name;
            me["emulators"] = Json::Value(Json::arrayValue);
            for (const Json::Value& e : _emulators)
                me["emulators"].append(e);
            ++_built;
            return me;
        });
    }
    void TearDown() override
    {
        _hub.Stop();
        Registry().ResetForTesting();
    }

    static companion::CompanionRegistry& Registry() { return companion::CompanionRegistry::Instance(); }

    void SetView(std::vector<Json::Value> emulators)
    {
        std::lock_guard<std::mutex> lock(_viewMutex);
        _emulators = std::move(emulators);
    }

    void Connect(Inbox& inbox, const std::string& token = kToken)
    {
        _hub.Open(&inbox, token, inbox.Sender(), inbox.Closer());
    }

    CompanionEventHub _hub;
    std::mutex _viewMutex;
    std::vector<Json::Value> _emulators{Entry("e0", "running")};
    int _built = 0;
};

TEST_F(CompanionEventHub_Test, TheHandshakeTokenGetsTheWholeViewFirst)
{
    Inbox inbox;
    Connect(inbox);
    ASSERT_EQ(inbox.Messages().size(), 1u);
    const Json::Value hello = inbox.Last();
    EXPECT_EQ(hello["op"].asString(), "hello");
    EXPECT_EQ(hello["seq"].asUInt64(), 1u);
    EXPECT_EQ(hello["companion"]["name"].asString(), "panel");
    ASSERT_EQ(hello["emulators"].size(), 1u);
    EXPECT_EQ(hello["emulators"][0]["state"].asString(), "running");
}

TEST_F(CompanionEventHub_Test, OnlyAChangedInstanceIsSent)
{
    Inbox inbox;
    Connect(inbox);
    _hub.Refresh();
    EXPECT_EQ(inbox.Messages().size(), 1u) << "nothing changed: nothing sent";

    SetView({Entry("e0", "paused"), Entry("e1", "running")});
    _hub.Refresh();
    const std::vector<Json::Value> messages = inbox.Messages();
    ASSERT_EQ(messages.size(), 3u);
    EXPECT_EQ(messages[1]["op"].asString(), "emulator");
    EXPECT_EQ(messages[1]["emulator"]["id"].asString(), "e0");
    EXPECT_TRUE(messages[1]["emulator"]["actions"]["step"]["available"].asBool());
    EXPECT_EQ(messages[2]["emulator"]["id"].asString(), "e1") << "a new instance is a change too";
    EXPECT_EQ(messages[2]["seq"].asUInt64(), 3u);

    SetView({Entry("e1", "running")});
    _hub.Refresh();
    EXPECT_EQ(inbox.Last()["op"].asString(), "emulator_gone");
    EXPECT_EQ(inbox.Last()["id"].asString(), "e0");
    EXPECT_EQ(inbox.Messages().size(), 4u);
}

TEST_F(CompanionEventHub_Test, AClientWithoutHeadersAuthenticatesByMessage)
{
    Inbox inbox;
    Connect(inbox, "");
    EXPECT_TRUE(inbox.Messages().empty());
    _hub.Refresh();
    EXPECT_TRUE(inbox.Messages().empty()) << "nothing for an unauthenticated connection";
    EXPECT_EQ(_built, 0);

    _hub.HandleMessage(&inbox, R"({"op":"snapshot"})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "companion_required");

    _hub.HandleMessage(&inbox, std::string(R"({"op":"auth","token":")") + kToken + "\"}");
    EXPECT_EQ(inbox.Last()["op"].asString(), "hello");
    _hub.HandleMessage(&inbox, R"({"op":"snapshot"})");
    EXPECT_EQ(inbox.Last()["op"].asString(), "snapshot");
    EXPECT_EQ(inbox.Last()["emulators"].size(), 1u);
    _hub.HandleMessage(&inbox, R"({"op":"auth","token":"x"})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request");
    EXPECT_FALSE(inbox.Closed());
}

TEST_F(CompanionEventHub_Test, AnUnknownTokenIsToldAndClosed)
{
    Inbox inbox;
    Connect(inbox, "unc_not-a-companion-token-0000");
    EXPECT_EQ(inbox.Last()["error"].asString(), "companion_unknown");
    EXPECT_TRUE(inbox.Closed());
}

TEST_F(CompanionEventHub_Test, ARevokedCompanionIsToldAndClosed)
{
    Inbox inbox;
    Connect(inbox);
    ASSERT_EQ(Registry().Revoke("panel"), 1u);
    _hub.Refresh();
    EXPECT_EQ(inbox.Last()["op"].asString(), "revoked");
    EXPECT_TRUE(inbox.Closed());
    const size_t count = inbox.Messages().size();
    _hub.Refresh();
    EXPECT_EQ(inbox.Messages().size(), count) << "told once";
    _hub.Remove(&inbox);
    EXPECT_EQ(_hub.Connections(), 0u);
}

TEST_F(CompanionEventHub_Test, ANotificationRefreshes)
{
    _hub.Start();
    Inbox inbox;
    Connect(inbox);
    SetView({Entry("e0", "paused")});
    MessageCenter::DefaultMessageCenter().Post(NC_RUN_CONTROL_CHANGED, nullptr, true);
    ASSERT_TRUE(TestWait::For([&] { return inbox.Messages().size() == 2; }, std::chrono::seconds(2)));
    EXPECT_EQ(inbox.Last()["emulator"]["state"].asString(), "paused");
}
