// The question before an emulator with unsaved media closes (D-8): only ask and lost need the user

#include <gtest/gtest.h>

#include "media/core/closeprompt.h"

namespace
{
    SlotInfo Unsaved(const char* slot, const char* onRelease)
    {
        SlotInfo info;
        info.descriptor.id = slot;
        info.source = std::string("/zx/") + slot;
        info.dirty = true;
        info.changedUnits = 3;
        info.changes = "3 sectors";
        info.onRelease = onRelease;
        return info;
    }
}  // namespace

TEST(ClosePrompt_Test, PolicyDecidesUnlessItAsks)
{
    const ClosePrompt quiet = ClosePromptFor({Unsaved("sd.zc", "delta"), Unsaved("ide0.master", "journal")});
    EXPECT_FALSE(quiet.ask) << "the policy saves them: nothing to ask";
    ASSERT_EQ(quiet.media.size(), 2u);
    EXPECT_EQ(quiet.media[0].fate, "saved as a session delta (writes.save)");

    const ClosePrompt asks = ClosePromptFor({Unsaved("sd.zc", "delta"), Unsaved("fdd.a", "lost"), Unsaved("ide0.master", "ask")});
    EXPECT_TRUE(asks.ask);
    ASSERT_EQ(asks.media.size(), 3u);
    EXPECT_EQ(asks.media[0].slot, "fdd.a") << "the ones that ask come first, in order";
    EXPECT_EQ(asks.media[1].slot, "ide0.master");
    EXPECT_FALSE(asks.media[2].asks);

    SlotInfo clean = Unsaved("fdd.b", "");
    clean.dirty = false;
    EXPECT_TRUE(ClosePromptFor({clean}).media.empty());
    EXPECT_TRUE(ClosePromptFor({Unsaved("fdd.b", "")}).ask) << "no policy: the changes would be lost";
}
