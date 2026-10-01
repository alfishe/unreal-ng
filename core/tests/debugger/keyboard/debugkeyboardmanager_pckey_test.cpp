#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/memory/atm/evoavr.h"
#include "emulator/ports/models/portdecoder_atm3.h"

/// PressCombo/ReleaseCombo/TapCombo(vector<string>) used to resolve every name
/// through ResolveKeyName (ZX-only) and silently drop names with no ZX matrix
/// equivalent at all (e.g. "f12", "rshift" - TS-Conf's BIOS Setup entry is
/// Right Shift + F12, neither half a ZX Spectrum key). A name ResolveKeyName
/// can't place now falls back to pckey::FromName, same as the single-key
/// PressKey(string)/TapKey(string) overloads already did.

TEST(DebugKeyboardManagerPcKey_Test, ComboOfTwoPcOnlyNamesReachesThePs2Controller)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* atm3 = dynamic_cast<PortDecoder_ATM3*>(context->pPortDecoder);
    ASSERT_NE(atm3, nullptr);
    EvoAvr& avr = atm3->GetEvoAvr();
    ASSERT_EQ(context->pKeyboard->GetPs2Sink(), &avr) << "the ZX-Evo decoder attaches its AVR";

    DebugKeyboardManager* keys = emulator->GetDebugManager()->GetKeyboardManager();
    ASSERT_NE(keys, nullptr);

    // Both names resolve to ZXKEY_NONE - before the fix, PressCombo/ReleaseCombo
    // pushed ZXKEY_NONE into the ZX key list and ApplyKey silently no-op'd it,
    // so the AVR's PS/2 log never grew at all
    keys->PressCombo({"rshift", "f12"});
    EXPECT_EQ(avr.GetPs2LogCount(), 2u) << "RightShift (0x59) + F12 (0x07) make codes, 1 byte each";

    keys->ReleaseCombo({"rshift", "f12"});
    EXPECT_EQ(avr.GetPs2LogCount(), 6u) << "+ 2 break codes (F0 + code), 2 bytes each, cumulative";

    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(DebugKeyboardManagerPcKey_Test, TapComboOfTwoPcOnlyNamesPressesAndReleases)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* atm3 = dynamic_cast<PortDecoder_ATM3*>(context->pPortDecoder);
    ASSERT_NE(atm3, nullptr);
    EvoAvr& avr = atm3->GetEvoAvr();

    DebugKeyboardManager* keys = emulator->GetDebugManager()->GetKeyboardManager();
    ASSERT_NE(keys, nullptr);

    keys->TapCombo({"rshift", "f12"}, 2);
    emulator->RunNFrames(5);  // comfortably past the tap's 2-frame hold: queued release fires
    EXPECT_EQ(avr.GetPs2LogCount(), 6u) << "2 make + 2 break bytes (RightShift) + 2 break... "
                                           "same totals as the immediate Press/ReleaseCombo test";

    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(DebugKeyboardManagerPcKey_Test, AllKeyNamesAdvertisesPcOnlyNames)
{
    const std::vector<std::string> names = DebugKeyboardManager::GetAllKeyNames();
    EXPECT_NE(std::find(names.begin(), names.end(), "f12"), names.end())
        << "a PC-only key (no ZX equivalent) must be discoverable, not just silently accepted";
    EXPECT_NE(std::find(names.begin(), names.end(), "rshift"), names.end());
}
