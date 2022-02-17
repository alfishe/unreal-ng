#include "keyboard_test.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void Keyboard_Test::SetUp()
{
    _context = new EmulatorContext();
    _keyboard = new KeyboardCUT(_context);
}

void Keyboard_Test::TearDown()
{
    if (_keyboard != nullptr)
    {
        delete _keyboard;
        _keyboard = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(Keyboard_Test, isExtendedKey)
{
    for (uint8_t key = 0; key < 255; key++)
    {
        bool refValue = key >= ZXKEY_EXT_CTRL;
        bool value = _keyboard->isExtendedKey(static_cast<ZXKeysEnum>(key));

        EXPECT_EQ(refValue, value);
    }
}
