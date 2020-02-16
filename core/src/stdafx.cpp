#include "stdafx.h"

// Fake export for stdafx.o to get rid of MSVC linker error.
// LNK4221: This object file does not define any previously undefined public symbols,
// so it will not be used by any link operation that consumes this library
__declspec( dllexport ) void getRidOfLNK4221(){}
