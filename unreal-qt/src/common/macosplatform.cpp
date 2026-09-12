#include "macosplatform.h"

// Non-macOS platforms: nothing to do (the .mm implementation is compiled on macOS only)
#ifndef __APPLE__
void MacOSPlatform::disableAutomaticFullScreenMenuItem() {}
#endif
