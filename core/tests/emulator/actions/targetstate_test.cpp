// TargetState (Stream Deck integration targeting.md): the views and their focus order, the followed instance (rules
// 1-6), the pins of the surface sessions and the lifetime rules (§5), and NC_SURFACE_TARGET. Each test runs its own
// state over a fake rule 4 and a fake instance list: no machine is created.

#include "pch.h"

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/testwaithelper.h"
#include "emulator/actions/targetstate.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

using namespace actions;

namespace
{
/// Rule 4's answer and the instances that exist, as a test sets them
struct FakeHost
{
    std::mutex mutex;
    std::string selected;
    std::set<std::string> instances = {"A", "B", "C"};

    TargetState::Fallback Fallback()
    {
        return [this] {
            std::lock_guard<std::mutex> lock(mutex);
            if (!selected.empty())
                return std::make_pair(selected, FollowSource::Selected);
            return instances.size() == 1 ? std::make_pair(*instances.begin(), FollowSource::Sole)
                                         : std::make_pair(std::string(), FollowSource::None);
        };
    }
    TargetState::Exists Exists()
    {
        return [this](const std::string& id) {
            std::lock_guard<std::mutex> lock(mutex);
            return instances.count(id) > 0;
        };
    }
};

class TargetState_Test : public ::testing::Test
{
protected:
    FakeHost _host;
    TargetState _state{_host.Fallback(), false, _host.Exists()};

    std::string PinError(const std::string& session, const Pin& pin)
    {
        std::string error;
        std::string message;
        return _state.SetPin(session, pin, error, message) ? std::string() : error;
    }
};
}  // namespace

TEST(TargetPin_Test, TextFormsRoundTrip)
{
    Pin pin;
    std::string error;
    ASSERT_TRUE(Pin::FromText("follow", pin, error));
    EXPECT_EQ(pin, Pin::Follow());
    ASSERT_TRUE(Pin::FromText("", pin, error));
    EXPECT_EQ(pin.ToText(), "follow");
    ASSERT_TRUE(Pin::FromText("view:v2", pin, error));
    EXPECT_EQ(pin, Pin::OnView("v2"));
    EXPECT_STREQ(pin.ModeName(), "view");
    ASSERT_TRUE(Pin::FromText("instance:abc", pin, error));
    EXPECT_EQ(pin.ToText(), "instance:abc");
    EXPECT_FALSE(Pin::FromText("view:", pin, error));
    EXPECT_FALSE(Pin::FromText("window:1", pin, error));
    EXPECT_NE(error.find("follow, view:<id> or instance:<id>"), std::string::npos);
}

TEST_F(TargetState_Test, HeadlessFollowsTheSelectedElseTheSoleInstance)
{
    EXPECT_FALSE(_state.HasViews());
    EXPECT_EQ(_state.Followed(), "") << "three instances, none selected";
    EXPECT_EQ(_state.FollowedTarget().source, FollowSource::None);
    _host.selected = "B";
    EXPECT_EQ(_state.Followed(), "B");
    EXPECT_EQ(_state.FollowedTarget().source, FollowSource::Selected);
    _host.selected.clear();
    _host.instances = {"C"};
    EXPECT_EQ(_state.Followed(), "C");
    EXPECT_EQ(_state.FollowedTarget().source, FollowSource::Sole);
}

TEST_F(TargetState_Test, TheFrontViewWithAnActiveInstanceIsFollowed)
{
    ViewHandle main = _state.RegisterView("Main window", "unreal-qt");
    ViewHandle second = _state.RegisterView("Window 2", "unreal-qt");
    EXPECT_EQ(main.Id(), "v1");
    EXPECT_EQ(second.Id(), "v2");
    _host.selected = "C";
    EXPECT_EQ(_state.Followed(), "C") << "no view has an active instance yet: rule 4";

    _state.SetActive(main, "A");
    _state.SetActive(second, "B");
    EXPECT_EQ(_state.Followed(), "A") << "registration order until a view is activated";
    const TargetInfo followed = _state.FollowedTarget();
    EXPECT_EQ(followed.view, "v1");
    EXPECT_EQ(followed.viewName, "Main window");
    EXPECT_EQ(followed.source, FollowSource::View);

    _state.Activated(second);
    EXPECT_EQ(_state.Followed(), "B") << "rule 2: the focused view moves to the front";
    std::vector<ViewInfo> views = _state.Views();
    ASSERT_EQ(views.size(), 2u);
    EXPECT_EQ(views[0].id, "v2");
    EXPECT_EQ(views[1].id, "v1");

    _state.SetActive(second, "");
    EXPECT_EQ(_state.Followed(), "A") << "rule 3: a view without an active instance is skipped";

    _host.selected = "B";
    EXPECT_EQ(_state.Followed(), "A") << "rule 5: a selection does not change the focus order";
    EXPECT_EQ(_state.Views()[0].id, "v2");
}

TEST_F(TargetState_Test, PinsFollowTheirViewOrStayOnTheirInstance)
{
    ViewHandle main = _state.RegisterView("Main window");
    ViewHandle second = _state.RegisterView("Window 2");
    _state.SetActive(main, "A");
    _state.SetActive(second, "B");
    SessionHandle deck = _state.OpenSession({"Stream Deck + XL", "companion", "A1B2C3", "cid"});
    SessionHandle script = _state.OpenSession({"script", "api", "", ""});
    EXPECT_EQ(_state.TargetFor(deck.Id())->instance, "A") << "follow by default";

    EXPECT_EQ(PinError(deck.Id(), Pin::OnView(second.Id())), "");
    EXPECT_EQ(PinError(script.Id(), Pin::OnInstance("C")), "");
    _state.Activated(main);
    EXPECT_EQ(_state.TargetFor(deck.Id())->instance, "B") << "a view pin holds through focus changes";
    EXPECT_EQ(_state.TargetFor(deck.Id())->view, second.Id());
    _state.SetActive(second, "C");
    EXPECT_EQ(_state.TargetFor(deck.Id())->instance, "C") << "and follows the view's active instance";
    const TargetInfo onC = *_state.TargetFor(script.Id());
    EXPECT_EQ(onC.instance, "C");
    EXPECT_EQ(onC.view, second.Id()) << "an instance pin names the view that shows it";
    EXPECT_EQ(onC.pin.kind, Pin::Kind::Instance);

    EXPECT_EQ(PinError(deck.Id(), Pin::Follow()), "");
    EXPECT_EQ(_state.TargetFor(deck.Id())->instance, "A") << "unpinned: the followed instance";

    EXPECT_EQ(PinError("s99", Pin::Follow()), "Not Found");
    EXPECT_EQ(PinError(deck.Id(), Pin::OnView("v99")), "Not Found");
    EXPECT_EQ(PinError(deck.Id(), Pin::OnInstance("Z")), "Not Found");
    const std::vector<SurfaceSessionInfo> sessions = _state.Sessions();
    ASSERT_EQ(sessions.size(), 2u);
    EXPECT_EQ(sessions[0].spec.serial, "A1B2C3");
    EXPECT_EQ(sessions[1].pin, Pin::OnInstance("C"));
}

TEST_F(TargetState_Test, AViewPinIsNotNowOnAHeadlessHost)
{
    SessionHandle deck = _state.OpenSession({"deck", "companion", "", ""});
    EXPECT_EQ(PinError(deck.Id(), Pin::OnView("v1")), "not_now");
    EXPECT_EQ(PinError(deck.Id(), Pin::OnInstance("B")), "") << "an instance pin works headless";
    EXPECT_EQ(_state.TargetFor(deck.Id())->instance, "B");
    EXPECT_EQ(_state.TargetFor(deck.Id())->view, "");
}

TEST_F(TargetState_Test, ClosedViewsAndGoneInstancesFallBackToFollowing)
{
    ViewHandle main = _state.RegisterView("Main window");
    _state.SetActive(main, "A");
    SessionHandle deck = _state.OpenSession({"deck", "companion", "", ""});
    SessionHandle pad = _state.OpenSession({"pad", "host_device", "", ""});
    {
        ViewHandle second = _state.RegisterView("Window 2");
        _state.SetActive(second, "B");
        _state.Activated(second);
        EXPECT_EQ(PinError(deck.Id(), Pin::OnView(second.Id())), "");
        EXPECT_EQ(_state.Followed(), "B");
    }  // the window closes
    EXPECT_EQ(_state.Followed(), "A") << "followers move to the next view's active instance";
    std::optional<SurfaceSessionInfo> info = _state.Session(deck.Id());
    ASSERT_TRUE(info);
    EXPECT_EQ(info->pin, Pin::Follow());
    EXPECT_NE(info->notice.find("closed"), std::string::npos) << info->notice;
    EXPECT_EQ(info->target.instance, "A");

    EXPECT_EQ(PinError(pad.Id(), Pin::OnInstance("C")), "");
    _state.InstanceGone("C");
    info = _state.Session(pad.Id());
    EXPECT_EQ(info->pin, Pin::Follow());
    EXPECT_NE(info->notice.find("gone"), std::string::npos);

    _state.InstanceGone("A");
    EXPECT_EQ(_state.Views()[0].activeInstance, "") << "the view lost the instance it showed";
    EXPECT_EQ(PinError(deck.Id(), Pin::Follow()), "");
    EXPECT_EQ(_state.Session(deck.Id())->notice, "") << "the next pin clears the notice";
}

TEST_F(TargetState_Test, AReplacedMachineKeepsItsViewAndItsPins)
{
    ViewHandle main = _state.RegisterView("Main window");
    _state.SetActive(main, "A");
    SessionHandle deck = _state.OpenSession({"deck", "companion", "", ""});
    EXPECT_EQ(PinError(deck.Id(), Pin::OnInstance("A")), "");
    _host.instances.insert("A2");
    _state.InstanceReplaced("A", "A2");
    EXPECT_EQ(_state.Followed(), "A2");
    EXPECT_EQ(_state.Session(deck.Id())->pin, Pin::OnInstance("A2"));
    _state.InstanceGone("A");
    EXPECT_EQ(_state.Session(deck.Id())->pin, Pin::OnInstance("A2")) << "the old id's removal changes nothing";
}

namespace
{
struct TargetNotes
{
    std::mutex mutex;
    std::vector<std::string> seen;  ///< "reason followed sessions"
    std::atomic<int> count{0};
    uint64_t observer = 0;
    uint64_t first = 0;  ///< the revision of the first post this state made (other states may post too)

    TargetNotes()
    {
        observer = MessageCenter::DefaultMessageCenter().AddObserver(
            NC_SURFACE_TARGET, ObserverCallbackFunc([this](int, Message* message) {
                auto* payload = message != nullptr ? dynamic_cast<SurfaceTargetPayload*>(message->obj) : nullptr;
                if (payload == nullptr)
                    return;
                std::string sessions;
                for (const std::string& id : payload->sessions)
                    sessions += id + ",";
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    seen.push_back(payload->reason + " " + payload->followed + " " + sessions);
                }
                count++;
            }));
    }
    ~TargetNotes() { MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_SURFACE_TARGET, observer); }
    bool Saw(const std::string& line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return std::find(seen.begin(), seen.end(), line) != seen.end();
    }
};
}  // namespace

TEST(TargetStateNotification_Test, ChangesPostNcSurfaceTargetAndNothingElseDoes)
{
    FakeHost host;
    TargetState state(host.Fallback(), true, host.Exists());
    TargetNotes notes;
    ViewHandle main = state.RegisterView("Main window");
    const uint64_t afterRegister = state.Revision();
    state.Activated(main);  // already in front: nothing moves
    EXPECT_EQ(state.Revision(), afterRegister);
    state.SetActive(main, "A");
    EXPECT_EQ(state.Revision(), afterRegister + 1);
    SessionHandle deck = state.OpenSession({"deck", "companion", "", ""});
    std::string error;
    std::string message;
    ASSERT_TRUE(state.SetPin(deck.Id(), Pin::OnInstance("B"), error, message));
    ASSERT_TRUE(TestWait::For([&] { return notes.Saw("pin A " + deck.Id() + ","); }));
    EXPECT_TRUE(notes.Saw("active A "));
    state.Refresh();  // rule 4 did not change anything that matters with a view
    EXPECT_EQ(state.Revision(), afterRegister + 3);
}
