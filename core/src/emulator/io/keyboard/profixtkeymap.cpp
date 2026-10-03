#include "emulator/io/keyboard/profixtkeymap.h"

#include <cstring>

namespace profixt
{

namespace
{
struct Name
{
    const char* name;
    int row;
    int line;
};

// clang-format off
constexpr Name kNames[] = {
    {"CS", 0, 0}, {"Z", 0, 1}, {"X", 0, 2}, {"C", 0, 3}, {"V", 0, 4},
    {"A", 1, 0}, {"S", 1, 1}, {"D", 1, 2}, {"F", 1, 3}, {"G", 1, 4},
    {"Q", 2, 0}, {"W", 2, 1}, {"E", 2, 2}, {"R", 2, 3}, {"T", 2, 4},
    {"1", 3, 0}, {"2", 3, 1}, {"3", 3, 2}, {"4", 3, 3}, {"5", 3, 4},
    {"0", 4, 0}, {"9", 4, 1}, {"8", 4, 2}, {"7", 4, 3}, {"6", 4, 4},
    {"P", 5, 0}, {"O", 5, 1}, {"I", 5, 2}, {"U", 5, 3}, {"Y", 5, 4},
    {"ENT", 6, 0}, {"L", 6, 1}, {"K", 6, 2}, {"J", 6, 3}, {"H", 6, 4}, {"EXT", 6, 5},
    {"SP", 7, 0}, {"SS", 7, 1}, {"M", 7, 2}, {"N", 7, 3}, {"B", 7, 4}, {"X15", 7, 5},
};
// clang-format on

/// One PC key: the positions it closes, as names separated by spaces. `shifted`
/// applies while a Shift is held, `numLock` after Num Lock, `mode2` in the second
/// mode; null = as `plain`
struct Entry
{
    PcKey key;
    const char* plain;
    const char* shifted;
    const char* numLock;
    const char* mode2;
};

// The firmware's table (JV KRAMIS 1.27), default mode first. Read off by running the firmware for every key
// (ProfiXtKbc firmware engine); ProfiXtKbc_Test.TheTableAgreesWithTheFirmware keeps the two in step
// clang-format off
constexpr Entry kTable[] = {
    {PcKey::Escape, "CS 1", nullptr, nullptr, nullptr},
    {PcKey::Digit1, "1", nullptr, nullptr, nullptr}, {PcKey::Digit2, "2", nullptr, nullptr, nullptr},
    {PcKey::Digit3, "3", nullptr, nullptr, nullptr}, {PcKey::Digit4, "4", nullptr, nullptr, nullptr},
    {PcKey::Digit5, "5", nullptr, nullptr, nullptr}, {PcKey::Digit6, "6", nullptr, nullptr, nullptr},
    {PcKey::Digit7, "7", nullptr, nullptr, nullptr}, {PcKey::Digit8, "8", nullptr, nullptr, nullptr},
    {PcKey::Digit9, "9", nullptr, nullptr, nullptr}, {PcKey::Digit0, "0", nullptr, nullptr, nullptr},
    {PcKey::Minus, "SS J", "SS 0", nullptr, nullptr},
    {PcKey::Equal, "SS L", "SS K", nullptr, nullptr},
    {PcKey::Backspace, "CS 0", nullptr, nullptr, nullptr},
    {PcKey::Tab, "CS I", nullptr, nullptr, nullptr},
    {PcKey::Q, "Q", nullptr, nullptr, nullptr}, {PcKey::W, "W", nullptr, nullptr, nullptr},
    {PcKey::E, "E", nullptr, nullptr, nullptr}, {PcKey::R, "R", nullptr, nullptr, nullptr},
    {PcKey::T, "T", nullptr, nullptr, nullptr}, {PcKey::Y, "Y", nullptr, nullptr, nullptr},
    {PcKey::U, "U", nullptr, nullptr, nullptr}, {PcKey::I, "I", nullptr, nullptr, nullptr},
    {PcKey::O, "O", nullptr, nullptr, nullptr}, {PcKey::P, "P", nullptr, nullptr, nullptr},
    {PcKey::LeftBracket, "SS Y", "SS F", nullptr, nullptr},
    {PcKey::RightBracket, "SS U", "SS G", nullptr, nullptr},
    {PcKey::Enter, "ENT", nullptr, nullptr, nullptr},
    {PcKey::LeftCtrl, "CS", nullptr, nullptr, nullptr},
    {PcKey::A, "A", nullptr, nullptr, nullptr}, {PcKey::S, "S", nullptr, nullptr, nullptr},
    {PcKey::D, "D", nullptr, nullptr, nullptr}, {PcKey::F, "F", nullptr, nullptr, nullptr},
    {PcKey::G, "G", nullptr, nullptr, nullptr}, {PcKey::H, "H", nullptr, nullptr, nullptr},
    {PcKey::J, "J", nullptr, nullptr, nullptr}, {PcKey::K, "K", nullptr, nullptr, nullptr},
    {PcKey::L, "L", nullptr, nullptr, nullptr},
    {PcKey::Semicolon, "SS O", "SS Z", nullptr, nullptr},
    {PcKey::Quote, "SS 7", "SS P", nullptr, nullptr},
    {PcKey::Backquote, "SS W", "SS A", nullptr, "SS X"},
    {PcKey::LeftShift, "SS", nullptr, nullptr, "X15"},
    {PcKey::Backslash, "SS D", "SS S", nullptr, nullptr},
    {PcKey::Z, "Z", nullptr, nullptr, nullptr}, {PcKey::X, "X", nullptr, nullptr, nullptr},
    {PcKey::C, "C", nullptr, nullptr, nullptr}, {PcKey::V, "V", nullptr, nullptr, nullptr},
    {PcKey::B, "B", nullptr, nullptr, nullptr}, {PcKey::N, "N", nullptr, nullptr, nullptr},
    {PcKey::M, "M", nullptr, nullptr, nullptr},
    {PcKey::Comma, "SS N", "SS R", nullptr, nullptr},
    {PcKey::Period, "SS M", "SS T", nullptr, nullptr},
    {PcKey::Slash, "SS V", "SS C", nullptr, nullptr},
    {PcKey::RightShift, "SS", nullptr, nullptr, "X15"},
    {PcKey::KeypadMultiply, "SS B", nullptr, nullptr, nullptr},
    {PcKey::LeftAlt, "SS ENT", nullptr, nullptr, nullptr},
    {PcKey::Space, "SP", nullptr, nullptr, nullptr},
    {PcKey::CapsLock, "CS SS", nullptr, nullptr, nullptr},
    {PcKey::Function1, "A EXT", nullptr, nullptr, nullptr}, {PcKey::Function2, "B EXT", nullptr, nullptr, nullptr},
    {PcKey::Function3, "C EXT", nullptr, nullptr, nullptr}, {PcKey::Function4, "D EXT", nullptr, nullptr, nullptr},
    {PcKey::Function5, "E EXT", nullptr, nullptr, nullptr}, {PcKey::Function6, "F EXT", nullptr, nullptr, nullptr},
    {PcKey::Function7, "G EXT", nullptr, nullptr, nullptr}, {PcKey::Function8, "H EXT", nullptr, nullptr, nullptr},
    {PcKey::Function9, "I EXT", nullptr, nullptr, nullptr}, {PcKey::Function10, "J EXT", nullptr, nullptr, nullptr},
    {PcKey::Keypad7, "7", nullptr, "K EXT", nullptr}, {PcKey::Keypad8, "8", nullptr, "CS 7", nullptr},
    {PcKey::Keypad9, "9", nullptr, "M EXT", nullptr}, {PcKey::KeypadMinus, "SS J", nullptr, nullptr, nullptr},
    {PcKey::Keypad4, "4", nullptr, "CS 5", nullptr}, {PcKey::Keypad5, "5", nullptr, "", nullptr},
    {PcKey::Keypad6, "6", nullptr, "CS 8", nullptr}, {PcKey::KeypadPlus, "SS K", nullptr, nullptr, nullptr},
    {PcKey::Keypad1, "1", nullptr, "L EXT", nullptr}, {PcKey::Keypad2, "2", nullptr, "CS 6", nullptr},
    {PcKey::Keypad3, "3", nullptr, "N EXT", nullptr}, {PcKey::Keypad0, "0", nullptr, "O EXT", nullptr},
    {PcKey::KeypadDecimal, "SS M", nullptr, "P EXT", nullptr},
    {PcKey::Function11, "SS Q", nullptr, nullptr, nullptr},
    {PcKey::Function12, "SS W", nullptr, nullptr, nullptr},
    // E0 keys
    {PcKey::KeypadEnter, "ENT", nullptr, nullptr, nullptr},
    {PcKey::RightCtrl, "CS", nullptr, nullptr, nullptr},
    {PcKey::KeypadDivide, "SS V", "SS C", nullptr, nullptr},
    {PcKey::RightAlt, "SS SP", nullptr, nullptr, nullptr},
    {PcKey::Home, "K EXT", nullptr, nullptr, nullptr}, {PcKey::Up, "CS 7", nullptr, nullptr, nullptr},
    {PcKey::PageUp, "M EXT", nullptr, nullptr, nullptr}, {PcKey::Left, "CS 5", nullptr, nullptr, nullptr},
    {PcKey::Right, "CS 8", nullptr, nullptr, nullptr}, {PcKey::End, "L EXT", nullptr, nullptr, nullptr},
    {PcKey::Down, "CS 6", nullptr, nullptr, nullptr}, {PcKey::PageDown, "N EXT", nullptr, nullptr, nullptr},
    {PcKey::Insert, "O EXT", nullptr, nullptr, nullptr}, {PcKey::Delete, "P EXT", nullptr, nullptr, nullptr},
    // The Windows keys (E0 5B / 5C / 5D; the 1992 keyboards had none): the firmware closes H, 0 and 4 for them,
    // as running it shows. Kept as it reads
    {PcKey::LeftGui, "H", nullptr, nullptr, nullptr}, {PcKey::RightGui, "0", nullptr, nullptr, nullptr},
    {PcKey::Menu, "4", nullptr, nullptr, nullptr},
};
// clang-format on

MatrixMask Parse(const char* spec)
{
    MatrixMask mask = 0;
    char token[8];
    while (spec && *spec)
    {
        while (*spec == ' ')
            ++spec;
        size_t n = 0;
        while (spec[n] && spec[n] != ' ' && n < sizeof(token) - 1)
        {
            token[n] = spec[n];
            ++n;
        }
        token[n] = '\0';
        spec += n;
        if (n)
            mask |= PositionByName(token);
    }
    return mask;
}

const Entry* Find(PcKey key)
{
    for (const Entry& entry : kTable)
    {
        if (entry.key == key)
            return &entry;
    }
    return nullptr;
}
}  // namespace

MatrixMask PositionByName(const char* name)
{
    for (const Name& entry : kNames)
    {
        if (std::strcmp(entry.name, name) == 0)
            return Position(entry.row, entry.line);
    }
    return 0;
}

MatrixMask Translate(PcKey key, const KeyContext& context)
{
    const Entry* entry = Find(key);
    if (!entry)
        return 0;
    // The keypad's second meaning wins over the shifted one; mode 2 changes ` and Left Shift only
    if (context.numLock && entry->numLock)
        return Parse(entry->numLock);
    if (context.mode2 && entry->mode2)
        return Parse(entry->mode2);
    if (context.shift && entry->shifted)
        return Parse(entry->shifted);
    return Parse(entry->plain);
}

uint8_t RowBits(MatrixMask closed, int row)
{
    return static_cast<uint8_t>(~(closed >> (row * 6)) & 0x3F);
}

}  // namespace profixt
