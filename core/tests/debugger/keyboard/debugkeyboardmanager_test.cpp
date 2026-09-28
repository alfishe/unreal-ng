#include <gtest/gtest.h>

#include <string>

#include "_helpers/romeditortesthelper.h"

/// Keyboard input against the real ROMs: the keys go through the matrix and
/// the machine's own keyboard routine decides what was typed, read back from
/// the screen, on every editor of RomEditorFixture::RomEditors().
/// Every editor of the matrix, as a test parameter
class DebugKeyboardManagerRomEditor_Test : public RomEditorFixture,
                                           public ::testing::WithParamInterface<std::string>
{
};

INSTANTIATE_TEST_SUITE_P(Editors, DebugKeyboardManagerRomEditor_Test,
                         ::testing::ValuesIn(RomEditorFixture::RomEditors()), RomEditorFixture::ParamName);

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
