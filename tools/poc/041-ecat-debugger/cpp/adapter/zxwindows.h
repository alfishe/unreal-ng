// POC 041: which of a ZX model's four 16 KB windows its ports can remap, by the model's full name (the in-process
// adapter has the model, the WebAPI one /state/paging's "model"). The core keeps no such table: this one is read
// from the port decoders (core/src/emulator/ports/models) - SetRAMPageToBank1/2 outside a reset come only from
// Profi, ATM, the +2A/+3 special modes; TS-Conf, Sprinter and Next map every window through their own memory.

#pragma once

#include <string>

struct ZxPaging {
    bool switchable[4] = {false, false, false, false};
    // RAM 5 and 7 are the two screens of the 128K line
    bool spectrum_screens = true;
};

inline ZxPaging ZxModelPaging(const std::string &full_name)
{
    auto has = [&full_name](const char * s) { return full_name.find(s) != std::string::npos; };
    ZxPaging p;
    // A 48K maps nothing
    if (has("48k") || has("48K")) return p;
    if (has("+2A") || has("+3") || has("TS-Conf") || has("TS-Config") || has("ZX-Evo") || has("ATM") || has("Profi") ||
        has("Sprinter") || has("Next")) {
        for (bool &s : p.switchable) s = true;
        p.spectrum_screens = !(has("TS-Conf") || has("TS-Config") || has("Sprinter") || has("Next"));
        return p;
    }
    // The 128K line (128K, +2, Pentagon, Scorpion, KAY, Quorum, ...): the ROM by #7FFD / #1FFD, which may also put
    // RAM there (Scorpion, Pentagon 1024), and the top window by #7FFD
    p.switchable[0] = true;
    p.switchable[3] = true;
    return p;
}

inline std::string ZxRamNote(const ZxPaging &p, unsigned int page)
{
    if (!p.spectrum_screens) return std::string();
    if (page == 5) return "screen 0";
    //The second screen is a 128K's: a 48K has no paging and no page 7 on the screen
    if (page == 7 && (p.switchable[0] || p.switchable[3])) return "screen 1";
    return std::string();
}
