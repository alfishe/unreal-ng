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
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_atm3.h"

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
        return { "48K",           "128K-48BASIC",     "128K-128BASIC",     "Plus3-48BASIC",     "Plus3-3BASIC",
                 "Plus2-128BASIC", "Plus2A-3BASIC",
                 "Pentagon-48BASIC", "Pentagon-128BASIC", "Pentagon-TRDOS",
                 "Scorpion-48BASIC", "Scorpion-128BASIC", "Scorpion-TRDOS" };
    }

    /// The editors that run BASIC (every editor but the TR-DOS prompt)
    static std::vector<std::string> BasicEditors()
    {
        std::vector<std::string> editors;
        for (const std::string& editor : RomEditors())
        {
            if (editor.find("TRDOS") == std::string::npos)
                editors.push_back(editor);
        }
        return editors;
    }

    /// The TR-DOS A> prompt (the 48K editor, the line goes to TR-DOS)
    static std::vector<std::string> TrDosEditors()
    {
        std::vector<std::string> editors;
        for (const std::string& editor : RomEditors())
        {
            if (editor.find("TRDOS") != std::string::npos)
                editors.push_back(editor);
        }
        return editors;
    }

    /// The 48K editor running BASIC: keywords are single keys (K and E modes). The 128K, +2 and +3
    /// editors spell them out and tokenise at ENTER
    static std::vector<std::string> KeywordEditors()
    {
        std::vector<std::string> editors;
        for (const std::string& editor : BasicEditors())
        {
            if (editor.find("128BASIC") == std::string::npos && editor.find("-3BASIC") == std::string::npos)
                editors.push_back(editor);
        }
        return editors;
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
        // Mutes audio and decimates rendering: nothing here listens or looks at pixels (OCR reads video RAM)
        _emulator->EnableTurboMode();
        _loop = reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);
        _keys = _context->pDebugManager->GetKeyboardManager();
        ASSERT_NE(_keys, nullptr);

        ASSERT_TRUE(RunUntil([&] { return ScreenHas(bootText); }, 500))
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

    /// From the menu into the editor: "128 BASIC" / "+3 BASIC" is the second
    /// item on the 128K, Pentagon, Scorpion and +3 menus
    void Enter128Editor(const std::string& banner = "128 BASIC")
    {
        TapUntilTaken(ZXKEY_EXT_DOWN, 0x0A);
        ASSERT_FALSE(HasFatalFailure());
        TapUntilTaken(ZXKEY_ENTER, 0x0D);
        ASSERT_FALSE(HasFatalFailure());
        ASSERT_TRUE(RunUntil([&] { return ScreenHas(banner); }, 100)) << Screen();
    }

    /// ZX-Evo (BaseConf ROM zxevo-fe.rom) into TR-DOS at 3.5 MHz, as an owner does it. A reset always
    /// enters the EVO Reset Service (ERS) menu, which itself runs at 7 MHz. The CPU speed for what the
    /// ERS starts lives in the AVR's battery-backed NVRAM (pentevo rom/global_vars.a80): cell #EC bit 7
    /// = 3.5 MHz, #ED bit 7 = 14 MHz, both clear = 7 MHz, guarded by a CRC16 in #EE/#EF. On a blank or
    /// corrupt NVRAM the ERS writes its defaults (#ED = #04, #EC = #82: 3.5 MHz; page5/source/addons.a80
    /// CMOS_DEFAULT). The hidden main-menu key W steps the speed 3.5 -> 7 -> 14 -> 3.5 MHz and stores it
    /// with a fresh CRC (mainmenu/src/main.a80 CHNGTURBO). A reset with SPACE held then skips the menu
    /// and jumps straight to TR-DOS (CS: 128 BASIC, SS: 48 BASIC) at the stored speed
    /// (page0/source/services.a80, RAM_CODE). That path never runs the menu's virtual-drive set-up
    /// (#13BD resets to "all drives real"), so drive A is the real drive the .trd goes into
    void BootEvoTrDos()
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("rom-editor", "ATM3", LoggerLevel::LogError);
        ASSERT_TRUE(_emulator) << "ATM3";
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kScreenHQ, false);
        _context->pFeatureManager->setFeature(Features::kSoundHQ, false);
        _emulator->EnableTurboMode();
        _loop = reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);
        _keys = _context->pDebugManager->GetKeyboardManager();
        ASSERT_NE(_keys, nullptr);

        // The ERS main menu idles at #6117 with the BASIC48 ROM page 28 in window 0 (zxevo_ers_test.cpp)
        Memory* memory = _context->pMemory;
        const auto inMenu = [&] {
            return _context->pCore->GetZ80()->pc == 0x6117 && memory->IsBank0ROM() && memory->GetROMPage() == 28u;
        };
        ASSERT_TRUE(RunUntil(inMenu, 500)) << "the ERS main menu never came up";

        // W until the NVRAM says 3.5 MHz (none with the ERS defaults; at most two otherwise). The menu
        // reads keys through the ROM's LAST_K, lower case, and writes the NVRAM right after taking one
        EvoAvr& avr = static_cast<PortDecoder_ATM3*>(_context->pPortDecoder)->GetEvoAvr();
        const auto nvram = [&](uint8_t cell) { return avr.ReadRegister(cell); };
        for (int i = 0; i < 3 && ((nvram(0xEC) & 0x80) == 0 || (nvram(0xED) & 0x80) != 0); i++)
        {
            TapUntilTaken(ZXKEY_W, 'w');
            ASSERT_FALSE(HasFatalFailure());
            RunFrames(10);
        }
        ASSERT_TRUE((nvram(0xEC) & 0x80) != 0 && (nvram(0xED) & 0x80) == 0) << "the ERS never stored 3.5 MHz";

        // Reset with SPACE held from the start: the service ROM checks the keys before any menu
        _emulator->Reset();
        _context->pKeyboard->PressKey(ZXKEY_SPACE);
        RunFrames(25);
        _context->pKeyboard->ReleaseKey(ZXKEY_SPACE);
        ASSERT_TRUE(RunUntil([&] { return ScreenHas("A>"); }, 500)) << "no TR-DOS prompt:\n" << Screen();
        ASSERT_EQ(_context->emulatorState.hw_turbo_ratio, 1) << "the CPU must run at 3.5 MHz";
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
        {
            // Through the menu ("48 BASIC" is the 4th item), as a person does:
            // a reset straight into the 48K ROM skips the Scorpion ROM's own
            // set-up, and its error handler (RST 8 -> #3C98 -> service ROM)
            // then never returns
            Boot("SCORPION", RM_128, "48 BASIC");
            for (int i = 0; i < 3 && !HasFatalFailure(); i++)
                TapUntilTaken(ZXKEY_EXT_DOWN, 0x0A);
            if (!HasFatalFailure())
                TapUntilTaken(ZXKEY_ENTER, 0x0D);
            if (!HasFatalFailure())
                ASSERT_TRUE(RunUntil([&] { return ScreenHas("1982 Sinclair"); }, 300)) << Screen();
        }
        else if (editor == "Plus2-128BASIC")
        {
            // The +2's Amstrad ROM: the 128K editor moved, "Tape Tester" added to the menu
            Boot("PLUS2", RM_128, "48 BASIC");
            if (!HasFatalFailure())
                Enter128Editor();
        }
        else if (editor == "Plus2A-3BASIC")
        {
            // The +3's ROM without a disk controller: it calls itself a +2A
            Boot("PLUS2A", RM_128, "48 BASIC");
            if (!HasFatalFailure())
                Enter128Editor("+3 BASIC");
        }
        else if (editor == "Plus3-3BASIC")
        {
            // The +3 menu comes up after the RAM disk check (about 300 frames)
            Boot("PLUS3", RM_128, "48 BASIC");
            if (!HasFatalFailure())
                Enter128Editor("+3 BASIC");
        }
        else if (editor == "ATM710-TRDOS")  // not in RomEditors(): used by the contention probe's loads only
            Boot("ATM710", RM_DOS, "A>");
        else if (editor == "ATM3-TRDOS")  // not in RomEditors(): used by the contention probe's loads only
            BootEvoTrDos();
        else if (editor == "Profi-TRDOS")
            Boot("PROFI", RM_DOS, "A>");
        else if (editor == "ProfScorp-TRDOS")
            Boot("PROFSCORP", RM_DOS, "A>");
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
