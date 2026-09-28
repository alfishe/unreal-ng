#pragma once

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

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

/// Fixture base for tests against the real ROM editors of several machines.
/// Frames are driven synchronously through MainLoop::RunFrame (no emulator
/// thread, no wall clock); every wait runs only until its condition holds,
/// with a frame cap as the failure bound.
///
/// The editor matrix (RomEditors()):
///   48 BASIC  - 48K, and 128K / +3 / Pentagon / Scorpion booted into it
///   128K editor - 128K, Pentagon, Scorpion, entered from the menu
///   TR-DOS A> - Pentagon (5.04T) and Scorpion (5.03), booted into TR-DOS
class RomEditorFixture : public ::testing::Test
{
public:
    static std::vector<std::string> RomEditors()
    {
        return { "48K",           "128K-48BASIC",     "128K-128BASIC",     "Plus3-48BASIC",
                 "Pentagon-48BASIC", "Pentagon-128BASIC", "Pentagon-TRDOS",
                 "Scorpion-48BASIC", "Scorpion-128BASIC", "Scorpion-TRDOS" };
    }

    /// gtest parameter name: '-' is not allowed
    static std::string ParamName(const ::testing::TestParamInfo<std::string>& info)
    {
        std::string name = info.param;
        for (char& c : name)
        {
            if (c == '-')
                c = '_';
        }
        return name;
    }

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

    bool ScreenHas(const std::string& text) const
    {
        return Screen().find(text) != std::string::npos;
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

    void RunFrames(int frames)
    {
        for (int i = 0; i < frames; i++)
            _loop->RunFrame();
    }

    uint8_t SysVar(uint16_t address) const
    {
        return _context->pMemory->DirectReadFromZ80Memory(address);
    }

    /// Boots `model` from reset into the ROM `mode` selects and waits for
    /// `bootText` on screen (about 60-90 frames on every model)
    void Boot(const std::string& model, ROMModeEnum mode, const std::string& bootText)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("rom-editor", model, LoggerLevel::LogError);
        ASSERT_TRUE(_emulator) << model;
        _context = _emulator->GetContext();
        _context->config.reset_rom = mode;
        _emulator->Reset();
        _context->pFeatureManager->setFeature(Features::kScreenHQ, false);
        _context->pFeatureManager->setFeature(Features::kSoundHQ, false);
        _loop = reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);
        _keys = _context->pDebugManager->GetKeyboardManager();
        ASSERT_NE(_keys, nullptr);

        ASSERT_TRUE(RunUntil([&] { return ScreenHas(bootText); }, 250))
            << model << " never showed '" << bootText << "':\n" << Screen();
    }

    /// Taps `key` and waits until the ROM has stored `code` in LAST_K ($5C08)
    /// and the program has taken it (FLAGS $5C3B bit 5 cleared). A menu or
    /// banner can be on screen before the ROM's key loop runs, and code that
    /// runs with interrupts off (the 128K editor's error beep, for one) does not
    /// scan the keyboard at all: a press that falls there is never seen, so it
    /// is pressed again.
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

    /// Runs until every queued key has been typed, then lets the editor echo it
    bool FinishTyping()
    {
        const bool done = RunUntil([&] { return !_keys->IsSequenceRunning(); }, 400);
        RunFrames(2);  // the editor prints the character on the next interrupt
        return done;
    }

    /// From the 128K menu into the 128K editor: "128 BASIC" is the second item
    /// on the 128K, Pentagon and Scorpion menus
    void Enter128Editor()
    {
        TapUntilTaken(ZXKEY_EXT_DOWN, 0x0A);
        ASSERT_FALSE(HasFatalFailure());
        TapUntilTaken(ZXKEY_ENTER, 0x0D);
        ASSERT_FALSE(HasFatalFailure());
        ASSERT_TRUE(RunUntil([&] { return ScreenHas("128 BASIC"); }, 50)) << Screen();
    }

    /// Boots one editor of the matrix, ready to type
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
        else if (editor == "Scorpion-48BASIC")
            Boot("SCORPION", RM_SOS, "1982 Sinclair");
        else if (editor == "Pentagon-TRDOS")
            Boot("PENTAGON", RM_DOS, "A>");
        else if (editor == "Scorpion-TRDOS")
            Boot("SCORPION", RM_DOS, "A>");
        else if (editor == "128K-128BASIC" || editor == "Pentagon-128BASIC" || editor == "Scorpion-128BASIC")
        {
            const std::string model = editor == "128K-128BASIC" ? "128k" : (editor == "Pentagon-128BASIC" ? "PENTAGON" : "SCORPION");
            Boot(model, RM_128, "48 BASIC");
            if (!HasFatalFailure())
                Enter128Editor();
        }
        else
        {
            FAIL() << "unknown editor " << editor;
        }

        // The editor reads the keyboard: a `0` typed at the start of the line
        // has been taken (the 48K editor spends this first key clearing the
        // report, so it may not show)
        if (!HasFatalFailure())
            TapUntilTaken(ZXKEY_0, '0');
    }
};
