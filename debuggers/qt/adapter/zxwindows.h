// unreal-debugger: which of a ZX model's four 16 KB windows its ports can remap, by the model's full name (the in-process
// adapter has the model, the WebAPI one /state/paging's "model"). The core keeps no such table: this one is read
// from the port decoders (core/src/emulator/ports/models) - SetRAMPageToBank1/2 outside a reset come only from
// Profi, ATM, the +2A/+3 special modes; TS-Conf, Sprinter and Next map every window through their own memory.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

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

// A ROM page's title (ROM::GetROMPageTitle, /state/memory/rom) as a page list shows it: "Unknown ROM, <digest>" cut
// to "unknown ROM <8 digits>" (one unknown page told from another); blank pages and the " (= ROM page N)" of a copy
// stay
inline std::string ZxRomPageNote(const std::string &title)
{
    if (title.rfind("Unknown ROM", 0) != 0) return title;
    const size_t open = title.find('<');
    const size_t close = title.find('>', open);
    std::string note = "unknown ROM";
    if (open != std::string::npos && close != std::string::npos) note += ' ' + title.substr(open + 1, std::min<size_t>(8, close - open - 1));
    return close == std::string::npos ? note : note + title.substr(close + 1);
}

// The event breakpoint kinds (the core's names, gsdebughooks.h GSCardEventName) a machine's main CPU takes: the host's
// side of a sound card's mailbox when the machine has a card, then the events its model tells (the port decoders of
// the Next, TS-Conf and the Sprinter; every WD1793 and every Z80 the rest)
inline std::vector<std::string> ZxMainEventKinds(const std::string &full_name, bool card)
{
    auto has = [&full_name](const char * s) { return full_name.find(s) != std::string::npos; };
    std::vector<std::string> r;
    if (card) r = {"host_command", "host_data", "host_reply"};
    if (has("Next"))
        r.insert(r.end(), {"cpu_int", "cpu_reti", "cpu_nmi", "cpu_retn", "machine_reset", "divmmc_map", "divmmc_unmap",
                           "esxdos_call", "zxndma_start", "zxndma_done"});
    else if (has("TS-Conf") || has("TS-Config"))
        r.insert(r.end(), {"cpu_int", "tsdma_start", "tsdma_done"});
    else if (has("Sprinter"))
        r.insert(r.end(), {"cpu_int", "cpu_reti", "pld_read", "pld_write", "pld_event"});
    r.insert(r.end(), {"int_accept", "int_return", "fdc_command", "fdc_done"});
    return r;
}

// A sound card CPU's: the card's side of the mailbox and its hardware; the NeoGS's clock, DMA and GSCFG0 on a NeoGS
inline std::vector<std::string> ZxCardEventKinds(bool neogs)
{
    std::vector<std::string> r = {"int", "nmi", "reset", "page", "card_command", "card_data", "card_reply", "dac_fetch"};
    if (neogs) r.insert(r.end(), {"clock_change", "dma_start", "dma_done", "gscfg0"});
    return r;
}
