// The asm-synchronizer's Live Source window (phase Y2): the source a dumped TASM 4.12 session holds in RAM, shown as
// the watch built it, with the guest's cursor line highlighted and the status line; the watch it starts is its own

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "debugger/asm/sync/asmsyncservice.h"
#include "debugger/debugmanager.h"
#include "debugger/livesourcewindow.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

namespace
{
/// Pages 2 and 6 of a dumped session (unreal-asm testdata/sync) into the machine, page 6 at #C000 as it was
void LoadSession(EmulatorContext* context, const std::string& folder)
{
    const std::filesystem::path dir =
        TestPathHelper::FindProjectRoot() / "core" / "src" / "3rdparty" / "unreal-asm" / "testdata" / "sync" / folder;
    for (const uint16_t page : {uint16_t(2), uint16_t(6)})
    {
        std::ifstream in(dir / ("page" + std::to_string(page) + ".bin"), std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ASSERT_EQ(bytes.size(), 0x4000u);
        std::copy(bytes.begin(), bytes.end(), context->pMemory->RAMPageAddress(page));
    }
    context->pMemory->SetRAMPageToBank3(6);
}

class LiveSourceWindow_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("live-source", "PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        LoadSession(_emulator->GetContext(), "tasm412-typing");
        _sync = _emulator->GetContext()->pDebugManager->GetAsmSyncService();
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    std::shared_ptr<Emulator> _emulator;
    AsmSyncService* _sync = nullptr;
};
}  // namespace

TEST_F(LiveSourceWindow_Test, TheWindowShowsTheBuiltTextAndTheCursorLine)
{
    // A watch already running (interval 50 ms, no quiet period): the window shows it and leaves it on when closed.
    // Waits on the worker's first build (two looks of 50 ms)
    AsmSyncService::WatchOptions options;
    options.intervalMs = 50;
    options.quietMs = 0;
    _sync->Start(options);
    {
        LiveSourceWindow window(_emulator.get());
        auto* text = window.findChild<QPlainTextEdit*>();
        ASSERT_NE(text, nullptr);
        ASSERT_TRUE(TestWait::For([&] {
            QApplication::processEvents();
            window.refresh();
            return text->document()->blockCount() > 100;
        })) << "the first build's text";
        EXPECT_EQ(text->document()->blockCount(), 104);

        // The guest's cursor (TASM's editor, line 26) highlighted
        bool cursor = false;
        for (const QTextEdit::ExtraSelection& s : text->extraSelections())
            cursor = cursor || s.cursor.blockNumber() == 25;
        EXPECT_TRUE(cursor);

        const auto labels = window.findChildren<QLabel*>();
        ASSERT_FALSE(labels.isEmpty());
        EXPECT_TRUE(labels.front()->text().contains("TASM 4.12")) << labels.front()->text().toStdString();
        EXPECT_TRUE(labels.front()->text().contains("cursor at line 26")) << labels.front()->text().toStdString();
        EXPECT_TRUE(labels.front()->text().contains("1 errors, 2 warnings")) << "SNAKE does not settle (an error without a line)";
        auto* hints = window.findChild<QListWidget*>();
        ASSERT_NE(hints, nullptr);
        EXPECT_EQ(hints->count(), 3);
        QPushButton* watch = nullptr;
        for (QPushButton* b : window.findChildren<QPushButton*>())
            if (b->isCheckable())
                watch = b;
        ASSERT_NE(watch, nullptr);
        EXPECT_TRUE(watch->isChecked());
    }
    EXPECT_TRUE(_sync->Watching()) << "not the window's watch: it stays";
    _sync->Stop();
}

TEST_F(LiveSourceWindow_Test, AWatchTheWindowStartsEndsWithIt)
{
    ASSERT_FALSE(_sync->Watching());
    {
        LiveSourceWindow window(_emulator.get());
        EXPECT_TRUE(_sync->Watching());
        QPushButton* watch = nullptr;
        for (QPushButton* b : window.findChildren<QPushButton*>())
            if (b->isCheckable())
                watch = b;
        ASSERT_NE(watch, nullptr);
        watch->setChecked(false);
        EXPECT_FALSE(_sync->Watching()) << "Watch off stops it";
        watch->setChecked(true);
        EXPECT_TRUE(_sync->Watching());
    }
    EXPECT_FALSE(_sync->Watching()) << "closed: the window's own watch ends";
}
