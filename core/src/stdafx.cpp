#include "stdafx.h"

// Fake export for stdafx.o to get rid of MSVC linker error.
// LNK4221: This object file does not define any previously undefined public symbols,
// so it will not be used by any link operation that consumes this library
#if defined _WIN32 || defined __CYGWIN__ || defined __MINGW32__
#ifdef __cplusplus
extern "C" {
#endif

__declspec( dllexport ) void getRidOfLNK4221(){}

#ifdef __cplusplus
}
#endif

#endif