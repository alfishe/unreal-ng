// MachinePreset (core/src/emulator/preset/machinepreset.h): the preset machine verb behind every surface (RE API efficiency V3).
// One call creates the machine with its cards, sets the features, mutes, starts and pauses it and takes run control;
// a refusal leaves no instance behind. The surfaces pass their arguments through as a StateNode object.
//
// A created machine is started and paused (its emulation thread): ~30 ms per machine by nature, so the checks of one
// machine share a test

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "common/uuid.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/preset/machinepreset.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/audiomixer.h"

namespace
{

const StateNode& Field(const StateNode& node, const std::string& key)
{
    static const StateNode missing;
    const StateNode* found = node.find(key);
    return found != nullptr ? *found : missing;
}

class MachinePreset_Test : public ::testing::Test
{
protected:
    std::vector<std::string> _before;

    void SetUp() override
    {
        _before = EmulatorManager::GetInstance()->GetEmulatorIds();
    }

    void TearDown() override
    {
        // Every instance the test made, whatever it kept track of
        for (const std::string& id : EmulatorManager::GetInstance()->GetEmulatorIds())
        {
            if (std::find(_before.begin(), _before.end(), id) == _before.end())
                EmulatorManager::GetInstance()->RemoveEmulator(id);
        }
        MessageCenter::DisposeDefaultMessageCenter();
    }

    size_t Instances() const
    {
        return EmulatorManager::GetInstance()->GetEmulatorIds().size();
    }

    static StateNode Args(const std::string& model)
    {
        StateNode args = StateNode::Object();
        args["model"] = model;
        return args;
    }

    /// A surface with sessions: takes the machine's run control with a fresh owner, as the WebAPI does
    static MachinePresetCaller SessionCaller(unreal::UUID& owner)
    {
        MachinePresetCaller caller;
        caller.surface = "webapi";
        caller.takeControl = [&owner](Emulator& emulator, const std::string& client, uint32_t leaseSeconds,
                                      std::string& session, std::string& reason) {
            owner = unreal::UUID::Generate();
            if (!emulator.GetContext()->TakeRunControl(owner, "webapi", &reason, client,
                                                       std::chrono::milliseconds(uint64_t(leaseSeconds) * 1000)))
                return false;
            session = owner.toString();
            return true;
        };
        caller.holdsControl = [&owner](Emulator& emulator) { return emulator.GetContext()->HasRunControl(owner); };
        return caller;
    }
};

/// The defaults are the reproducible preset choice: RAM zero, muted, started and paused at power-on, the tape
/// accelerators off; a surface without sessions gets no control session
TEST_F(MachinePreset_Test, DefaultsAreTheReproducibleMachine)
{
    const size_t before = Instances();
    const std::string selectedBefore = EmulatorManager::GetInstance()->GetSelectedEmulatorId();
    const MachinePresetReply reply = MachinePreset::Execute(Args("48K"), MachinePresetCaller{});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    ASSERT_NE(reply.emulator, nullptr);
    EXPECT_EQ(reply.httpStatus, 201);
    EXPECT_EQ(Instances(), before + 1);

    Emulator& emulator = *reply.emulator;
    EmulatorContext* context = emulator.GetContext();
    EXPECT_EQ(Field(reply.body, "id").s, emulator.GetId());
    EXPECT_EQ(Field(reply.body, "model").s, "48K");
    EXPECT_EQ(Field(reply.body, "ram").s, "zero");
    EXPECT_EQ(context->config.ramPowerOn, RamPowerOn::Zero);

    EXPECT_TRUE(Field(reply.body, "paused").b);
    EXPECT_TRUE(emulator.IsRunning()) << "started: runs and steps are accepted";
    EXPECT_TRUE(emulator.IsPaused());
    const StateNode& position = Field(reply.body, "position");
    EXPECT_EQ(position.find("frame")->i, static_cast<int64_t>(context->emulatorState.frame_counter));
    EXPECT_EQ(position.find("frame")->i, 0) << "paused at power-on";
    EXPECT_EQ(position.find("t")->i, 3) << "the reset's three T-states: no instruction has run";
    EXPECT_EQ(position.find("pc")->i, 0);
    EXPECT_EQ(emulator.GetZ80State()->pc, 0);

    EXPECT_TRUE(Field(reply.body, "muted").b);
    EXPECT_EQ(AudioMixer::Flag(context, "master", "muted"), std::optional<bool>(true));

    EXPECT_FALSE(Field(reply.body, "fast_tape").b);
    EXPECT_FALSE(emulator.GetFeatureManager()->isEnabled(Features::kFastTape));
    EXPECT_FALSE(emulator.GetFeatureManager()->isEnabled(Features::kTurboTape));
    const StateNode& notDefault = Field(Field(reply.body, "features"), "not_default");
    ASSERT_NE(notDefault.find(Features::kFastTape), nullptr) << "on by default, off on a preset machine: listed";
    EXPECT_FALSE(notDefault.find(Features::kFastTape)->b);

    EXPECT_FALSE(Field(Field(reply.body, "control"), "held").b);
    EXPECT_FALSE(context->IsRunControlClaimed());
    EXPECT_EQ(EmulatorManager::GetInstance()->GetSelectedEmulatorId(), selectedBefore)
        << "the selection is left alone: which machine is active is the caller's choice";
    EXPECT_FALSE(Field(reply.body, "selected").b);

    EXPECT_NE(reply.text.find("Preset machine " + emulator.GetId() + " ready: 48K"), std::string::npos) << reply.text;
    EXPECT_NE(reply.text.find("RAM zero, paused at frame"), std::string::npos) << reply.text;
    EXPECT_NE(reply.text.find("master muted"), std::string::npos) << reply.text;
    EXPECT_NE(reply.text.find("Run control: not taken"), std::string::npos) << reply.text;
    EXPECT_EQ(Field(reply.ToValue(), "text").s, reply.text);
}

/// A card, a feature and the control session in one call: the card is in its slot of the machine the answer names
/// (no restart, the same instance), the machine is ready and paused, the feature is off in its FeatureManager, the
/// caller holds run control
TEST_F(MachinePreset_Test, CardFeatureAndControlInOneCall)
{
    StateNode args = Args("48K");
    args["cards"].push("edge.1=kempston");
    args["features"]["kmouse"] = false;   // an alias: answered under the feature's id
    args["ram"] = "random";
    args["paused"] = true;
    args["fast_tape"] = true;
    args["client"] = "preset-test";
    unreal::UUID owner;
    const MachinePresetReply reply = MachinePreset::Execute(args, SessionCaller(owner));
    ASSERT_TRUE(reply.Ok()) << reply.message;
    Emulator& emulator = *reply.emulator;
    EmulatorContext* context = emulator.GetContext();

    // The card: in the slot set the machine was built with
    const SlotManager::Slot* slot = context->pSlotManager->Current().FindSlot("edge.1");
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->entry.card, "kempston");
    EXPECT_FALSE(slot->entry.disabled);
    const StateNode& cards = Field(reply.body, "cards");
    bool listed = false;
    for (const StateNode& card : cards.items)
    {
        if (Field(card, "slot").s == "edge.1" && Field(card, "card").s == "kempston")
            listed = Field(card, "asked").b;
    }
    EXPECT_TRUE(listed) << "edge.1 = kempston, asked";
    EXPECT_TRUE(emulator.IsRunning() && emulator.IsPaused()) << "ready: no restart to wait for";
    EXPECT_TRUE(Field(reply.body, "paused").b);

    // The feature
    EXPECT_FALSE(emulator.GetFeatureManager()->isEnabled(Features::kKempstonMouse));
    const StateNode& features = Field(reply.body, "features");
    ASSERT_NE(Field(features, "asked").find(Features::kKempstonMouse), nullptr);
    EXPECT_FALSE(Field(features, "asked").find(Features::kKempstonMouse)->b);
    EXPECT_NE(Field(features, "not_default").find(Features::kKempstonMouse), nullptr);

    // The other arguments
    EXPECT_EQ(Field(reply.body, "ram").s, "random");
    EXPECT_TRUE(Field(reply.body, "fast_tape").b);
    EXPECT_TRUE(emulator.GetFeatureManager()->isEnabled(Features::kFastTape));
    EXPECT_EQ(Field(features, "not_default").find(Features::kFastTape), nullptr) << "at its default: not listed";

    // Control
    const StateNode& control = Field(reply.body, "control");
    EXPECT_TRUE(Field(control, "held").b);
    EXPECT_EQ(Field(control, "session").s, owner.toString());
    EXPECT_EQ(Field(control, "client").s, "preset-test");
    EXPECT_EQ(Field(control, "lease_seconds").i, 60);
    EXPECT_TRUE(context->HasRunControl(owner));

    EXPECT_NE(reply.text.find("edge.1 = kempston"), std::string::npos) << reply.text;
    EXPECT_NE(reply.text.find("kempstonmouse off (asked)"), std::string::npos) << reply.text;
    EXPECT_NE(reply.text.find("Run control: held by you (webapi, session " + owner.toString()), std::string::npos)
        << reply.text;
}

/// A running machine when asked, not muted when asked, selected when asked; a card as an object with the planner's slot
TEST_F(MachinePreset_Test, RunningUnmutedAndACardObject)
{
    StateNode args = Args("48K");
    StateNode card = StateNode::Object();
    card["slot"] = "auto";
    card["card"] = "kempston";
    args["cards"].push(std::move(card));
    args["paused"] = false;
    args["muted"] = false;
    args["select"] = true;
    const MachinePresetReply reply = MachinePreset::Execute(args, MachinePresetCaller{});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(EmulatorManager::GetInstance()->GetSelectedEmulatorId(), reply.emulator->GetId()) << "select: asked";
    EXPECT_TRUE(Field(reply.body, "selected").b);
    EXPECT_TRUE(reply.emulator->IsRunning());
    EXPECT_FALSE(reply.emulator->IsPaused());
    EXPECT_FALSE(Field(reply.body, "paused").b);
    EXPECT_FALSE(Field(reply.body, "muted").b);
    EXPECT_NE(reply.text.find(", running."), std::string::npos) << reply.text;
    EXPECT_NE(reply.text.find("= kempston"), std::string::npos) << reply.text;
}

/// Each refusal names the valid values and leaves no instance behind
TEST_F(MachinePreset_Test, RefusalsLeaveNoInstance)
{
    const size_t before = Instances();
    auto refused = [&](StateNode args, int status, const std::string& says) {
        const MachinePresetReply reply = MachinePreset::Execute(args, MachinePresetCaller{});
        EXPECT_FALSE(reply.Ok());
        EXPECT_EQ(reply.httpStatus, status) << reply.message;
        EXPECT_NE(reply.message.find(says), std::string::npos) << reply.message;
        EXPECT_EQ(reply.emulator, nullptr);
        EXPECT_EQ(Instances(), before) << "an instance was left behind: " << reply.message;
        return reply;
    };

    refused(StateNode::Object(), 400, "model: required");
    const MachinePresetReply model = refused(Args("48Q"), 400, "unknown model '48Q'; the models:");
    EXPECT_NE(model.message.find("PENTAGON"), std::string::npos) << model.message;
    EXPECT_GT(Field(model.body, "valid").size(), 3u);
    refused(Args("ZXPOLY-48K"), 400, "ZX-Poly");

    StateNode ram = Args("48K");
    ram["ram"] = "ones";
    refused(ram, 400, "ram: expected zero or random");

    StateNode feature = Args("48K");
    feature["features"]["kempstonmice"] = false;
    const MachinePresetReply unknownFeature = refused(feature, 400, "unknown feature 'kempstonmice'; the features:");
    EXPECT_NE(unknownFeature.message.find("kempstonmouse"), std::string::npos);

    StateNode card = Args("48K");
    card["cards"].push("edge.1=kempstone");
    const MachinePresetReply unknownCard = refused(card, 400, "unknown card 'kempstone'; the cards:");
    EXPECT_NE(unknownCard.message.find("kempston,"), std::string::npos) << unknownCard.message;

    StateNode malformed = Args("48K");
    malformed["cards"].push("kempston");
    refused(malformed, 400, "expected <slot>=<card>");

    StateNode flag = Args("48K");
    flag["muted"] = "maybe";
    refused(flag, 400, "muted: expected true or false");

    StateNode unknown = Args("48K");
    unknown["mute"] = true;
    refused(unknown, 400, "unknown argument 'mute'; the arguments: model, ram, cards");

    // The slot plan needs the model's configuration: made while the machine is created, a refusal removes it
    StateNode slot = Args("48K");
    slot["cards"].push("nobus.1=kempston");
    const MachinePresetReply badSlot = refused(slot, 409, "The buses of 48K: ");
    EXPECT_NE(badSlot.message.find("edge"), std::string::npos) << badSlot.message;
    EXPECT_NE(badSlot.message.find("Nothing was created"), std::string::npos) << badSlot.message;

    // A session that cannot be taken: the built machine is removed
    MachinePresetCaller noSession;
    noSession.takeControl = [](Emulator&, const std::string&, uint32_t, std::string&, std::string& reason) {
        reason = "held by another";
        return false;
    };
    const MachinePresetReply control = MachinePreset::Execute(Args("48K"), noSession);
    EXPECT_EQ(control.httpStatus, 409);
    EXPECT_NE(control.message.find("take_control: held by another"), std::string::npos) << control.message;
    EXPECT_EQ(Instances(), before);
}

/// replace destroys the other instances once the new one is built; a machine another client's explicit session
/// holds refuses the call with nothing created or destroyed; without replace the machines stay side by side
TEST_F(MachinePreset_Test, ReplaceDestroysTheOthersUnlessHeld)
{
    unreal::UUID mine;
    StateNode first = Args("48K");
    first["paused"] = false;
    const MachinePresetReply a = MachinePreset::Execute(first, SessionCaller(mine));
    ASSERT_TRUE(a.Ok()) << a.message;
    const std::string aId = a.emulator->GetId();
    const size_t withA = Instances();

    // Another client (no session of its own on A) asks for a replace: refused
    unreal::UUID other;
    StateNode replace = Args("48K");
    replace["replace"] = true;
    replace["take_control"] = false;
    const MachinePresetReply refused = MachinePreset::Execute(replace, SessionCaller(other));
    EXPECT_EQ(refused.httpStatus, 409);
    EXPECT_NE(refused.message.find("replace: instance " + aId + " is held"), std::string::npos) << refused.message;
    EXPECT_EQ(Instances(), withA);
    EXPECT_TRUE(EmulatorManager::GetInstance()->HasEmulator(aId));

    // The holder itself: A goes, the new machine is the only one of this test
    const MachinePresetReply b = MachinePreset::Execute(replace, SessionCaller(mine));
    ASSERT_TRUE(b.Ok()) << b.message;
    EXPECT_FALSE(EmulatorManager::GetInstance()->HasEmulator(aId));
    EXPECT_EQ(Instances(), 1u) << "every other instance went";
    bool named = false;
    for (const StateNode& id : Field(b.body, "replaced").items)
        named = named || id.s == aId;
    EXPECT_TRUE(named);
    EXPECT_NE(b.text.find("Replaced"), std::string::npos) << b.text;
    EXPECT_FALSE(Field(Field(b.body, "control"), "held").b) << "take_control: false";
}

/// The CLI's words become the same arguments object
TEST_F(MachinePreset_Test, ArgumentsFromWords)
{
    StateNode args;
    std::string error;
    ASSERT_TRUE(MachinePreset::ArgumentsFromWords({"48K", "ram=random", "card=edge.1=kempston", "feature=kempstonmouse=off",
                                                "features=fasttape=on,turbotape=on", "muted=off", "lease_seconds=30"},
                                               args, error))
        << error;
    EXPECT_EQ(Field(args, "model").s, "48K");
    EXPECT_EQ(Field(args, "ram").s, "random");
    ASSERT_EQ(Field(args, "cards").size(), 1u);
    EXPECT_EQ(Field(args, "cards").items[0].s, "edge.1=kempston");
    EXPECT_EQ(Field(Field(args, "features"), "kempstonmouse").s, "off");
    EXPECT_EQ(Field(Field(args, "features"), "turbotape").s, "on");
    EXPECT_EQ(Field(args, "muted").s, "off");
    EXPECT_EQ(Field(args, "lease_seconds").i, 30);

    EXPECT_FALSE(MachinePreset::ArgumentsFromWords({"48K", "128K"}, args, error));
    EXPECT_NE(error.find("expected name=value"), std::string::npos) << error;
    EXPECT_FALSE(MachinePreset::ArgumentsFromWords({"48K", "feature=kempstonmouse"}, args, error));
}

}  // namespace
