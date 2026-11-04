#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/ttd/engine/ttdbytes.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdbookmarkexport.h"
#include "debugger/ttd/ttdbookmarks.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdsessionfacts.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

/// TTD agent bookmarks (docs/inprogress/2026-11-03-ttd-agent-bookmarks, plan.md B1-B8): a counter program recorded a
/// few frames; bookmarks with notes, tags, the context and watched bytes added, edited, listed, exported and saved
class TTDAgentBookmarks_Test : public ::testing::Test
{
protected:
    static constexpr uint16_t kOrg = 0x8000;
    static constexpr uint16_t kIsr = 0xFDFD;   // the IM 2 handler: I = #FE, the table at #FE00 all #FD

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    std::vector<uint64_t> _frames;

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    /// di; ld a,#FE; ld i,a; im 2; ei; loop: halt; ld hl,#A000; inc (hl); jr loop (one increment a frame)
    void Record(unsigned frames)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-agent-bookmarks", "PENTAGON",
                                                                            LoggerLevel::LogNone);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kDebugMode, true);
        _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _emulator->EnableTurboMode();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);

        Memory* memory = _context->pMemory;
        const std::vector<uint8_t> code = {0xF3, 0x3E, 0xFE, 0xED, 0x47, 0xED, 0x5E, 0xFB,
                                           0x76, 0x21, 0x00, 0xA0, 0x34, 0x18, 0xF9};
        for (size_t i = 0; i < code.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kOrg + i), code[i]);
        memory->DirectWriteToZ80Memory(0xA000, 0x00);
        for (uint16_t i = 0; i <= 0x100; ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xFE00 + i), 0xFD);
        const uint8_t isr[] = {0xFB, 0xED, 0x4D};   // ei; reti
        for (size_t i = 0; i < sizeof(isr); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kIsr + i), isr[i]);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = kOrg;
        z80->sp = 0xBF00;
        z80->iff1 = z80->iff2 = 0;
        z80->halted = 0;

        ASSERT_TRUE(_ttd->StartRecording());
        _frames.clear();
        for (unsigned n = 0; n < frames; ++n)
        {
            _frames.push_back(_ttd->CurrentPosition().frame);
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        }
        _ttd->StopRecording();
        _ttd->SeekTo(_ttd->SessionEndPosition());
    }

    static ttd::TTDBookmark Mark(const std::string& label, ttd::TTDTimePoint at, const std::string& note = "")
    {
        ttd::TTDBookmark b;
        b.label = label;
        b.time = at;
        b.note = note;
        return b;
    }

    static const StateNode* Find(const StateNode& n, const char* key) { return n.find(key); }
};

/// B1: a bookmark at the current position: the machine read as it stands (no seek), the note, the tags and the
/// watched byte kept
TEST_F(TTDAgentBookmarks_Test, B1_HereTakesTheContextAsTheMachineStands)
{
    Record(5);
    ttd::TTDBookmark b = Mark("here", _ttd->CurrentPosition(), "the counter after five frames");
    b.tags = {"counter", "end"};
    b.watches.push_back({0xA000, 1, {}});
    std::string err;
    ASSERT_TRUE(_ttd->AddAgentBookmark(b, true, &err)) << err;

    ttd::TTDBookmark got;
    ASSERT_TRUE(_ttd->FindBookmark("here", got));
    EXPECT_EQ(got.note, "the counter after five frames");
    EXPECT_EQ(got.tags, (std::vector<std::string>{"counter", "end"}));
    ASSERT_TRUE(got.context.valid);
    const Z80* z80 = _context->pCore->GetZ80();
    EXPECT_EQ(got.context.pc, z80->pc);
    EXPECT_EQ(got.context.sp, z80->sp);
    EXPECT_EQ(got.context.im, 2u);
    EXPECT_EQ(got.context.codeKind, 1u) << "the program runs from RAM";
    EXPECT_EQ(got.context.codePage, got.context.pc >= 0xC000 ? 0u : 2u) << "the loop in bank 2, the handler in bank 0";
    EXPECT_FALSE(got.context.instruction.empty());
    ASSERT_EQ(got.watches.size(), 1u);
    ASSERT_EQ(got.watches[0].bytes.size(), 1u);
    EXPECT_EQ(got.watches[0].bytes[0], _context->pMemory->DirectReadFromZ80Memory(0xA000));
}

/// B2: a bookmark in the past: one silent seek takes the moment's registers and bytes, the machine is put back
TEST_F(TTDAgentBookmarks_Test, B2_ThePastTakenByOneSilentSeekTheMachineStays)
{
    Record(6);
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();
    const uint16_t pc = _context->pCore->GetZ80()->pc;
    const uint8_t counterNow = _context->pMemory->DirectReadFromZ80Memory(0xA000);
    ttd::TTDBookmark b = Mark("past", {_frames[2], 3000});
    b.watches.push_back({0xA000, 1, {}});
    b.watches.push_back({0x8009, 3, {}});
    std::string err;
    ASSERT_TRUE(_ttd->AddAgentBookmark(b, true, &err)) << err;
    EXPECT_EQ(_ttd->CurrentPosition().frame, before.frame);
    EXPECT_EQ(_ttd->CurrentPosition().tInFrame, before.tInFrame);
    EXPECT_EQ(_context->pCore->GetZ80()->pc, pc);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xA000), counterNow);

    ttd::TTDBookmark got;
    ASSERT_TRUE(_ttd->FindBookmark("past", got));
    ASSERT_TRUE(got.context.valid);
    ASSERT_EQ(got.watches[1].bytes, (std::vector<uint8_t>{0x21, 0x00, 0xA0})) << "ld hl,#A000 at #8009";

    // The truth: a seek to the same moment
    ASSERT_TRUE(_ttd->SeekTo({_frames[2], 3000}));
    EXPECT_EQ(got.context.pc, _context->pCore->GetZ80()->pc);
    EXPECT_EQ(got.context.af, _context->pCore->GetZ80()->af);
    EXPECT_EQ(got.watches[0].bytes[0], _context->pMemory->DirectReadFromZ80Memory(0xA000));
    EXPECT_NE(got.watches[0].bytes[0], counterNow) << "the counter moved on after frame 2";
}

/// B3: context=false: no seek, nothing read; the old two-argument form is that
TEST_F(TTDAgentBookmarks_Test, B3_WithoutContextNothingIsRead)
{
    Record(4);
    ttd::TTDBookmark b = Mark("bare", {_frames[1], 0});
    b.watches.push_back({0xA000, 1, {}});
    std::string err;
    ASSERT_TRUE(_ttd->AddAgentBookmark(b, false, &err)) << err;
    ASSERT_TRUE(_ttd->AddBookmark({_frames[2], 0}, "old-form", &err)) << err;
    ttd::TTDBookmark got;
    ASSERT_TRUE(_ttd->FindBookmark("bare", got));
    EXPECT_FALSE(got.context.valid);
    ASSERT_EQ(got.watches.size(), 1u);
    EXPECT_TRUE(got.watches[0].bytes.empty());
    ASSERT_TRUE(_ttd->FindBookmark("old-form", got));
    EXPECT_FALSE(got.context.valid);
}

/// B4: edits: append to the note, tags added and removed, a rename; a duplicate label, a broken bound and an
/// unknown label refused with the bookmark unchanged
TEST_F(TTDAgentBookmarks_Test, B4_EditsAndTheirRefusals)
{
    Record(4);
    std::string err;
    ASSERT_TRUE(_ttd->AddAgentBookmark(Mark("a", {_frames[1], 0}, "first line"), false, &err)) << err;
    ASSERT_TRUE(_ttd->AddAgentBookmark(Mark("b", {_frames[2], 0}), false, &err)) << err;

    ttd::TTDControl control(_context);
    ttd::TTDReply r = control.Execute({"bookmark-note", {{"label", "a"}, {"note", "second line"}, {"append", "true"},
                                                         {"add_tags", "lives, hud"}}});
    ASSERT_TRUE(r.Ok()) << r.message;
    r = control.Execute({"bookmark-note", {{"label", "a"}, {"remove_tags", "hud"}, {"rename", "lives-drop"}}});
    ASSERT_TRUE(r.Ok()) << r.message;
    ttd::TTDBookmark got;
    ASSERT_FALSE(_ttd->FindBookmark("a", got));
    ASSERT_TRUE(_ttd->FindBookmark("lives-drop", got));
    EXPECT_EQ(got.note, "first line\nsecond line");
    EXPECT_EQ(got.tags, std::vector<std::string>{"lives"});
    EXPECT_EQ(got.time.frame, _frames[1]);

    r = control.Execute({"bookmark-note", {{"label", "lives-drop"}, {"rename", "b"}}});
    EXPECT_EQ(r.error, ttd::TTDControlError::Conflict) << r.message;
    r = control.Execute({"bookmark-note", {{"label", "lives-drop"}, {"note", std::string(5000, 'x')}}});
    EXPECT_EQ(r.error, ttd::TTDControlError::BadRequest) << r.message;
    r = control.Execute({"bookmark-note", {{"label", "lives-drop"}, {"tags", "bad tag"}}});
    EXPECT_EQ(r.error, ttd::TTDControlError::BadRequest) << r.message;
    r = control.Execute({"bookmark-note", {{"label", "nosuch"}, {"note", "x"}}});
    EXPECT_EQ(r.error, ttd::TTDControlError::NotFound) << r.message;
    r = control.Execute({"bookmark-note", {{"label", "lives-drop"}}});
    EXPECT_EQ(r.error, ttd::TTDControlError::BadRequest) << "nothing to change";
    ASSERT_TRUE(_ttd->FindBookmark("lives-drop", got));
    EXPECT_EQ(got.note, "first line\nsecond line") << "the refused edits left it unchanged";
}

/// B5: the verbs: add with note / tags / watch (the context taken by default), the list by tag with its text
TEST_F(TTDAgentBookmarks_Test, B5_AddAndListThroughTheVerbs)
{
    Record(5);
    ttd::TTDControl control(_context);
    ttd::TTDReply r = control.Execute({"bookmark-add", {{"label", "two"}, {"frame", std::to_string(_frames[2])},
                                                        {"note", "counter is 2 here"}, {"tags", "counter"},
                                                        {"watch", "#A000,0x8009:3"}}});
    ASSERT_TRUE(r.Ok()) << r.message;
    const StateNode* bm = r.body.find("bookmark");
    ASSERT_NE(bm, nullptr);
    ASSERT_NE(bm->find("context"), nullptr);
    EXPECT_NE(bm->find("context")->find("pc"), nullptr) << "the context taken by default";
    ASSERT_TRUE(control.Execute({"bookmark-add", {{"label", "three"}, {"frame", std::to_string(_frames[3])},
                                                  {"context", "false"}}})
                    .Ok());

    r = control.Execute({"bookmarks", {{"tag", "counter"}, {"text", "true"}}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.body.find("count")->i, 1);
    const std::string text = r.body.find("text")->s;
    EXPECT_NE(text.find("'two'"), std::string::npos) << text;
    EXPECT_NE(text.find("counter is 2 here"), std::string::npos) << text;
    EXPECT_NE(text.find("#A000="), std::string::npos) << text;
    EXPECT_EQ(text.find("'three'"), std::string::npos) << text;
    EXPECT_EQ(control.Execute({"bookmarks", {}}).body.find("count")->i, 2);

    EXPECT_EQ(control.Execute({"bookmark-add", {{"label", "w"}, {"watch", "#A000:17"}}}).error,
              ttd::TTDControlError::BadRequest);
    EXPECT_EQ(control.Execute({"bookmark-add", {{"label", "w"}, {"watch", "#10000"}}}).error,
              ttd::TTDControlError::BadRequest);
    EXPECT_EQ(control.Execute({"bookmark-add", {{"label", "w"}, {"tags", "a,b,c,d,e,f,g,h,i"}}}).error,
              ttd::TTDControlError::BadRequest);
}

/// B6: export: the WALKTHROUGH section (Markdown), the profile comment, JSON; by tag; to a file
TEST_F(TTDAgentBookmarks_Test, B6_ExportInEveryFormat)
{
    Record(5);
    ttd::TTDControl control(_context);
    ASSERT_TRUE(control.Execute({"bookmark-add", {{"label", "inc"}, {"frame", std::to_string(_frames[2])},
                                                  {"note", "inc (hl) counts the frames"}, {"tags", "counter"},
                                                  {"watch", "#A000"}}})
                    .Ok());
    ASSERT_TRUE(control.Execute({"bookmark-add", {{"label", "other"}, {"frame", std::to_string(_frames[3])}}}).Ok());

    ttd::TTDReply md = control.Execute({"bookmark-export", {{"title", "Counter"}, {"tag", "counter"}}});
    ASSERT_TRUE(md.Ok()) << md.message;
    const std::string m = md.body.find("text")->s;
    EXPECT_EQ(m.rfind("## Counter\n", 0), 0u) << m;
    EXPECT_NE(m.find("### inc (`f" + std::to_string(_frames[2]) + "+0`)"), std::string::npos) << m;
    EXPECT_NE(m.find("inc (hl) counts the frames"), std::string::npos) << m;
    EXPECT_NE(m.find("- **At:** `PC #"), std::string::npos) << m;
    EXPECT_NE(m.find("- **Watched:** `#A000="), std::string::npos) << m;
    EXPECT_NE(m.find("seek_bookmark"), std::string::npos) << m;
    EXPECT_EQ(m.find("### other"), std::string::npos) << "filtered by tag\n" << m;

    ttd::TTDReply profile = control.Execute({"bookmark-export", {{"format", "profile"}, {"title", "Counter"}}});
    ASSERT_TRUE(profile.Ok()) << profile.message;
    const std::string p = profile.body.find("text")->s;
    std::istringstream lines(p);
    std::string line;
    while (std::getline(lines, line))
    {
        EXPECT_EQ(line.rfind("#", 0), 0u) << "every line a YAML comment: " << line;
        EXPECT_LE(line.size(), 120u) << line;
    }
    EXPECT_NE(p.find("- inc f" + std::to_string(_frames[2]) + "+0, PC #"), std::string::npos) << p;
    EXPECT_NE(p.find("- other f"), std::string::npos) << p;

    ttd::TTDReply json = control.Execute({"bookmark-export", {{"format", "json"}}});
    ASSERT_TRUE(json.Ok()) << json.message;
    EXPECT_NE(json.body.find("text")->s.find("\"label\""), std::string::npos);
    EXPECT_EQ(json.body.find("count")->i, 2);

    EXPECT_EQ(control.Execute({"bookmark-export", {{"format", "pdf"}}}).error, ttd::TTDControlError::BadRequest);
}

/// B7: the .ttd file keeps the note, the tags, the context and the watched bytes
TEST_F(TTDAgentBookmarks_Test, B7_TheFileKeepsEverything)
{
    Record(4);
    ttd::TTDBookmark b = Mark("saved", {_frames[2], 100}, "kept\nacross a save");
    b.tags = {"x", "y"};
    b.watches.push_back({0xA000, 2, {}});
    std::string err;
    ASSERT_TRUE(_ttd->AddAgentBookmark(b, true, &err)) << err;
    ttd::TTDBookmark before;
    ASSERT_TRUE(_ttd->FindBookmark("saved", before));

    std::ostringstream out;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;
    std::istringstream in(out.str());
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;
    ttd::TTDBookmark after;
    ASSERT_TRUE(_ttd->FindBookmark("saved", after));
    EXPECT_EQ(after.note, before.note);
    EXPECT_EQ(after.tags, before.tags);
    EXPECT_TRUE(after.context.valid);
    EXPECT_EQ(after.context.pc, before.context.pc);
    EXPECT_EQ(after.context.af, before.context.af);
    EXPECT_EQ(after.context.instruction, before.context.instruction);
    EXPECT_EQ(after.context.codePage, before.context.codePage);
    ASSERT_EQ(after.watches.size(), 1u);
    EXPECT_EQ(after.watches[0].bytes, before.watches[0].bytes);
}

/// B8: a version 1 stream (frame, T, label) still reads; an unknown version does not
TEST(TTDAgentBookmarks_Stream_Test, B8_VersionOneStillReads)
{
    ttd::TTDByteWriter w;
    w.U16(1);
    w.Varint(2);
    w.U64(10);
    w.U32(0);
    w.Str("boot");
    w.U64(20);
    w.U32(500);
    w.Str("title");
    ttd::TTDBookmarkJournal journal;
    ASSERT_TRUE(ttd::DecodeBookmarks(w.bytes, journal));
    const auto all = journal.Snapshot();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[1].label, "title");
    EXPECT_EQ(all[1].time.tInFrame, 500u);
    EXPECT_TRUE(all[1].note.empty());
    EXPECT_FALSE(all[1].context.valid);

    std::vector<uint8_t> v3 = w.bytes;
    v3[0] = 3;
    EXPECT_FALSE(ttd::DecodeBookmarks(v3, journal));
}
