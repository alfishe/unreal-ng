#include "zxkeydecoder.h"

namespace ZXKeyDecoder
{
const std::array<ZXKeysEnum, 40>& KeyOrder()
{
    // Main key table at #0205: "BHY65TGV" "NJU74RFC" "MKI83EDX" (SYM) "LO92WSZ"
    // SPACE ENTER "P01QA" (CAPS) - by name, not by enum value
    static const std::array<ZXKeysEnum, 40> order = {
        ZXKEY_B, ZXKEY_H, ZXKEY_Y, ZXKEY_6, ZXKEY_5, ZXKEY_T, ZXKEY_G, ZXKEY_V,
        ZXKEY_N, ZXKEY_J, ZXKEY_U, ZXKEY_7, ZXKEY_4, ZXKEY_R, ZXKEY_F, ZXKEY_C,
        ZXKEY_M, ZXKEY_K, ZXKEY_I, ZXKEY_8, ZXKEY_3, ZXKEY_E, ZXKEY_D, ZXKEY_X,
        ZXKEY_SYM_SHIFT, ZXKEY_L, ZXKEY_O, ZXKEY_9, ZXKEY_2, ZXKEY_W, ZXKEY_S, ZXKEY_Z,
        ZXKEY_SPACE, ZXKEY_ENTER, ZXKEY_P, ZXKEY_0, ZXKEY_1, ZXKEY_Q, ZXKEY_A, ZXKEY_CAPS_SHIFT,
    };
    return order;
}

namespace
{
int KeyNumber(ZXKeysEnum key)
{
    const auto& order = KeyOrder();
    for (size_t i = 0; i < order.size(); i++)
    {
        if (order[i] == key)
            return static_cast<int>(i);
    }
    return -1;
}

// K-LOOK-UP: HL = base, DE = main code
uint8_t LookUp(const uint8_t* rom, uint16_t base, uint8_t mainCode)
{
    return rom[(base + mainCode) & 0x3FFF];
}
} // anonymous namespace

std::optional<uint8_t> Decode(const uint8_t* rom, ZXKeysEnum key, Shift shift, const EditorState& state)
{
    const int number = KeyNumber(key);
    if (rom == nullptr || number < 0 || key == ZXKEY_CAPS_SHIFT)
        return std::nullopt;

    // K-TEST: SYMBOL SHIFT on its own is no key; with CAPS it is a key (#0E)
    const uint8_t b = static_cast<uint8_t>(shift);
    if (key == ZXKEY_SYM_SHIFT && shift != Shift::Caps)
        return std::nullopt;
    const uint8_t a = rom[0x0205 + number];  // K-MAIN

    // K-DECODE
    const uint8_t c = state.mode;
    if (a < 0x3A)
    {
        // K-DIGIT: digits, SPACE, ENTER, both shifts
        if (a < 0x30)
            return a;

        if (c == 0)
        {
            // K-KLC-DGT
            if (b == 0xFF)
                return a;
            if (b & 0x20)
                return LookUp(rom, 0x0230, a);
            const uint8_t s = static_cast<uint8_t>(a - 0x10);
            if (s == 0x22)
                return static_cast<uint8_t>(0x40);  // K-@-CHAR
            if (s == 0x20)
                return static_cast<uint8_t>(0x5F);  // '_'
            return s;
        }
        if (c == 1)
        {
            // E mode digits
            if ((b & 0x20) == 0)
                return LookUp(rom, 0x0254, a);  // SYMBOL SHIFT: table f
            if (a >= 0x38)
            {
                // K-8-&-9: BRIGHT / FLASH codes
                const uint8_t v = static_cast<uint8_t>(a - 0x36);
                return b == 0xFF ? v : static_cast<uint8_t>(v - 2);
            }
            const uint8_t v = static_cast<uint8_t>(a - 0x20);  // PAPER / INK colour codes
            return b == 0xFF ? v : static_cast<uint8_t>(v + 8);
        }
        // K-GRA-DGT
        if (a == 0x39 || a == 0x30)
            return LookUp(rom, 0x0230, a);
        const uint8_t v = static_cast<uint8_t>((a & 0x07) + 0x80);
        return b == 0xFF ? v : static_cast<uint8_t>(v ^ 0x0F);
    }

    // Letter keys
    if (c == 0)
    {
        // K-KLC-LET
        if ((b & 0x01) == 0)
            return LookUp(rom, 0x0229, a);  // SYMBOL SHIFT: table e
        if ((state.flags & 0x08) == 0)
            return static_cast<uint8_t>(a + 0xA5);  // K mode: K-TOKENS
        if (state.flags2 & 0x08)
            return a;  // CAPS LOCK
        if (b != 0xFF)
            return a;  // CAPS SHIFT
        return static_cast<uint8_t>(a + 0x20);
    }
    if (c == 1)
        return LookUp(rom, b == 0xFF ? 0x01EB : 0x0205, a);  // K-E-LET: table b or c
    return static_cast<uint8_t>(a + 0x4F);  // graphics mode letters
}

std::optional<Press> Find(const uint8_t* rom, uint8_t wanted, const EditorState& state)
{
    static const Shift shifts[] = { Shift::None, Shift::Caps, Shift::Symbol };

    auto search = [&](const EditorState& s, bool extended) -> std::optional<Press> {
        for (Shift shift : shifts)
        {
            for (ZXKeysEnum key : KeyOrder())
            {
                if (key == ZXKEY_CAPS_SHIFT || (key == ZXKEY_SYM_SHIFT && shift != Shift::Caps))
                    continue;
                const std::optional<uint8_t> code = Decode(rom, key, shift, s);
                if (!code || *code != wanted)
                    continue;

                Press press;
                press.extendedFirst = extended;
                press.code = wanted;
                if (shift == Shift::Caps)
                    press.keys.push_back(ZXKEY_CAPS_SHIFT);
                else if (shift == Shift::Symbol)
                    press.keys.push_back(ZXKEY_SYM_SHIFT);
                press.keys.push_back(key);
                return press;
            }
        }
        return std::nullopt;
    };

    if (std::optional<Press> press = search(state, false))
        return press;

    // Through E mode: CAPS + SYMBOL first, then the key in E mode
    if (state.mode != 1)
    {
        EditorState extended = state;
        extended.mode = 1;
        return search(extended, true);
    }
    return std::nullopt;
}
} // namespace ZXKeyDecoder
