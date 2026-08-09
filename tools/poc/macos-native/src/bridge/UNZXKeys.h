// UNZXKeys.h - ZX Spectrum key codes exposed to Swift.
//
// Plain C / Objective-C only: this header is reachable from the Swift bridging
// header, so it must never pull in C++.
//
// The values mirror ZXKeysEnum from core/src/emulator/io/keyboard/keyboard.h.
// UNEmulatorBridge.mm contains static_asserts that keep the two in sync - if the
// core enum ever changes, the bridge fails to compile instead of silently
// sending the wrong key.

#pragma once

#import <Foundation/Foundation.h>

typedef NS_ENUM(uint8_t, UNZXKey) {
    UNZXKeyNone       = 0x00,

    UNZXKeyCapsShift  = 0x04,
    UNZXKeySymShift   = 0x05,
    UNZXKeyEnter      = 0x0A,
    UNZXKeySpace      = 0x20,

    UNZXKeyDigit0     = 0x30,
    UNZXKeyDigit1     = 0x31,
    UNZXKeyDigit2     = 0x32,
    UNZXKeyDigit3     = 0x33,
    UNZXKeyDigit4     = 0x34,
    UNZXKeyDigit5     = 0x35,
    UNZXKeyDigit6     = 0x36,
    UNZXKeyDigit7     = 0x37,
    UNZXKeyDigit8     = 0x38,
    UNZXKeyDigit9     = 0x39,

    UNZXKeyLetterA          = 0x41,
    UNZXKeyLetterB          = 0x42,
    UNZXKeyLetterC          = 0x43,
    UNZXKeyLetterD          = 0x44,
    UNZXKeyLetterE          = 0x45,
    UNZXKeyLetterF          = 0x46,
    UNZXKeyLetterG          = 0x47,
    UNZXKeyLetterI          = 0x48,   // NOTE: core really does order I before H here
    UNZXKeyLetterH          = 0x49,
    UNZXKeyLetterJ          = 0x4A,
    UNZXKeyLetterK          = 0x4B,
    UNZXKeyLetterL          = 0x4C,
    UNZXKeyLetterM          = 0x4D,
    UNZXKeyLetterN          = 0x4E,
    UNZXKeyLetterO          = 0x4F,
    UNZXKeyLetterP          = 0x50,
    UNZXKeyLetterQ          = 0x51,
    UNZXKeyLetterR          = 0x52,
    UNZXKeyLetterS          = 0x53,
    UNZXKeyLetterT          = 0x54,
    UNZXKeyLetterU          = 0x55,
    UNZXKeyLetterV          = 0x56,
    UNZXKeyLetterW          = 0x57,
    UNZXKeyLetterX          = 0x58,
    UNZXKeyLetterY          = 0x59,
    UNZXKeyLetterZ          = 0x5A,

    // Extended (composite) keys - the core expands these into modifier + key
    UNZXKeyExtCtrl    = 0x80,
    UNZXKeyExtUp      = 0x81,
    UNZXKeyExtDown    = 0x82,
    UNZXKeyExtLeft    = 0x83,
    UNZXKeyExtRight   = 0x84,
    UNZXKeyExtDelete  = 0x85,
    UNZXKeyExtBreak   = 0x86,
    UNZXKeyExtEdit    = 0x87,
    UNZXKeyExtDot     = 0x88,
    UNZXKeyExtComma   = 0x89,
    UNZXKeyExtPlus    = 0x8A,
    UNZXKeyExtMinus   = 0x8B,
    UNZXKeyExtMul     = 0x8C,
    UNZXKeyExtDiv     = 0x8D,
    UNZXKeyExtEqual   = 0x8E,
    UNZXKeyExtBar     = 0x8F,
    UNZXKeyExtBkSlash = 0x90,
    UNZXKeyExtCapsLock= 0x91,
    UNZXKeyExtDblQuote= 0x92,
};
