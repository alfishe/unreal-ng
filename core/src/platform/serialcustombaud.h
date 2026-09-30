#pragma once

/// @file serialcustombaud.h
/// @brief A baud rate outside the termios constants on an open serial device
/// (the ZX-Evo's 345600, an AVR divisor, a 16550 divisor that is not a PC
/// rate). One implementation per POSIX OS: platform/macos (IOSSIOSPEED),
/// platform/linux (termios2, BOTHER). Used by hostserialport_posix.cpp after
/// the rest of the line is set.

#include <cstdint>

/// True when the device now runs at `baud`
bool SetCustomSerialBaud(int fd, uint32_t baud);
