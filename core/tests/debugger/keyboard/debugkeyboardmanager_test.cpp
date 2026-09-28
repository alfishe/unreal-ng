#include <gtest/gtest.h>

#include <functional>
#include <string>

#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"

/// Keyboard input against the real ROMs of several machines: the keys go
/// through the matrix and the machine's own keyboard routine decides what was
/// typed, read back from the screen. Frames are driven synchronously through
/// MainLoop::RunFrame (no emulator thread, no wall clock); every wait runs
/// only until its condition holds, with a frame cap as the failure bound.
///
/// Editors covered: the Sinclair 48K ROM, the 128K editor and 48 BASIC of the
/// Spectrum 128 and the Pentagon, and the Amstrad 48 BASIC ROM of the +3.
class DebugKeyboardManagerRom_Test : public ::testing::Test
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    MainLoop_CUT* _loop = nullptr;
    DebugKeyboardManager* _keys = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
        _emulator.reset();
        MessageCenter::DisposeDefaultMessageCenter();
    }

    std::string Screen() const
    {
        return ScreenOCR::ocrScreen(_emulator->GetUUID());
    }

    /// Runs frames until `done` holds; returns false after `maxFrames`
    bool RunUntil(const std::function<bool()>& done, int maxFrames)
    {
        for (int i = 0; i < maxFrames; i++)
        {
            if (done())
                return true;
            _loop->RunFrame();
        }
        return done();
    }

    bool ScreenHas(const std::string& text) const
    {
        return Screen().find(text) != std::string::npos;
    }

    /// Boots `model` from reset into the ROM `mode` selects and waits for
    /// `bootText` on screen
    void Boot(const std::string& model, ROMModeEnum mode, const std::string& bootText)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("kbd-rom", model, LoggerLevel::LogError);
        ASSERT_TRUE(_emulator) << model;
        _context = _emulator->GetContext();
        _context->config.reset_rom = mode;
        _emulator->Reset();
        _context->pFeatureManager->setFeature(Features::kScreenHQ, false);
        _context->pFeatureManager->setFeature(Features::kSoundHQ, false);
        _loop = reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);
        _keys = _context->pDebugManager->GetKeyboardManager();
        ASSERT_NE(_keys, nullptr);

        // About 90 frames on every model (memory test, then the banner)
        ASSERT_TRUE(RunUntil([&] { return ScreenHas(bootText); }, 250))
            << model << " never showed '" << bootText << "':\n" << Screen();
    }

    /// The ROM's interrupt puts each new key in LAST_K ($5C08) and sets FLAGS
    /// ($5C3B) bit 5; the editor clears the bit when it takes the key, within
    /// the same frame when it is waiting. Every editor of the matrix (48K,
    /// 128K, +3 48 BASIC) uses these system variables.
    uint8_t SysVar(uint16_t address) const
    {
        return _context->pMemory->DirectReadFromZ80Memory(address);
    }

    /// Taps `key` and waits until the ROM has stored `code` in LAST_K and
    /// the program has taken it (new-key flag cleared). A menu or banner can
    /// be on screen before the ROM's key loop runs, and code that runs with
    /// interrupts off (the 128K editor's error beep, for one) does not scan the
    /// keyboard at all: a press that falls there is never seen, so it is
    /// pressed again.
    void TapUntilTaken(ZXKeysEnum key, uint8_t code)
    {
        for (int attempt = 0; attempt < 10; attempt++)
        {
            _context->pMemory->DirectWriteToZ80Memory(0x5C08, 0x00);
            _keys->TapKey(key);
            const bool taken = RunUntil([&] {
                return SysVar(0x5C08) == code && (SysVar(0x5C3B) & 0x20) == 0 && !_keys->IsSequenceRunning();
            }, 60);
            if (taken)
                return;
        }
        FAIL() << "the ROM never took key code " << int(code) << ":\n" << Screen();
    }

    /// The editor reads the keyboard: a `0` typed at the start of the line has
    /// been taken (the 48K editor spends this first key clearing the report)
    void WaitForEditor()
    {
        TapUntilTaken(ZXKEY_0, '0');
    }


    /// From the 128K menu (Tape Loader highlighted) into the 128K editor
    void Enter128Editor()
    {
        TapUntilTaken(ZXKEY_EXT_DOWN, 0x0A);
        ASSERT_FALSE(HasFatalFailure());
        TapUntilTaken(ZXKEY_ENTER, 0x0D);
        ASSERT_FALSE(HasFatalFailure());
        // The editor draws its status line a few frames after the menu goes
        ASSERT_TRUE(RunUntil([&] { return ScreenHas("128 BASIC"); }, 50)) << Screen();
    }

    /// Runs until every queued key has been typed, then lets the editor echo it
    bool FinishTyping()
    {
        const bool done = RunUntil([&] { return !_keys->IsSequenceRunning(); }, 400);
        // The editor prints the character on the next interrupt after the key
        for (int i = 0; i < 2; i++)
            _loop->RunFrame();
        return done;
    }

    /// Boots one editor of the matrix
    void BootEditor(const std::string& editor)
    {
        if (editor == "48K")
            Boot("48K", RM_SOS, "1982 Sinclair");
        else if (editor == "128K-48BASIC")
            Boot("128k", RM_SOS, "1982 Sinclair");
        else if (editor == "Plus3-48BASIC")
            Boot("PLUS3", RM_SOS, "1982 Amstrad");
        else if (editor == "Pentagon-48BASIC")
            Boot("PENTAGON", RM_SOS, "1982 Sinclair");
        else if (editor == "128K-128BASIC" || editor == "Pentagon-128BASIC")
        {
            Boot(editor == "128K-128BASIC" ? "128k" : "PENTAGON", RM_128, "48 BASIC");
            if (!HasFatalFailure())
                Enter128Editor();
        }
        else
            FAIL() << "unknown editor " << editor;

        if (!HasFatalFailure())
            WaitForEditor();
    }
};

/// Every editor of the matrix, as a test parameter
class DebugKeyboardManagerRomEditor_Test : public DebugKeyboardManagerRom_Test,
                                           public ::testing::WithParamInterface<std::string>
{
};

INSTANTIATE_TEST_SUITE_P(Editors, DebugKeyboardManagerRomEditor_Test,
                         ::testing::Values("48K", "128K-48BASIC", "128K-128BASIC", "Plus3-48BASIC",
                                           "Pentagon-48BASIC", "Pentagon-128BASIC"),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                             std::string name = info.param;
                             for (char& c : name)
                                 if (c == '-')
                                     c = '_';
                             return name;
                         });

// Two identical keys in a row: the ROM only enters the second one after the
// key has been released for 5 interrupts (REPRESS_RELEASED_FRAMES)
TEST_P(DebugKeyboardManagerRomEditor_Test, RepeatedCharactersAreAllTyped)
{
    BootEditor(GetParam());
    ASSERT_FALSE(HasFatalFailure());

    _keys->TypeText("\"\"");
    ASSERT_TRUE(FinishTyping());

    EXPECT_TRUE(ScreenHas("\"\"")) << Screen();
}

// Separate calls, made back to back while earlier keys are still queued, are
// typed one after another; none cancels or merges with another
TEST_P(DebugKeyboardManagerRomEditor_Test, BackToBackCallsAreTypedInOrder)
{
    BootEditor(GetParam());
    ASSERT_FALSE(HasFatalFailure());

    _keys->TapCombo({ZXKEY_SYM_SHIFT, ZXKEY_P});
    _keys->TapCombo({ZXKEY_SYM_SHIFT, ZXKEY_P});
    _keys->TypeText("7");
    _keys->TapKey(ZXKEY_7);
    ASSERT_TRUE(FinishTyping());

    EXPECT_TRUE(ScreenHas("\"\"77")) << Screen();
}
