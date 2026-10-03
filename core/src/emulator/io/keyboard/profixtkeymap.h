#pragma once

/// @file profixtkeymap.h
/// @brief The PROFI-XT keyboard controller's key table, for the high-level
/// (table) engine of ProfiXtKbc: which Profi matrix positions each PC key
/// closes. Read off the controller's firmware ("JV KRAMIS 1.27") by running it
/// - the low-level engine - for every key (research-profi-keyboard.md section
/// 2 "Key map from the firmware"); a test checks the two agree key by key.
///
/// A matrix position is a half-row (0..7 = A8..A15) and a data line (0..5 =
/// KD0..KD5). KD5 is the 6th line of the v5 keyboard connector X9: on half-row
/// A14 (#BFFE) it is the extra key EXT that BIOS 2.0 reads (F1 = A + EXT); on
/// half-row A15 it is where Left Shift goes in the controller's second mode.
///
///   half-row  port   KD0   KD1  KD2 KD3 KD4  KD5
///   0  A8     #FEFE  CS    Z    X   C   V    -
///   1  A9     #FDFE  A     S    D   F   G    -
///   2  A10    #FBFE  Q     W    E   R   T    -
///   3  A11    #F7FE  1     2    3   4   5    -
///   4  A12    #EFFE  0     9    8   7   6    -
///   5  A13    #DFFE  P     O    I   U   Y    -
///   6  A14    #BFFE  ENT   L    K   J   H    EXT
///   7  A15    #7FFE  SP    SS   M   N   B    (Left Shift in mode 2)
///
/// Worked example: F1 closes A (half-row 1, KD0) and EXT (half-row 6, KD5),
/// so #FDFE reads 3Eh, #BFFE reads 1Fh, #00FE (all half-rows) reads 1Eh.

#include <cstdint>

#include "emulator/io/keyboard/pckey.h"

namespace profixt
{

/// 48 matrix positions as bits: bit (row * 6 + line)
using MatrixMask = uint64_t;

constexpr MatrixMask Position(int row, int line)
{
    return MatrixMask{1} << (row * 6 + line);
}

/// The controller's state that changes how a key translates
struct KeyContext
{
    bool shift = false;     ///< a Shift key is held when the key is made
    bool numLock = false;   ///< Num Lock was toggled: the keypad is the cursor / navigation block
    bool mode2 = false;     ///< Scroll Lock (or a read of #AAFE) switched the controller's second mode on
};

/// The positions `key` closes in `context` (0 for a key the controller ignores:
/// the lock keys, Pause, the Windows keys)
MatrixMask Translate(PcKey key, const KeyContext& context);

/// One half-row's six lines from a mask of closed positions (bit = 0: closed)
uint8_t RowBits(MatrixMask closed, int row);

/// A position by its name in the table ("CS", "SS", "ENT", "SP", "EXT", "X15",
/// "A".."Z", "0".."9"); 0 when unknown
MatrixMask PositionByName(const char* name);
}  // namespace profixt
