/// @file markupcontrol_test.cpp
/// @brief The `markup` verbs every plane shares (markupcontrol.h; docs/inprogress/2026-11-05-software-metadata-profile
/// §5): show, layers, mark, unmark, comment, clear and their refusals; save through a registered profile saver (the
/// user layer emptied after, kept on keep or when the profile does not lie over the machine); the `debug.markup`
/// readout; the CLI's words (cli-markup.h) giving the same action and options as the REST routes.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "../../../automation/cli/src/commands/cli-markup.h"
#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/markup/markupcontrol.h"
#include "debugger/markup/memorymarkup.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/monitoring/monitoringapi.h"
#include "emulator/sound/soundmanager.h"

using analysis::RegionKind;
using markup::Layer;
using markup::Place;
using markup::Space;

namespace
{
const StateNode& Member(const StateNode& node, const std::string& name)
{
    static const StateNode kNull;
    const StateNode* found = node.find(name);
    return found ? *found : kNull;
}
}  // namespace

class MarkupControl_Test : public ::testing::Test
{
protected:
    static inline Emulator* _shared = nullptr;
    Emulator* _emulator = nullptr;
    markup::MemoryMarkup* _markup = nullptr;

    static void SetUpTestSuite()
    {
        _shared = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        if (!_shared)
            return;
        _shared->GetFeatureManager()->setFeature(Features::kSoundGeneration, false);
        if (_shared->GetContext()->pSoundManager)
            _shared->GetContext()->pSoundManager->mute();
    }
    static void TearDownTestSuite()
    {
        if (_shared)
            EmulatorTestHelper::CleanupEmulator(_shared);
        _shared = nullptr;
    }

    void SetUp() override
    {
        _emulator = _shared;
        ASSERT_NE(_emulator, nullptr);
        _markup = _emulator->GetContext()->pDebugManager->GetMemoryMarkup();
        ASSERT_NE(_markup, nullptr);
        _emulator->GetContext()->pMemory->SetRAMPageToBank3(0);
    }

    void TearDown() override
    {
        markup::SetProfileSaver({});
        if (!_markup)
            return;
        for (int i = 0; i < markup::kLayerCount; i++)
            _markup->ClearLayer(static_cast<Layer>(i));
    }

    markup::Reply Run(const std::string& action, const markup::Options& options = {})
    {
        return markup::Execute(*_emulator, action, options);
    }
};

TEST_F(MarkupControl_Test, MarkShowUnmarkAndComment)
{
    markup::Reply reply = Run("mark", {{"range", "8000-80FF"}, {"kind", "sprite"}, {"title", "hero"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_NE(reply.text.find("Marked $8000-$80FF as sprite 'hero'"), std::string::npos) << reply.text;
    ASSERT_TRUE(Run("comment", {{"address", "$8010"}, {"text", "frame 2"}}).Ok());

    reply = Run("show", {{"from", "$8000"}, {"to", "$81FF"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const StateNode& spans = Member(reply.body, "spans");
    ASSERT_EQ(spans.items.size(), 1u);
    EXPECT_EQ(Member(spans.items[0], "from").s, "$8000");
    EXPECT_EQ(Member(spans.items[0], "to").s, "$80FF");
    EXPECT_EQ(Member(spans.items[0], "kind").s, "sprite");
    EXPECT_EQ(Member(spans.items[0], "layer").s, "user");
    EXPECT_EQ(Member(spans.items[0], "title").s, "hero");
    ASSERT_EQ(Member(reply.body, "comments").items.size(), 1u);
    EXPECT_NE(reply.text.find("$8000-$80FF sprite"), std::string::npos) << reply.text;
    EXPECT_NE(reply.text.find("; $8010 frame 2"), std::string::npos) << "the comment in the text an agent reads";

    // layer= keeps only the spans that layer wins
    EXPECT_TRUE(Member(Run("show", {{"range", "8000-80FF"}, {"layer", "analysis"}}).body, "spans").items.empty());

    ASSERT_TRUE(Run("unmark", {{"range", "8040-80FF"}}).Ok());
    reply = Run("show", {{"range", "8000-80FF"}});
    ASSERT_EQ(Member(reply.body, "spans").items.size(), 1u);
    EXPECT_EQ(Member(Member(reply.body, "spans").items[0], "to").s, "$803F");

    ASSERT_TRUE(Run("comment", {{"address", "$8010"}}).Ok()) << "no text removes it";
    EXPECT_TRUE(_markup->Comments(0x8010, 0x8010).empty());
}

TEST_F(MarkupControl_Test, LayersCountAndClear)
{
    ASSERT_TRUE(Run("mark", {{"from", "0x9000"}, {"kind", "data"}}).Ok());
    markup::Reply reply = Run("layers");
    ASSERT_TRUE(reply.Ok());
    const StateNode& layers = Member(reply.body, "layers");
    ASSERT_EQ(layers.items.size(), static_cast<size_t>(markup::kLayerCount));
    EXPECT_EQ(Member(layers.items.back(), "layer").s, "user");
    EXPECT_EQ(Member(layers.items.back(), "claims").i, 1);
    EXPECT_EQ(Member(reply.body, "runtime").s, "none");
    EXPECT_NE(reply.text.find("user: "), std::string::npos) << reply.text;

    // The readout every plane pushes (WebSocket topic debug.markup) is the same count
    StateNode args = StateNode::Object();
    args["instance"] = _emulator->GetId();
    const monitoring::Reply readout = monitoring::Call("debug.markup", args);
    ASSERT_TRUE(readout.Ok());
    EXPECT_TRUE(Member(readout.body, "available").b);
    EXPECT_EQ(Member(Member(readout.body, "layers").items.back(), "claims").i, 1);

    ASSERT_TRUE(Run("clear").Ok()) << "the user layer by default";
    EXPECT_TRUE(_markup->GetLayer(Layer::User).claims.empty());
}

TEST_F(MarkupControl_Test, RefusalsSayWhatToGive)
{
    EXPECT_EQ(Run("nope").error, markup::Error::BadRequest);
    markup::Reply reply = Run("show", {{"colour", "red"}});
    EXPECT_EQ(reply.error, markup::Error::BadRequest);
    EXPECT_NE(reply.message.find("range"), std::string::npos) << "names the options it takes: " << reply.message;
    EXPECT_EQ(Run("mark", {{"range", "8000-80FF"}}).error, markup::Error::BadRequest) << "no kind";
    EXPECT_EQ(Run("mark", {{"range", "8000-80FF"}, {"kind", "graphics"}}).error, markup::Error::BadRequest);
    EXPECT_EQ(Run("mark", {{"kind", "code"}}).error, markup::Error::BadRequest) << "no addresses";
    EXPECT_EQ(Run("mark", {{"from", "$9000"}, {"to", "$8000"}, {"kind", "code"}}).error, markup::Error::BadRequest);
    EXPECT_EQ(Run("mark", {{"from", "no_such_label"}, {"kind", "code"}}).error, markup::Error::NotFound);
    EXPECT_EQ(Run("show", {{"limit", "0"}}).error, markup::Error::BadRequest);
    EXPECT_EQ(Run("clear", {{"layer", "top"}}).error, markup::Error::BadRequest);
    EXPECT_EQ(Run("comment", {{"text", "x"}}).error, markup::Error::BadRequest);
    EXPECT_EQ(Run("save").error, markup::Error::NotAvailable) << "no profile owner in this process";
}

TEST_F(MarkupControl_Test, SaveHandsTheLayersToTheProfileOwner)
{
    markup::SaveRequest seen;
    bool applied = true;
    markup::SetProfileSaver([&seen, &applied](Emulator&, const markup::SaveRequest& request) {
        seen = request;
        markup::Reply reply;
        reply.body["profile"] = request.profile.empty() ? "user/game-test" : request.profile;
        reply.body["applied"] = applied;
        reply.text = "Saved";
        return reply;
    });
    EXPECT_EQ(Run("save").error, markup::Error::Conflict) << "nothing to save";

    markup::LayerContent analysisLayer;
    analysisLayer.source = "analysis";
    markup::Claim sure;
    sure.place = Place{Space::Cpu, 0, 0xA000, 0xA0FF};
    sure.kind = RegionKind::Sprite;
    sure.confidence = 900;
    markup::Claim weak = sure;
    weak.place = Place{Space::Cpu, 0, 0xB000, 0xB0FF};
    weak.confidence = 400;
    markup::Claim code = sure;
    code.place = Place{Space::Cpu, 0, 0xC000, 0xC0FF};
    code.kind = RegionKind::Code;
    analysisLayer.claims = {sure, weak, code};
    _markup->SetLayer(Layer::Analysis, analysisLayer);
    ASSERT_TRUE(Run("mark", {{"range", "8000-80FF"}, {"kind", "data"}}).Ok());
    ASSERT_TRUE(Run("comment", {{"address", "$8000"}, {"text", "lives"}}).Ok());

    markup::Reply reply = Run("save", {{"analysis", "sprite"}, {"profile", "user/game-x"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(seen.profile, "user/game-x");
    ASSERT_EQ(seen.user.claims.size(), 1u);
    EXPECT_EQ(seen.user.comments.size(), 1u);
    ASSERT_EQ(seen.analysis.size(), 1u) << "the sprite of 900; 400 is below the 700 default, code not chosen";
    EXPECT_EQ(seen.analysis[0].place.lo, 0xA000);
    EXPECT_EQ(Member(reply.body, "saved_analysis_claims").i, 1);
    EXPECT_TRUE(_markup->GetLayer(Layer::User).claims.empty()) << "the marks live in the profile now";

    ASSERT_TRUE(Run("mark", {{"range", "8000-80FF"}, {"kind", "data"}}).Ok());
    ASSERT_TRUE(Run("save", {{"analysis", "all"}, {"confidence", "300"}, {"keep", "true"}}).Ok());
    EXPECT_EQ(seen.analysis.size(), 3u);
    EXPECT_EQ(_markup->GetLayer(Layer::User).claims.size(), 1u) << "keep";

    applied = false;
    reply = Run("save");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(_markup->GetLayer(Layer::User).claims.size(), 1u)
        << "a profile that does not lie over the machine would hide the marks: they stay";
    EXPECT_TRUE(Member(reply.body, "kept_user_layer").b);

    EXPECT_EQ(Run("save", {{"analysis", "graphics"}}).error, markup::Error::BadRequest);
    EXPECT_EQ(Run("save", {{"analysis", "all"}, {"confidence", "2000"}}).error, markup::Error::BadRequest);
}

TEST(MarkupControlCli_Test, WordsGiveTheRestOptions)
{
    std::string action;
    std::string error;
    std::map<std::string, std::string> options;
    bool json = false;

    ASSERT_TRUE(CliMarkup::ParseArgs({}, action, options, json, error));
    EXPECT_EQ(action, "show");
    EXPECT_TRUE(options.empty());
    ASSERT_TRUE(CliMarkup::ParseArgs({"8000-80FF", "--json"}, action, options, json, error));
    EXPECT_EQ(action, "show");
    EXPECT_EQ(options, (std::map<std::string, std::string>{{"range", "8000-80FF"}}));
    EXPECT_TRUE(json);
    ASSERT_TRUE(CliMarkup::ParseArgs({"show", "$8000", "$80FF", "layer=user"}, action, options, json, error));
    EXPECT_EQ(options, (std::map<std::string, std::string>{{"from", "$8000"}, {"to", "$80FF"}, {"layer", "user"}}));

    ASSERT_TRUE(CliMarkup::ParseArgs({"mark", "8000", "80FF", "sprite", "the", "hero", "cpu"}, action, options, json,
                                     error));
    EXPECT_EQ(action, "mark");
    EXPECT_EQ(options, (std::map<std::string, std::string>{
                           {"from", "8000"}, {"to", "80FF"}, {"kind", "sprite"}, {"title", "the hero"}, {"cpu", "true"}}));
    ASSERT_TRUE(CliMarkup::ParseArgs({"mark", "$8000-$80FF", "code"}, action, options, json, error));
    EXPECT_EQ(options, (std::map<std::string, std::string>{{"range", "$8000-$80FF"}, {"kind", "code"}}));

    ASSERT_TRUE(CliMarkup::ParseArgs({"comment", "$8000", "lives", "left"}, action, options, json, error));
    EXPECT_EQ(options, (std::map<std::string, std::string>{{"address", "$8000"}, {"text", "lives left"}}));
    ASSERT_TRUE(CliMarkup::ParseArgs({"clear", "analysis"}, action, options, json, error));
    EXPECT_EQ(options, (std::map<std::string, std::string>{{"layer", "analysis"}}));
    ASSERT_TRUE(CliMarkup::ParseArgs({"save", "user/game-x", "analysis=code,sprite", "keep"}, action, options, json,
                                     error));
    EXPECT_EQ(options, (std::map<std::string, std::string>{
                           {"profile", "user/game-x"}, {"analysis", "code,sprite"}, {"keep", "true"}}));

    EXPECT_FALSE(CliMarkup::ParseArgs({"layers", "extra"}, action, options, json, error));
    EXPECT_NE(error.find("extra"), std::string::npos);
}
