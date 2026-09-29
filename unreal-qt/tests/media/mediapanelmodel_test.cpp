// The media panel's Qt-free model: rows, file filters, the disposition question

#include <gtest/gtest.h>

#include "media/core/mediapanelmodel.h"

namespace
{
    StateNode Slot(const char* id, const char* alias, const char* kind, const char* state)
    {
        StateNode slot = StateNode::Object();
        slot["id"] = id;
        slot["kind"] = kind;
        slot["label"] = std::string("label of ") + id;
        StateNode aliases = StateNode::Array();
        if (*alias)
            aliases.push(alias);
        slot["aliases"] = aliases;
        slot["acceptsFolder"] = true;
        slot["writeProtect"] = false;
        slot["detached"] = std::string(state) == "detached";
        slot["state"] = state;
        slot["medium"] = StateNode();
        return slot;
    }

    StateNode Medium(const char* source, bool dirty, int64_t units, const char* changes)
    {
        StateNode medium = StateNode::Object();
        medium["source"] = source;
        medium["format"] = "trd";
        medium["access"] = "session";
        medium["dirty"] = dirty;
        medium["dirtyUnits"] = units;
        medium["changes"] = changes;
        return medium;
    }
}  // namespace

TEST(MediaPanelModel_Test, RowsFromAListReply)
{
    StateNode a = Slot("fdd.a", "A", "floppy", "present");
    a["medium"] = Medium("games/elite.trd", true, 3, "3 tracks: 20 sectors total");
    StateNode sd = Slot("sd.zc", "sd", "block", "empty");
    StateNode gone = Slot("sd.ngs", "", "block", "detached");
    gone["medium"] = Medium("cards/ngs.img", true, 12, "12 sectors");

    StateNode reply = StateNode::Object();
    reply["ok"] = true;
    reply["slots"] = StateNode::Array();
    reply["slots"].push(a);
    reply["slots"].push(sd);
    reply["detached"] = StateNode::Array();
    reply["detached"].push(gone);

    const auto rows = MediaPanelRows(reply);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].slot, "fdd.a");
    EXPECT_EQ(rows[0].alias, "A");
    EXPECT_TRUE(rows[0].present);
    EXPECT_EQ(rows[0].medium, "games/elite.trd");
    EXPECT_EQ(rows[0].dirty, "3 tracks: 20 sectors total") << "the core's words";
    EXPECT_FALSE(rows[1].present);
    EXPECT_EQ(rows[1].dirty, "");
    EXPECT_TRUE(rows[2].detached);
    EXPECT_FALSE(rows[2].present) << "a detached medium is in no slot";
    EXPECT_EQ(rows[2].dirty, "12 sectors");
}

TEST(MediaPanelModel_Test, FileFilterAndDispositionQuestion)
{
    StateNode reply = StateNode::Object();
    reply["formats"] = StateNode::Object();
    reply["formats"]["floppy"] = StateNode::Array();
    reply["formats"]["floppy"].push("trd");
    reply["formats"]["floppy"].push("scl");
    EXPECT_EQ(MediaFileFilter(reply, "floppy"), "Disk images (*.trd *.scl);;All files (*)");
    EXPECT_EQ(MediaFileFilter(reply, "tape"), "All files (*)");

    StateNode dirty = StateNode::Object();
    dirty["ok"] = false;
    dirty["error"] = "dirty";
    EXPECT_TRUE(MediaNeedsDisposition(dirty));
    dirty["error"] = "unknown-slot";
    EXPECT_FALSE(MediaNeedsDisposition(dirty));
}
