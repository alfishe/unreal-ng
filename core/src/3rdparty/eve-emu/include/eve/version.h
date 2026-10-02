/* eve-emu - FT812 emulator library. Library version. */
#ifndef EVE_VERSION_H
#define EVE_VERSION_H

#define EVE_VERSION_MAJOR 0
#define EVE_VERSION_MINOR 1
#define EVE_VERSION_PATCH 0

/* (major << 16) | (minor << 8) | patch, as reported by EveGetBuildInfo(). */
#define EVE_VERSION ((EVE_VERSION_MAJOR << 16) | (EVE_VERSION_MINOR << 8) | EVE_VERSION_PATCH)

#endif
