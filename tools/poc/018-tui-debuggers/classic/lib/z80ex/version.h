/* version.h - vendored z80ex release constants (poc018).
 * Upstream z80ex generates this header from CMake variables; the poc ships a
 * fixed 1.1.21 release copy so plain compiler and CMake builds agree. The
 * release/version macros stay unquoted: TOSTRING() in z80ex.c stringizes
 * them after macro expansion. */
#ifndef Z80EX_VERSION_H
#define Z80EX_VERSION_H

#define Z80EX_API_REVISION 1
#define Z80EX_VERSION_MAJOR 1
#define Z80EX_VERSION_MINOR 1
#define Z80EX_VERSION_REVISION 21
#define Z80EX_RELEASE_TYPE release
#define Z80EX_VERSION_STR 1.1.21

#endif /* Z80EX_VERSION_H */
