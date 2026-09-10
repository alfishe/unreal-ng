#include "keyboard_test.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void Keyboard_Test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);
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

TEST_F(Keyboard_Test, HandlePortIn_SingleRow)
{
    // No keys pressed - all rows return 0xFF
    EXPECT_EQ(_keyboard->HandlePortIn(0xFEFE), 0xFF);  // Row 0: Caps...V
    EXPECT_EQ(_keyboard->HandlePortIn(0x7FFE), 0xFF);  // Row 7: Space...B

    // Press 'A' (row 1, bit 0)
    _keyboard->PressKey(ZXKEY_A);
    EXPECT_EQ(_keyboard->HandlePortIn(0xFDFE), 0xFE);  // Row 1: bit 0 cleared
    EXPECT_EQ(_keyboard->HandlePortIn(0xFEFE), 0xFF);  // Row 0: unaffected

    _keyboard->ReleaseKey(ZXKEY_A);
    EXPECT_EQ(_keyboard->HandlePortIn(0xFDFE), 0xFF);  // Row 1: restored
}

TEST_F(Keyboard_Test, HandlePortIn_MultiRowScan)
{
    // Press 'A' (row 1, bit 0) and 'Q' (row 2, bit 0)
    _keyboard->PressKey(ZXKEY_A);
    _keyboard->PressKey(ZXKEY_Q);

    // Single row scans
    EXPECT_EQ(_keyboard->HandlePortIn(0xFDFE), 0xFE);  // Row 1 only
    EXPECT_EQ(_keyboard->HandlePortIn(0xFBFE), 0xFE);  // Row 2 only

    // Multi-row scan: rows 1 and 2 (bits 1 and 2 cleared in high byte)
    // High byte 0xF9 = 0b1111'1001 = rows 1 and 2 selected
    // Result should be AND of both rows = 0xFE & 0xFE = 0xFE
    EXPECT_EQ(_keyboard->HandlePortIn(0xF9FE), 0xFE);

    // Press 'S' (row 1, bit 1) - now row 1 has bits 0 and 1 cleared
    _keyboard->PressKey(ZXKEY_S);
    EXPECT_EQ(_keyboard->HandlePortIn(0xFDFE), 0xFC);  // Row 1: bits 0,1 cleared
    EXPECT_EQ(_keyboard->HandlePortIn(0xFBFE), 0xFE);  // Row 2: only bit 0 cleared
    // Multi-row: 0xFC & 0xFE = 0xFC
    EXPECT_EQ(_keyboard->HandlePortIn(0xF9FE), 0xFC);

    _keyboard->Reset();
}

TEST_F(Keyboard_Test, HandlePortIn_AllRows)
{
    // Scan all rows (high byte 0x00 = all bits cleared)
    EXPECT_EQ(_keyboard->HandlePortIn(0x00FE), 0xFF);

    // Press Space (row 7)
    _keyboard->PressKey(ZXKEY_SPACE);
    EXPECT_EQ(_keyboard->HandlePortIn(0x00FE), 0xFE);  // Detects any key

    _keyboard->Reset();
}

TEST_F(Keyboard_Test, HandlePortIn_EvenPorts)
{
    // ULA responds to any even port, not just 0xFE
    _keyboard->PressKey(ZXKEY_ENTER);  // Row 6, bit 0

    // Various even ports with row 6 selected (bit 6 cleared = 0xBF)
    EXPECT_EQ(_keyboard->HandlePortIn(0xBFFE), 0xFE);  // Standard
    EXPECT_EQ(_keyboard->HandlePortIn(0xBF00), 0xFE);  // Even port 0x00
    EXPECT_EQ(_keyboard->HandlePortIn(0xBF02), 0xFE);  // Even port 0x02
    EXPECT_EQ(_keyboard->HandlePortIn(0xBFFC), 0xFE);  // Even port 0xFC

    _keyboard->Reset();
}

TEST_F(Keyboard_Test, HandlePortIn_Bits5to7Preserved)
{
    // Verify bits 5-7 are always 1 (bit 6 is EAR input, mixed externally)
    _keyboard->PressKey(ZXKEY_1);
    _keyboard->PressKey(ZXKEY_2);
    _keyboard->PressKey(ZXKEY_3);
    _keyboard->PressKey(ZXKEY_4);
    _keyboard->PressKey(ZXKEY_5);

    // All 5 keys in row 3 pressed - bits 0-4 should be 0, bits 5-7 should be 1
    uint8_t result = _keyboard->HandlePortIn(0xF7FE);
    EXPECT_EQ(result & 0b1110'0000, 0b1110'0000) << "Bits 5-7 must be 1";
    EXPECT_EQ(result & 0b0001'1111, 0b0000'0000) << "Bits 0-4 must be 0";

    _keyboard->Reset();
}

// Comprehensive tests for all keyboard polling variations used by real software
TEST_F(Keyboard_Test, HandlePortIn_AllSingleRowPorts)
{
    // Test all 8 standard single-row scan ports
    const uint16_t rowPorts[] = {
        0xFEFE,  // Row 0: Caps, Z, X, C, V
        0xFDFE,  // Row 1: A, S, D, F, G
        0xFBFE,  // Row 2: Q, W, E, R, T
        0xF7FE,  // Row 3: 1, 2, 3, 4, 5
        0xEFFE,  // Row 4: 0, 9, 8, 7, 6
        0xDFFE,  // Row 5: P, O, I, U, Y
        0xBFFE,  // Row 6: Enter, L, K, J, H
        0x7FFE   // Row 7: Space, Sym, M, N, B
    };

    // All rows should return 0xFF when no keys pressed
    for (int i = 0; i < 8; i++)
    {
        EXPECT_EQ(_keyboard->HandlePortIn(rowPorts[i]), 0xFF)
            << "Row " << i << " should be 0xFF with no keys";
    }
}

TEST_F(Keyboard_Test, HandlePortIn_WeakAddressDecoding)
{
    // ZX Spectrum ULA has weak address decoding - responds to any even port
    // Some software exploits this for faster polling
    _keyboard->PressKey(ZXKEY_SPACE);  // Row 7, bit 0

    // Standard port
    EXPECT_EQ(_keyboard->HandlePortIn(0x7FFE), 0xFE);

    // Alternative even ports with same row selection (A15=0)
    EXPECT_EQ(_keyboard->HandlePortIn(0x7F00), 0xFE);  // Port 0x00
    EXPECT_EQ(_keyboard->HandlePortIn(0x7F02), 0xFE);  // Port 0x02
    EXPECT_EQ(_keyboard->HandlePortIn(0x7F10), 0xFE);  // Port 0x10
    EXPECT_EQ(_keyboard->HandlePortIn(0x7FAA), 0xFE);  // Port 0xAA
    EXPECT_EQ(_keyboard->HandlePortIn(0x7FFC), 0xFE);  // Port 0xFC

    _keyboard->Reset();
}

TEST_F(Keyboard_Test, HandlePortIn_CombinedRowScans)
{
    // Test various multi-row scan combinations used by games
    _keyboard->PressKey(ZXKEY_Q);      // Row 2, bit 0
    _keyboard->PressKey(ZXKEY_SPACE);  // Row 7, bit 0

    // Scan rows 2 and 7 together (bits 2 and 7 cleared in high byte)
    // High byte: ~(0x04 | 0x80) = 0x7B
    EXPECT_EQ(_keyboard->HandlePortIn(0x7BFE), 0xFE);  // AND of both = 0xFE

    // Scan rows 0, 1, 2 (Caps+A+Q rows for cursor key detection)
    // High byte: ~(0x01 | 0x02 | 0x04) = 0xF8
    EXPECT_EQ(_keyboard->HandlePortIn(0xF8FE), 0xFE);  // Q pressed in row 2

    _keyboard->Reset();
}

TEST_F(Keyboard_Test, HandlePortIn_FullKeyboardScan)
{
    // IN A,($00FE) or IN A,($FFFE with all bits set) scans all rows at once
    // Common for "any key pressed" detection

    EXPECT_EQ(_keyboard->HandlePortIn(0x00FE), 0xFF);  // No keys

    _keyboard->PressKey(ZXKEY_M);
    EXPECT_EQ(_keyboard->HandlePortIn(0x00FE), 0xFB);  // M = row 7, bit 2

    _keyboard->PressKey(ZXKEY_A);
    // M (row 7, bit 2) AND A (row 1, bit 0) = 0xFB & 0xFE = 0xFA
    EXPECT_EQ(_keyboard->HandlePortIn(0x00FE), 0xFA);

    _keyboard->Reset();
}

TEST_F(Keyboard_Test, HandlePortIn_ShiftCombinations)
{
    // Common shift+key combinations (cursors, delete, etc.)

    // Cursor left = Caps Shift + 5
    _keyboard->PressKey(ZXKEY_CAPS_SHIFT);  // Row 0, bit 0
    _keyboard->PressKey(ZXKEY_5);           // Row 3, bit 4

    // Scan rows 0 and 3 together
    EXPECT_EQ(_keyboard->HandlePortIn(0xFEFE), 0xFE);  // Caps row: bit 0 clear
    EXPECT_EQ(_keyboard->HandlePortIn(0xF7FE), 0xEF);  // 5 row: bit 4 clear
    // Combined scan: 0xF6 = rows 0+3
    EXPECT_EQ(_keyboard->HandlePortIn(0xF6FE), 0xEE);  // 0xFE & 0xEF

    _keyboard->Reset();

    // Symbol shift combinations
    _keyboard->PressKey(ZXKEY_SYM_SHIFT);  // Row 7, bit 1
    _keyboard->PressKey(ZXKEY_P);          // Row 5, bit 0 (for ")

    EXPECT_EQ(_keyboard->HandlePortIn(0x7FFE), 0xFD);  // Sym row: bit 1 clear
    EXPECT_EQ(_keyboard->HandlePortIn(0xDFFE), 0xFE);  // P row: bit 0 clear

    _keyboard->Reset();
}

TEST_F(Keyboard_Test, HandlePortIn_GhostKeys)
{
    // Test that pressing multiple keys in different rows correctly ANDs
    // This is how ghost key detection works on real hardware

    // Press keys in multiple rows
    _keyboard->PressKey(ZXKEY_A);  // Row 1, bit 0
    _keyboard->PressKey(ZXKEY_B);  // Row 7, bit 4
    _keyboard->PressKey(ZXKEY_C);  // Row 0, bit 3

    // Individual row scans
    EXPECT_EQ(_keyboard->HandlePortIn(0xFDFE), 0xFE);  // Row 1: A
    EXPECT_EQ(_keyboard->HandlePortIn(0x7FFE), 0xEF);  // Row 7: B
    EXPECT_EQ(_keyboard->HandlePortIn(0xFEFE), 0xF7);  // Row 0: C

    // Combined scans should AND correctly
    // Rows 1+7: 0xFE & 0xEF = 0xEE
    EXPECT_EQ(_keyboard->HandlePortIn(0x7DFE), 0xEE);
    // Rows 0+1+7: 0xF7 & 0xFE & 0xEF = 0xE6
    EXPECT_EQ(_keyboard->HandlePortIn(0x7CFE), 0xE6);

    _keyboard->Reset();
}

TEST_F(Keyboard_Test, HandlePortIn_CloneCompatibility)
{
    // Some clones and games use non-standard port addresses
    // All should work as long as A0=0 (even port)

    _keyboard->PressKey(ZXKEY_ENTER);  // Row 6, bit 0

    // Pentagon/Scorpion style ports (still even)
    EXPECT_EQ(_keyboard->HandlePortIn(0xBF00), 0xFE);
    EXPECT_EQ(_keyboard->HandlePortIn(0xBF7E), 0xFE);

    // Timex style
    EXPECT_EQ(_keyboard->HandlePortIn(0xBFF6), 0xFE);

    _keyboard->Reset();
}
