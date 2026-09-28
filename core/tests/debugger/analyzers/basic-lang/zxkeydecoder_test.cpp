#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "_helpers/romeditortesthelper.h"
#include "debugger/analyzers/basic-lang/editormonitor.h"
#include "debugger/analyzers/basic-lang/romcontrolpoints.h"
#include "debugger/analyzers/basic-lang/zxkeydecoder.h"

using ZXKeyDecoder::Shift;

/// ZXKeyDecoder against the ROM's own keyboard routine: every letter in K, L
/// and E mode, plain, with CAPS and with SYMBOL, and the digits with SYMBOL,
/// are pressed through the matrix; the code the ROM's keyboard routine puts in
/// LAST_K (seen at the keyTaken control point) must equal the prediction made
/// from the live MODE/FLAGS/FLAGS2 just before the press. (keyAccepted is not
/// used: a mode key such as CAPS + SYMBOL is handled inside KEY-INPUT and never
/// comes back to the editor loop.)
///
/// About 1000 frames per editor (one press is ~4 frames): this is the
/// exhaustive check of the table-driven decoding every typed command relies
/// on, run on the two 48 BASIC variants that differ (Sinclair, Amstrad +3).
class ZXKeyDecoder_Test : public RomEditorFixture, public ::testing::WithParamInterface<std::string>
{
protected:
    EditorMonitor* _monitor = nullptr;
    const uint8_t* _rom = nullptr;
    int _checked = 0;

    void Prepare()
    {
        BootEditor(GetParam());
        ASSERT_FALSE(HasFatalFailure());
        _monitor = _context->pDebugManager->GetEditorMonitor();
        _monitor->Arm();

        for (int page = 0; page < 64 && _rom == nullptr; page++)
        {
            const uint8_t* bytes = _context->pMemory->ROMPageHostAddress(static_cast<uint8_t>(page));
            if (ROMControlPoints::Identify(bytes) == ROMControlPoints::RomKind::Basic48)
                _rom = bytes;
        }
        ASSERT_NE(_rom, nullptr);

        // Clear the line: the `0` BootEditor typed may be on it
        Press({ ZXKEY_CAPS_SHIFT, ZXKEY_0 });
    }

    ZXKeyDecoder::EditorState State() const
    {
        ZXKeyDecoder::EditorState state;
        state.mode = SysVar(0x5C41);
        state.flags = SysVar(0x5C3B);
        state.flags2 = SysVar(0x5C6A);
        return state;
    }

    /// Presses `keys` together; returns the code the ROM decoded, -1 if none
    int Press(const std::vector<ZXKeysEnum>& keys)
    {
        _monitor->ClearEvents();
        if (keys.size() == 1)
            _keys->TapKey(keys[0]);
        else
            _keys->TapCombo(keys);

        int taken = -1;
        RunUntil([&] {
            for (const EditorMonitor::Event& e : _monitor->Events())
            {
                if (e.point == ROMControlPoints::Point::KeyTaken)
                    taken = e.lastK;
            }
            return taken >= 0 && !_keys->IsSequenceRunning();
        }, 40);
        RunFrames(1);  // the editor handles the key before the next one
        return taken;
    }

    /// Presses `key` with `shift`; the accepted code must be the prediction
    void Check(ZXKeysEnum key, Shift shift, const char* where)
    {
        const ZXKeyDecoder::EditorState state = State();
        const std::optional<uint8_t> expected = ZXKeyDecoder::Decode(_rom, key, shift, state);
        ASSERT_TRUE(expected.has_value());

        std::vector<ZXKeysEnum> keys;
        if (shift == Shift::Caps)
            keys.push_back(ZXKEY_CAPS_SHIFT);
        if (shift == Shift::Symbol)
            keys.push_back(ZXKEY_SYM_SHIFT);
        keys.push_back(key);

        const int got = Press(keys);
        EXPECT_EQ(got, *expected) << where << " key " << DebugKeyboardManager::GetKeyDisplayName(key)
                                  << " shift " << std::hex << int(static_cast<uint8_t>(shift)) << " MODE="
                                  << int(state.mode) << " FLAGS=" << int(state.flags);
        _checked++;
    }

    static std::vector<ZXKeysEnum> Letters()
    {
        return { ZXKEY_A, ZXKEY_B, ZXKEY_C, ZXKEY_D, ZXKEY_E, ZXKEY_F, ZXKEY_G, ZXKEY_H, ZXKEY_I,
                 ZXKEY_J, ZXKEY_K, ZXKEY_L, ZXKEY_M, ZXKEY_N, ZXKEY_O, ZXKEY_P, ZXKEY_Q, ZXKEY_R,
                 ZXKEY_S, ZXKEY_T, ZXKEY_U, ZXKEY_V, ZXKEY_W, ZXKEY_X, ZXKEY_Y, ZXKEY_Z };
    }
};

INSTANTIATE_TEST_SUITE_P(Editors, ZXKeyDecoder_Test, ::testing::Values("48K", "Plus3-48BASIC"),
                         RomEditorFixture::ParamName);

TEST_P(ZXKeyDecoder_Test, EveryKeyDecodesAsTheRomDoes)
{
    Prepare();
    ASSERT_FALSE(HasFatalFailure());

    // K mode: an empty line; each keyword is deleted again to stay in K mode
    for (ZXKeysEnum key : Letters())
    {
        ASSERT_EQ(State().flags & 0x08, 0) << "not in K mode";
        Check(key, Shift::None, "K");
        Press({ ZXKEY_CAPS_SHIFT, ZXKEY_0 });
        Check(key, Shift::Symbol, "K");
        Press({ ZXKEY_CAPS_SHIFT, ZXKEY_0 });
    }

    // L mode: after REM (E in K mode) everything is L mode
    Check(ZXKEY_E, Shift::None, "K");
    ASSERT_NE(State().flags & 0x08, 0) << "not in L mode after REM";
    for (ZXKeysEnum key : Letters())
    {
        Check(key, Shift::None, "L");
        Check(key, Shift::Caps, "L");
        Check(key, Shift::Symbol, "L");
    }
    for (ZXKeysEnum key : { ZXKEY_0, ZXKEY_1, ZXKEY_2, ZXKEY_3, ZXKEY_4, ZXKEY_5, ZXKEY_6, ZXKEY_7, ZXKEY_8, ZXKEY_9 })
    {
        Check(key, Shift::None, "L");
        Check(key, Shift::Symbol, "L");
    }

    // E mode: CAPS + SYMBOL, then the key (plain and with SYMBOL)
    for (ZXKeysEnum key : Letters())
    {
        for (Shift shift : { Shift::None, Shift::Symbol })
        {
            ASSERT_EQ(Press({ ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT }), 0x0E);
            ASSERT_EQ(State().mode, 1) << "not in E mode";
            Check(key, shift, "E");
        }
    }

    EXPECT_EQ(_checked, 203);  // K 52 + REM 1 + L 78 + digits 20 + E 52
}

TEST(ZXKeyDecoderUnit_Test, FindPrefersPlainKeysAndUsesEModeWhenNeeded)
{
    MessageCenter::DisposeDefaultMessageCenter();
    auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("kd-unit", "48K", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    const uint8_t* rom = emulator->GetContext()->pMemory->ROMPageHostAddress(0);

    ZXKeyDecoder::EditorState kMode;             // FLAGS bit 3 clear
    ZXKeyDecoder::EditorState lMode;
    lMode.flags = 0x08;

    // LOAD (#EF) is J in K mode
    auto load = ZXKeyDecoder::Find(rom, 0xEF, kMode);
    ASSERT_TRUE(load);
    EXPECT_FALSE(load->extendedFirst);
    EXPECT_EQ(load->keys, std::vector<ZXKeysEnum>{ ZXKEY_J });

    // " is SYMBOL + P in L mode
    auto quote = ZXKeyDecoder::Find(rom, '"', lMode);
    ASSERT_TRUE(quote);
    EXPECT_EQ(quote->keys, (std::vector<ZXKeysEnum>{ ZXKEY_SYM_SHIFT, ZXKEY_P }));

    // CODE (#AF) is E mode + I
    auto code = ZXKeyDecoder::Find(rom, 0xAF, lMode);
    ASSERT_TRUE(code);
    EXPECT_TRUE(code->extendedFirst);
    EXPECT_EQ(code->keys, std::vector<ZXKeysEnum>{ ZXKEY_I });

    // A lower-case letter cannot be typed in K mode
    EXPECT_FALSE(ZXKeyDecoder::Find(rom, 'a', kMode).has_value());

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    MessageCenter::DisposeDefaultMessageCenter();
}
