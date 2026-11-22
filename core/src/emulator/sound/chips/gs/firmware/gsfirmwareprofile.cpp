#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "common/filehelper.h"
#include "common/logger.h"
#include "common/signaturecache.h"
#include "emulator/emulator.h"
#include "emulator/state/statenodejson.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace
{

using K = GSParamKind;
using T = GSTransferKind;

constexpr uint8_t kVar = GSCommandInfo::kVariablePost;
constexpr size_t kPageSize = 0x4000; // a card ROM page (cardRomPage)

GSCommandInfo Cmd(const char* name, uint8_t pre, uint8_t post, std::initializer_list<GSParamInfo> params, uint8_t reply = 0,
                  const char* replyMeaning = nullptr, T transfer = T::None, bool documented = false, const char* note = nullptr)
{
    GSCommandInfo info;
    info.name = name;
    info.pre = pre;
    info.post = post;
    size_t i = 0;
    for (const GSParamInfo& param : params)
        if (i < GSCommandInfo::kMaxParams)
            info.params[i++] = param;
    info.reply = reply;
    info.replyMeaning = replyMeaning;
    info.transfer = transfer;
    info.documented = documented;
    info.note = note;
    return info;
}

/// Text built for a table (the channel commands carry their channel) lives as long as the program
const char* Keep(std::string text)
{
    static std::mutex mutex;
    static std::deque<std::string> kept;
    std::lock_guard<std::mutex> lock(mutex);
    kept.push_back(std::move(text));
    return kept.back().c_str();
}

/// The command set of v1.04 (COM_L.a80, COM_H.a80, TABLES_H.a80 COMTABH)
std::array<GSCommandInfo, 256> Commands104()
{
    std::array<GSCommandInfo, 256> c{};

    // #00-#1F: COMTAB, COM_L.a80. The low-level commands the guide marks (*) except #00 and #0E
    c[0x00] = Cmd("Reset flags", 0, 0, {}, 0, nullptr, T::None, true,
                  "reads the data register on entry: clears the data bit and the command bit");
    c[0x01] = Cmd("Initialize DACs (silence)", 0, 0, {}, 0, nullptr, T::None, false, "every DAC to #80");
    c[0x02] = Cmd("Initialize volumes (maximum)", 0, 0, {}, 0, nullptr, T::None, false,
                  "every volume port to #3F (the guide swaps #02 and #03)");
    c[0x03] = Cmd("Reset volumes (zero)", 0, 0, {}, 0, nullptr, T::None, false, "every volume port to 0");
    c[0x04] = Cmd("Set current channel", 1, 0, {{"channel", K::Channel}}, 0, nullptr, T::None, false, "E = value & 7");
    c[0x05] = Cmd("Set current channel volume", 1, 0, {{"volume", K::Volume}}, 0, nullptr, T::None, false,
                  "only when the current channel is 0..3");
    c[0x06] = Cmd("Output byte to current channel DAC", 1, 0, {{"byte", K::Byte}});
    c[0x07] = Cmd("Output byte to current channel DAC with volume", 1, 1, {{"byte", K::Byte}, {"volume", K::Volume}});
    c[0x09] = Cmd("Set channel volume", 1, 0, {{"channel and volume", K::Byte}}, 0, nullptr, T::None, false,
                  "bits 7-6 channel, 5-0 volume");
    c[0x0A] = Cmd("Output byte to channel DAC", 1, 1, {{"byte", K::Byte}, {"channel", K::Channel}});
    c[0x0B] = Cmd("Output byte to channel DAC with volume", 1, 1, {{"byte", K::Byte}, {"channel and volume", K::Byte}},
                  0, nullptr, T::None, false, "second byte: bits 7-6 channel, 5-0 volume");
    c[0x0C] = Cmd("SounDrive Covox: four DAC bytes", 1, 3,
                  {{"channel 0", K::Byte}, {"channel 1", K::Byte}, {"channel 2", K::Byte}, {"channel 3", K::Byte}});
    c[0x0D] = Cmd("Ultravox: DAC bytes to channels", 1, kVar, {{"channels", K::ChannelMask}}, 0, nullptr, T::None,
                  false, "then one byte per set bit of mask & #0F, channel 0 first");
    c[0x0E] = Cmd("LPT Covox mode", 0, 0, {}, 0, nullptr, T::Stream, true,
                  "every data byte goes to DACs 0 and 2 until a command is written");
    c[0x0F] = Cmd("Profi Covox mode", 1, 0, {{"key", K::Mode}}, 0, nullptr, T::Stream, false,
                  "key must be 'Y' (#59); data to DAC 0, command register to DAC 2; ends with data #4E, commands #0F, #AA");
    c[0x10] = Cmd("Output to card port", 1, 1, {{"port", K::Port}, {"byte", K::Byte}});
    c[0x11] = Cmd("Input from card port", 1, 0, {{"port", K::Port}}, 1, "the port's value");
    c[0x12] = Cmd("Output to page register", 1, 0, {{"page", K::Byte}});
    c[0x13] = Cmd("Jump to address", 1, 1, {{"address", K::Low}, {"address", K::High}});
    c[0x14] = Cmd("Receive block", 1, 3, {{"length", K::Low}, {"length", K::High}, {"address", K::Low}, {"address", K::High}},
                  0, nullptr, T::BlockUpload, false, "then the block's bytes; a command ends it early");
    c[0x15] = Cmd("Transmit block", 1, 3, {{"length", K::Low}, {"length", K::High}, {"address", K::Low}, {"address", K::High}},
                  kVar, "the block's bytes", T::BlockDownload);
    c[0x16] = Cmd("Put byte", 1, 2, {{"byte", K::Byte}, {"address", K::Low}, {"address", K::High}});
    c[0x17] = Cmd("Get byte", 1, 1, {{"address", K::Low}, {"address", K::High}}, 1, "the byte");
    c[0x18] = Cmd("Set memory pointer", 1, 1, {{"pointer", K::Low}, {"pointer", K::High}}, 0, nullptr, T::None, false,
                  "the card's DE");
    c[0x19] = Cmd("Put byte at pointer", 1, 0, {{"byte", K::Byte}});
    c[0x1A] = Cmd("Get byte at pointer", 0, 0, {}, 1, "the byte");
    c[0x1B] = Cmd("Increment pointer", 0, 0, {});
    c[0x1C] = Cmd("Put byte to #20xx", 1, 1, {{"address", K::Low}, {"byte", K::Byte}}, 0, nullptr, T::None, false,
                  "#2000-#20FF is ROM: no effect");
    c[0x1D] = Cmd("Get byte from #20xx", 1, 0, {{"address", K::Low}}, 1, "the byte");

    // #20-#EF: COMTABH, COM_H.a80. A handler that sets a value replies the old one first
    c[0x20] = Cmd("Get total RAM", 0, 0, {}, 3, "bytes, low, middle, high", T::None, true);
    c[0x21] = Cmd("Get free RAM", 0, 0, {}, 3, "bytes, low, middle, high", T::None, true, "clears the error code");
    c[0x22] = Cmd("Get RAM page number", 1, 0, {{"index", K::Byte}}, 1, "RAMPG[index]: the page register value",
                  T::None, false, "the guide calls it Get GS Variable");
    c[0x23] = Cmd("Get number of RAM pages", 0, 0, {}, 1, "pages", T::None, true);
    c[0x2A] = Cmd("Set module master volume", 1, 0, {{"volume", K::Volume}}, 1, "the old volume", T::None, true);
    c[0x2B] = Cmd("Set FX master volume", 1, 0, {{"volume", K::Volume}}, 1, "the old volume", T::None, true);
    c[0x2C] = Cmd("Set current module", 1, 0, {{"module", K::Handle}}, 1, "the old current module", T::None, false,
                  "0: the last loaded; past the count: none");
    c[0x2D] = Cmd("Set current sample", 1, 0, {{"sample", K::Handle}}, 1, "the old current sample", T::None, false,
                  "a module's sample (CURSMP); 0: the last");
    c[0x2E] = Cmd("Set current FX", 1, 0, {{"fx", K::Handle}}, 1, "the old current FX", T::None, true,
                  "0: the last loaded");
    c[0x2F] = Cmd("Set current track", 1, 1, {{"track", K::Low}, {"track", K::High}}, 0, nullptr, T::None, false,
                  "0: the last; past the count: none");
    c[0x30] = Cmd("Load module", 0, 0, {}, 1, "the module handle", T::ModuleUpload, true,
                  "a second load while a module is loaded reinitialises the firmware (INITVAR)");
    c[0x31] = Cmd("Play module", 1, 0, {{"module", K::Handle}}, 1, "the module played (0: none)", T::None, true);
    c[0x32] = Cmd("Stop module", 0, 0, {}, 1, "the module", T::None, true);
    c[0x33] = Cmd("Continue module", 0, 0, {}, 1, "the module", T::None, true);
    c[0x34] = Cmd("Set module fade", 1, 0, {{"fade", K::Byte}}, 1, "the old fade");
    c[0x35] = Cmd("Set module volume", 1, 0, {{"volume", K::Volume}}, 1, "the old volume", T::None, true);
    c[0x36] = Cmd("Data on", 0, 0, {}, 1, "#FF");
    c[0x37] = Cmd("Reinitialise", 0, 0, {}, 0, nullptr, T::None, false, "forgets the module and the loaded data");
    c[0x38] = Cmd("Load FX", 0, 0, {}, 1, "the FX handle (0: no room, 60 at most)", T::SampleUpload, true,
                  "unsigned (PC) samples");
    c[0x39] = Cmd("Play FX", 1, 0, {{"fx", K::Handle}}, 1, "0, #FF for a bad handle", T::None, true);
    c[0x3A] = Cmd("Stop FX in channels", 1, 0, {{"channels", K::ChannelMask}}, 0, nullptr, T::None, true,
                  "bit 7 is channel 0 (the guide's note)");
    c[0x3B] = Cmd("Set FX fade", 1, 0, {{"fade", K::Byte}}, 1, "the old fade", T::None, false, "the same handler as #3C");
    c[0x3C] = Cmd("Set FX fade", 1, 0, {{"fade", K::Byte}}, 1, "the old fade");
    c[0x3D] = Cmd("Set FX volume", 1, 0, {{"volume", K::Volume}}, 1, "the old volume", T::None, true);
    c[0x3E] = Cmd("Load FX (extended)", 1, 0, {{"format", K::Mode}}, 1, "the FX handle", T::SampleUpload, true,
                  "#01: as #38 (unsigned); #00: signed (Amiga), #80 added to every byte; other: no load, reply 0");
    c[0x3F] = Cmd("Set FX note", 1, 0, {{"note", K::Note}}, 0, nullptr, T::None, false, "the same handler as #40");
    c[0x40] = Cmd("Set FX note", 1, 0, {{"note", K::Note}}, 0, nullptr, T::None, true, "of the current FX");
    c[0x41] = Cmd("Set FX sample volume", 1, 0, {{"volume", K::Volume}}, 0, nullptr, T::None, true, "of the current FX");
    c[0x42] = Cmd("Set FX finetune", 1, 0, {{"finetune", K::Byte}}, 1, "the old finetune", T::None, true);
    c[0x45] = Cmd("Set FX priority", 1, 0, {{"priority", K::Byte}}, 1, "the old priority", T::None, true);
    c[0x46] = Cmd("Set FX seek first", 1, 0, {{"channels", K::ChannelMask}}, 1, "the old mask", T::None, true);
    c[0x47] = Cmd("Set FX seek last", 1, 0, {{"channels", K::ChannelMask}}, 1, "the old mask", T::None, true);
    c[0x48] = Cmd("Set FX loop begin", 1, 2, {{"offset", K::Low}, {"offset", K::Middle}, {"offset", K::High}}, 0, nullptr,
                  T::None, false, "high #FF: no loop");
    c[0x49] = Cmd("Set FX loop end", 1, 2, {{"offset", K::Low}, {"offset", K::Middle}, {"offset", K::High}});
    c[0x50] = Cmd("Set note in FX channels", 1, 1, {{"channels", K::ChannelMask}, {"note", K::Note}}, 0, nullptr,
                  T::None, false, "mask 0: the last channel used");
    c[0x60] = Cmd("Get song position", 0, 0, {}, 1, "song position", T::None, true);
    c[0x61] = Cmd("Get pattern position", 0, 0, {}, 1, "row", T::None, true);
    c[0x62] = Cmd("Get mixed position", 0, 0, {}, 1, "bits 7-6 song position, 5-0 row", T::None, true);
    c[0x63] = Cmd("Get channel notes", 0, 0, {}, 4, "a note per channel; bit 7 clear: changed; 127: silent", T::None, true);
    c[0x64] = Cmd("Get channel volumes", 0, 0, {}, 4, "a volume per channel", T::None, true);
    c[0x65] = Cmd("Jump to position", 1, 0, {{"position", K::Byte}}, 1, "the current module");
    c[0x66] = Cmd("Set speed or tempo", 1, 0, {{"value", K::Byte}}, 0, nullptr, T::None, false,
                  "#01-#1F speed, #20-#FF tempo");
    c[0x67] = Cmd("Get speed", 0, 0, {}, 1, "speed");
    c[0x68] = Cmd("Get tempo", 0, 0, {}, 1, "tempo");
    c[0x69] = Cmd("Process sound", 0, 0, {}, 0, nullptr, T::None, false, "one tick of the player");

    // #80-#9F: COM80, the channel in bits 2-0, a note when bit 3 is set, a volume when bit 4 is set
    for (int code = 0x80; code <= 0x9F; code++)
    {
        const bool note = (code & 0x08) != 0;
        const bool volume = (code & 0x10) != 0;
        const char* what = note && volume ? "Play FX with note and volume in channel"
                           : note         ? "Play FX with note in channel"
                           : volume       ? "Play FX with volume in channel"
                                          : "Play FX in channel";
        GSCommandInfo info = Cmd(Keep(std::string(what) + " " + std::to_string(code & 7)), 1, uint8_t(note + volume), {{"fx", K::Handle}}, 0, nullptr,
                                 T::None, (code & 7) < 4, "#FF in the output register for a bad handle");
        size_t i = 1;
        if (note)
            info.params[i++] = {"note", K::Note};
        if (volume)
            info.params[i++] = {"volume", K::Volume};
        c[code] = info;
    }
    // #A0-#AF: COMA0, the channel in bits 2-0, bit 3 volume instead of note
    for (int code = 0xA0; code <= 0xAF; code++)
    {
        const bool volume = (code & 0x08) != 0;
        c[code] = Cmd(Keep(std::string(volume ? "Change volume in channel " : "Change note in channel ") + std::to_string(code & 7)), 1, 0,
                      {volume ? GSParamInfo{"volume", K::Volume} : GSParamInfo{"note", K::Note}});
    }

    // #F0-#FF: COMTAB's tail, COM_L.a80
    c[0xF0] = Cmd("Get error code", 0, 0, {}, 1, "#10: no free space, #11: no free entries");
    c[0xF3] = Cmd("Warm restart", 0, 0, {}, 0, nullptr, T::None, true, "skips the memory test");
    c[0xF4] = Cmd("Cold restart", 0, 0, {}, 1, "pages, 0 until the memory test ends", T::None, true,
                  "JP #0000; INIT writes 0, then the number of RAM pages");
    c[0xF4].handshake.valuePoll = true;
    c[0xF5] = Cmd("Busy on", 0, 0, {}, 0, nullptr, T::None, true, "the player stops until busy off");
    c[0xF6] = Cmd("Busy off", 0, 0, {}, 0, nullptr, T::None, true);
    c[0xF7] = Cmd("Get HX", 0, 0, {}, 1, "HX: bit 7 busy handling");
    c[0xFA] = Cmd("Test mode", 0, 0, {}, 0, nullptr, T::None, true, "the card runs its test commands (TCOM)");

    // COM_H.a80: the handlers that OUT (OUTRG) the reply, then IN A,(DATRG) before OUT (RSCOM)
    for (uint8_t code : {0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x30, 0x32, 0x33, 0x34, 0x35, 0x38, 0x3B, 0x3C, 0x3D, 0x3E, 0x42,
                         0x45, 0x46, 0x47})
        c[code].replyUnflagged = true;
    return c;
}

/// v1.05a / v1.05b: the patch adds #6A and #6B (patch/patch.a80, Patch2 / Patch5)
std::array<GSCommandInfo, 256> Commands105()
{
    std::array<GSCommandInfo, 256> c = Commands104();
    c[0x6A] = Cmd("Set player mode", 1, 0, {{"mode", K::Mode}}, 1, "the old mode", T::None, true,
                  "#01: ignore the module's stop (F00); kept over #F3");
    c[0x6A].replyUnflagged = true;  // Patch2: OUT (OUTRG) before IN A,(DATRG)
    c[0x6B] = Cmd("Set minimum loop length", 1, kVar, {{"words", K::Low}, {"words", K::High}}, 0, nullptr, T::None, true,
                  "the high byte is optional: a command instead sets 512 words; 0: relooper off; at most 16384");
    return c;
}

const std::array<GSCommandInfo, 256>& Table104()
{
    static const std::array<GSCommandInfo, 256> table = Commands104();
    return table;
}

const std::array<GSCommandInfo, 256>& Table105()
{
    static const std::array<GSCommandInfo, 256> table = Commands105();
    return table;
}

// The variables the commands read and set (__MAIN.a80; the same addresses in every version)
constexpr GSVariableInfo kVariables104[] = {
    {"NUMPG", 0x4080, 1, "RAM pages", false},
    {"CPAGE", 0x4081, 1, "the page mapped at #8000", false},
    {"PROCESS", 0x4084, 1, "#FF: the player runs", false},
    {"BUSY", 0x4086, 1, "#FF: busy on, the player waits", false},
    {"INGEN", 0x4087, 1, "#FF: inside the sound generator", false},
    {"CNTMOD", 0x4090, 1, "modules loaded", false},
    {"CNTSMP", 0x4091, 1, "module samples", false},
    {"CNTFX", 0x4092, 1, "FX loaded", false},
    {"CNTTRK", 0x4093, 2, "tracks", false},
    {"CURMOD", 0x4096, 1, "the current module", false},
    {"CURSMP", 0x4097, 1, "the current module sample", false},
    {"CURFX", 0x4098, 1, "the current FX", false},
    {"CURTRK", 0x4099, 2, "the current track", false},
    {"MODULE", 0x409E, 1, "the module playing", false},
    {"GSCHNS", 0x409F, 1, "the channels that exist", false},
    {"FXCHNS", 0x40A0, 1, "the channels playing FX", false},
    {"MODVOL", 0x40A4, 1, "module master volume", false},
    {"FXVOL", 0x40A5, 1, "FX master volume", false},
    {"FXMVOL", 0x40A6, 1, "FX volume", false},
    {"MODFADE", 0x40A7, 1, "module fade", false},
    {"FXFADE", 0x40A8, 1, "FX fade", false},
    {"ERRCODE", 0x40B2, 1, "the last error (#10, #11)", false},
    {"LSTCHN", 0x40B8, 1, "the last FX channel used", false},
    {"RAMBOT", 0x40D0, 3, "RAM bottom", false},
    {"MEMBOT", 0x40D9, 3, "free memory bottom", false},
    {"MEMTOP", 0x40E2, 3, "free memory top", false},
    {"RAMTOP", 0x40FD, 3, "RAM top", false},
    {"MTSTAT", 0x4151, 1, "player status: bit 7 stopped", false},
    {"MTBPM", 0x4157, 1, "tempo", false},
    {"MTSPEED", 0x4158, 1, "speed", false},
    {"MTPATPS", 0x415A, 1, "pattern position (row)", false},
    {"MTSNGPS", 0x415B, 1, "song position", false},
    {"MTVOL", 0x4165, 1, "module volume", false},
    {"CURADR", 0x4198, 3, "the load address", false},
    {"MODTP", 0x419F, 1, "module type", false},
};

constexpr GSVariableInfo kVariables105[] = {
    {"NUMPG", 0x4080, 1, "RAM pages", false},
    {"CPAGE", 0x4081, 1, "the page mapped at #8000", false},
    {"PROCESS", 0x4084, 1, "#FF: the player runs", false},
    {"BUSY", 0x4086, 1, "#FF: busy on, the player waits", false},
    {"INGEN", 0x4087, 1, "#FF: inside the sound generator", false},
    {"CNTMOD", 0x4090, 1, "modules loaded", false},
    {"CNTSMP", 0x4091, 1, "module samples", false},
    {"CNTFX", 0x4092, 1, "FX loaded", false},
    {"CNTTRK", 0x4093, 2, "tracks", false},
    {"CURMOD", 0x4096, 1, "the current module", false},
    {"CURSMP", 0x4097, 1, "the current module sample", false},
    {"CURFX", 0x4098, 1, "the current FX", false},
    {"CURTRK", 0x4099, 2, "the current track", false},
    {"MODULE", 0x409E, 1, "the module playing", false},
    {"GSCHNS", 0x409F, 1, "the channels that exist", false},
    {"FXCHNS", 0x40A0, 1, "the channels playing FX", false},
    {"MODVOL", 0x40A4, 1, "module master volume", false},
    {"FXVOL", 0x40A5, 1, "FX master volume", false},
    {"FXMVOL", 0x40A6, 1, "FX volume", false},
    {"MODFADE", 0x40A7, 1, "module fade", false},
    {"FXFADE", 0x40A8, 1, "FX fade", false},
    {"ERRCODE", 0x40B2, 1, "the last error (#10, #11)", false},
    {"LSTCHN", 0x40B8, 1, "the last FX channel used", false},
    {"RAMBOT", 0x40D0, 3, "RAM bottom", false},
    {"MEMBOT", 0x40D9, 3, "free memory bottom", false},
    {"MEMTOP", 0x40E2, 3, "free memory top", false},
    {"RAMTOP", 0x40FD, 3, "RAM top", false},
    {"MTSTAT", 0x4151, 1, "player status: bit 7 stopped", false},
    {"MTBPM", 0x4157, 1, "tempo", false},
    {"MTSPEED", 0x4158, 1, "speed", false},
    {"MTPATPS", 0x415A, 1, "pattern position (row)", false},
    {"MTSNGPS", 0x415B, 1, "song position", false},
    {"MTVOL", 0x4165, 1, "module volume", false},
    {"CURADR", 0x4198, 3, "the load address", false},
    {"MODTP", 0x419F, 1, "module type", false},
    {"PlMode", 0x41A0, 1, "player mode (#6A): #01 ignores the module's stop", true},
    {"MODLLEN", 0x41A1, 2, "relooper minimum loop length in words (#6B)", true},
};

// LOAD (LOAD_L.a80): during an upload a command is taken as a stream command; #F3 / #F4 restart, any other is
// acknowledged and the upload goes on
constexpr GSStreamCommandInfo kStreamCommands[] = {
    {0xD1, "Open stream"},
    {0xD2, "Close stream"},
};

/// region <Profile files>

/// A file profile's command table, kept for the process as the built-in tables are
std::array<GSCommandInfo, 256>* KeepTable()
{
    static std::mutex mutex;
    static std::deque<std::array<GSCommandInfo, 256>> kept;
    std::lock_guard<std::mutex> lock(mutex);
    return &kept.emplace_back();
}

/// The names of a symbol file: `[ROMn:|RAMn:]ADDR NAME ...` lines, `;` comments
struct SymbolTable
{
    struct Symbol
    {
        uint16_t address = 0;
        GSPlace place = GSPlace::Window;
        uint16_t page = 0;
    };
    std::map<std::string, Symbol> names;

    explicit SymbolTable(const std::string& text)
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            line = line.substr(0, line.find(';'));
            std::istringstream words(line);
            std::string where, name;
            if (!(words >> where >> name))
                continue;
            Symbol symbol;
            const size_t colon = where.find(':');
            if (colon != std::string::npos)
            {
                const std::string prefix = where.substr(0, 3);
                symbol.place = prefix == "ROM" ? GSPlace::Rom : prefix == "RAM" ? GSPlace::Ram : GSPlace::Window;
                symbol.page = static_cast<uint16_t>(std::strtoul(where.substr(3, colon - 3).c_str(), nullptr, 10));
                where = where.substr(colon + 1);
            }
            char* end = nullptr;
            const unsigned long address = std::strtoul(where.c_str(), &end, 16);
            if (end == where.c_str() || *end || address > 0xFFFF)
                continue;
            symbol.address = static_cast<uint16_t>(address);
            names.emplace(name, symbol);
        }
    }
};

/// Reads one profile; the first fault stops it
class ProfileReader
{
public:
    ProfileReader(const SymbolTable& symbols, std::string& error) : _symbols(symbols), _error(error) {}

    bool Fail(const std::string& message)
    {
        if (_error.empty())
            _error = message;
        return false;
    }
    bool Ok() const { return _error.empty(); }

    std::string Text(const StateNode& node, const char* key, const std::string& fallback = {})
    {
        const StateNode* value = node.find(key);
        if (!value)
            return fallback;
        if (value->kind != StateNode::Kind::String)
            Fail(std::string("'") + key + "' is not a string");
        return value->s;
    }

    bool Flag(const StateNode& node, const char* key, bool fallback = false)
    {
        const StateNode* value = node.find(key);
        if (!value)
            return fallback;
        if (value->kind != StateNode::Kind::Bool)
            Fail(std::string("'") + key + "' is not true / false");
        return value->b;
    }

    /// A number: a JSON integer (decimal), or a string in hex ("#4000", "0x4000", "4000")
    uint32_t Number(const StateNode& value, const std::string& what, uint32_t max)
    {
        uint64_t number = 0;
        if (value.kind == StateNode::Kind::Int && value.i >= 0)
            number = static_cast<uint64_t>(value.i);
        else if (value.kind == StateNode::Kind::String && !value.s.empty())
        {
            std::string text = value.s;
            if (text[0] == '#' || text[0] == '$')
                text = text.substr(1);
            else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
                text = text.substr(2);
            char* end = nullptr;
            number = std::strtoull(text.c_str(), &end, 16);
            if (text.empty() || *end)
            {
                Fail(what + ": '" + value.s + "' is not a number");
                return 0;
            }
        }
        else
        {
            Fail(what + " is not a number");
            return 0;
        }
        if (number > max)
        {
            Fail(what + " is out of range");
            return 0;
        }
        return static_cast<uint32_t>(number);
    }

    uint32_t Number(const StateNode& node, const char* key, uint32_t fallback, uint32_t max)
    {
        const StateNode* value = node.find(key);
        return value ? Number(*value, key, max) : fallback;
    }

    /// An address: a symbol of the profile's symbol file, or a number; a symbol carries its page
    void Address(const StateNode& node, const char* key, std::string& at, uint16_t& address, GSPlace& place,
                 uint16_t& page, bool required)
    {
        const StateNode* value = node.find(key);
        if (!value)
        {
            if (required)
                Fail(std::string("'") + key + "' is missing");
            return;
        }
        if (value->kind == StateNode::Kind::String)
        {
            at = value->s;
            if (const auto it = _symbols.names.find(value->s); it != _symbols.names.end())
            {
                address = it->second.address;
                place = it->second.place;
                page = it->second.page;
                if (place != GSPlace::Window)
                    address = static_cast<uint16_t>(address & 0x3FFF);
                return;
            }
        }
        address = static_cast<uint16_t>(Number(*value, std::string("'") + key + "'", 0xFFFF));
        if (at.empty())
            at = value->kind == StateNode::Kind::String ? value->s : std::to_string(address);
    }

    /// `"page": {"kind": "ram", "page": 2}` overrides the place (the address keeps its offset in the page)
    void Page(const StateNode& node, uint16_t& address, GSPlace& place, uint16_t& page)
    {
        const StateNode* value = node.find("page");
        if (!value)
            return;
        if (!value->isObject())
        {
            Fail("'page' is not an object");
            return;
        }
        const std::string kind = Text(*value, "kind", "ram");
        if (kind == "ram")
            place = GSPlace::Ram;
        else if (kind == "rom" || kind == "flash")
            place = GSPlace::Rom;
        else if (kind == "window")
            place = GSPlace::Window;
        else
            Fail("page kind '" + kind + "' is not ram / rom / window");
        page = static_cast<uint16_t>(Number(*value, "page", 0, 0xFF));
        if (place != GSPlace::Window)
            address = static_cast<uint16_t>(address & 0x3FFF);
    }

    static bool FieldType(const std::string& name, GSFieldType& type, uint16_t& size)
    {
        static const std::pair<const char*, std::pair<GSFieldType, uint16_t>> kTypes[] = {
            {"u8", {GSFieldType::U8, 1}},   {"byte", {GSFieldType::U8, 1}}, {"u16", {GSFieldType::U16, 2}},
            {"word", {GSFieldType::U16, 2}}, {"u24", {GSFieldType::U24, 3}}, {"u32", {GSFieldType::U32, 4}},
            {"s8", {GSFieldType::S8, 1}},   {"s16", {GSFieldType::S16, 2}}, {"bytes", {GSFieldType::Bytes, 1}},
        };
        for (const auto& [n, t] : kTypes)
            if (name == n)
            {
                type = t.first;
                size = t.second;
                return true;
            }
        return false;
    }

    void Structs(const StateNode& list, std::vector<GSStructInfo>& out)
    {
        for (const StateNode& node : list.items)
        {
            GSStructInfo info;
            info.name = Text(node, "name");
            info.meaning = Text(node, "meaning");
            if (info.name.empty())
                Fail("a struct has no name");
            uint32_t end = 0;
            if (const StateNode* fields = node.find("fields"); fields && fields->isArray())
                for (const StateNode& f : fields->items)
                {
                    GSStructField field;
                    field.name = Text(f, "name");
                    field.meaning = Text(f, "meaning");
                    field.offset = static_cast<uint16_t>(Number(f, "offset", 0, 0xFFFF));
                    const std::string type = Text(f, "type", "u8");
                    if (const StateNode* nested = f.find("struct"))
                    {
                        field.type = GSFieldType::Struct;
                        field.structName = nested->s;
                        const GSStructInfo* inner = nullptr;
                        for (const GSStructInfo& s : out)
                            if (s.name == field.structName)
                                inner = &s;
                        if (!inner)
                            Fail("struct " + info.name + ": field " + field.name + " names an unknown struct " +
                                 field.structName + " (declare it first)");
                        field.size = inner ? inner->size : 0;
                    }
                    else if (!FieldType(type, field.type, field.size))
                        Fail("struct " + info.name + ": field " + field.name + " has an unknown type '" + type + "'");
                    if (field.type == GSFieldType::Bytes)
                        field.size = static_cast<uint16_t>(Number(f, "size", 1, 0xFFFF));
                    field.count = static_cast<uint16_t>(Number(f, "count", 1, 0xFFFF));
                    field.stride = static_cast<uint16_t>(Number(f, "stride", 0, 0xFFFF));
                    const uint32_t step = field.stride ? field.stride : field.size;
                    end = std::max<uint32_t>(end, field.offset + step * (field.count ? field.count - 1u : 0u) + field.size);
                    info.fields.push_back(std::move(field));
                }
            info.size = static_cast<uint16_t>(Number(node, "size", end, 0xFFFF));
            out.push_back(std::move(info));
        }
    }

    void Instances(const StateNode& list, GSFirmwareProfile& profile)
    {
        for (const StateNode& node : list.items)
        {
            GSInstanceInfo info;
            info.structName = Text(node, "struct");
            info.name = Text(node, "name", info.structName);
            if (!profile.Struct(info.structName))
                Fail("an instance names an unknown struct '" + info.structName + "'");
            Address(node, "at", info.at, info.address, info.place, info.page, true);
            Page(node, info.address, info.place, info.page);
            info.count = static_cast<uint16_t>(Number(node, "count", 1, 0xFFFF));
            info.stride = static_cast<uint16_t>(Number(node, "stride", 0, 0xFFFF));
            profile.instances.push_back(std::move(info));
        }
    }

    void Variables(const StateNode& list, GSFirmwareProfile& profile)
    {
        for (const StateNode& node : list.items)
        {
            GSProfileVariable v;
            v.name = Text(node, "name");
            v.meaning = Text(node, "meaning");
            const std::string kind = Text(node, "kind", "memory");
            if (kind == "memory")
                v.kind = GSVariableKind::Memory;
            else if (kind == "register")
                v.kind = GSVariableKind::Register;
            else if (kind == "smc_operand")
                v.kind = GSVariableKind::SmcOperand;
            else
                Fail("variable " + v.name + ": kind '" + kind + "' is not memory / register / smc_operand");
            v.size = static_cast<uint8_t>(Number(node, "size", 1, 4));
            if (v.kind == GSVariableKind::Register)
            {
                v.reg = Text(node, "register");
                const StateNode* pc = node.find("pc");
                if (!pc || !pc->isArray() || pc->items.size() != 2)
                    Fail("variable " + v.name + ": 'pc' is not [from, to]");
                else
                {
                    std::string at;
                    GSPlace place = GSPlace::Window;
                    uint16_t page = 0;
                    StateNode range = StateNode::Object();
                    range["from"] = pc->items[0];
                    range["to"] = pc->items[1];
                    Address(range, "from", at, v.pcFrom, place, page, true);
                    at.clear();
                    Address(range, "to", at, v.pcTo, place, page, true);
                }
            }
            else
            {
                Address(node, "at", v.at, v.address, v.place, v.page, true);
                Page(node, v.address, v.place, v.page);
            }
            profile.objectVariables.push_back(std::move(v));
        }
    }

    bool Transfer(const std::string& name, GSCommandInfo& info)
    {
        static const std::pair<const char*, GSTransferKind> kKinds[] = {
            {"none", GSTransferKind::None},
            {"module_upload", GSTransferKind::ModuleUpload},
            {"sample_upload", GSTransferKind::SampleUpload},
            {"block_upload", GSTransferKind::BlockUpload},
            {"block_download", GSTransferKind::BlockDownload},
            {"stream", GSTransferKind::Stream},
            {"block_pages", GSTransferKind::PagedBlock},
            {"sectors", GSTransferKind::Sectors},
        };
        for (const auto& [n, kind] : kKinds)
            if (name == n)
            {
                info.transfer = kind;
                return true;
            }
        // "block256": a fixed block of 256 data bytes
        if (name.rfind("block", 0) == 0 && name.size() > 5)
        {
            char* end = nullptr;
            const unsigned long length = std::strtoul(name.c_str() + 5, &end, 10);
            if (*end == 0 && length > 0 && length <= 0xFFFF)
            {
                info.transfer = GSTransferKind::FixedBlock;
                info.transferLength = static_cast<uint16_t>(length);
                return true;
            }
        }
        return false;
    }

    /// "sectors": { "length": 512, "count": 5, "select": 0, "read": 2, "write": 3 } (parameter indexes, pre first)
    void Sectors(const StateNode& node, const std::string& label, size_t params, GSCommandInfo& info)
    {
        const StateNode* sectors = node.find("sectors");
        if (!sectors || !sectors->isObject())
        {
            Fail(label + ": transfer 'sectors' without a 'sectors' object");
            return;
        }
        info.transferLength = static_cast<uint16_t>(Number(*sectors, "length", 512, 0xFFFF));
        info.sectors.countParam = static_cast<uint8_t>(Number(*sectors, "count", 0, 0xFF));
        info.sectors.selectParam = static_cast<uint8_t>(Number(*sectors, "select", 0, 0xFF));
        if (sectors->find("read"))
            info.sectors.read = static_cast<int16_t>(Number(*sectors, "read", 0, 0xFF));
        if (sectors->find("write"))
            info.sectors.write = static_cast<int16_t>(Number(*sectors, "write", 0, 0xFF));
        if (info.transferLength == 0)
            Fail(label + ": sectors of 0 bytes");
        if (info.sectors.countParam >= params || info.sectors.selectParam >= params)
            Fail(label + ": sectors 'count' / 'select' is not one of its parameters");
    }

    static bool ParamKind(const std::string& name, GSParamKind& kind)
    {
        static const std::pair<const char*, GSParamKind> kKinds[] = {
            {"byte", GSParamKind::Byte},     {"channel", GSParamKind::Channel}, {"channel_mask", GSParamKind::ChannelMask},
            {"volume", GSParamKind::Volume}, {"note", GSParamKind::Note},       {"handle", GSParamKind::Handle},
            {"port", GSParamKind::Port},     {"low", GSParamKind::Low},         {"middle", GSParamKind::Middle},
            {"high", GSParamKind::High},     {"mode", GSParamKind::Mode},
        };
        for (const auto& [n, k] : kKinds)
            if (name == n)
            {
                kind = k;
                return true;
            }
        return false;
    }

    void Commands(const StateNode& list, std::array<GSCommandInfo, 256>& out)
    {
        for (const StateNode& node : list.items)
        {
            const StateNode* number = node.find("number");
            if (!number)
            {
                Fail("a command has no number");
                continue;
            }
            const uint8_t code = static_cast<uint8_t>(Number(*number, "command number", 0xFF));
            GSCommandInfo info;
            const std::string label = "command #" + Hex(code);
            info.name = Keep(Text(node, "name", label));
            if (const std::string handler = Text(node, "handler", ""); !handler.empty())
                info.handler = Keep(handler);
            info.pre = static_cast<uint8_t>(Number(node, "pre", 0, 1));
            size_t count = 0;
            if (const StateNode* params = node.find("params"); params && params->isArray())
                for (const StateNode& p : params->items)
                {
                    if (count == GSCommandInfo::kMaxParams)
                    {
                        Fail(label + ": more than " + std::to_string(GSCommandInfo::kMaxParams) + " parameters");
                        break;
                    }
                    GSParamInfo param;
                    param.name = Keep(Text(p, "name", "byte"));
                    const std::string kind = Text(p, "kind", "byte");
                    if (!ParamKind(kind, param.kind))
                        Fail(label + ": parameter kind '" + kind + "' is unknown");
                    info.params[count++] = param;
                }
            if (info.pre > count)
                Fail(label + ": 'pre' without a parameter");
            info.post = static_cast<uint8_t>(Number(node, "post", count - info.pre, 0xFF));
            info.reply = static_cast<uint8_t>(Number(node, "reply", 0, 0xFF));
            if (const std::string meaning = Text(node, "reply_meaning"); !meaning.empty())
                info.replyMeaning = Keep(meaning);
            if (const std::string note = Text(node, "note"); !note.empty())
                info.note = Keep(note);
            info.documented = Flag(node, "documented");
            info.replyUnflagged = Flag(node, "reply_unflagged");
            if (const std::string transfer = Text(node, "transfer", "none"); !Transfer(transfer, info))
                Fail(label + ": transfer '" + transfer + "' is unknown");
            if (info.transfer == GSTransferKind::Sectors)
                Sectors(node, label, count, info);
            if (const StateNode* handshake = node.find("handshake"))
            {
                const std::string ack = Text(*handshake, "ack", "command");
                if (ack == "data")
                    info.handshake.ack = GSHandshakeAck::Data;
                else if (ack != "command")
                    Fail(label + ": handshake ack '" + ack + "' is not command / data");
                info.handshake.barrier = Flag(*handshake, "barrier");
                info.handshake.strobe = Flag(*handshake, "strobe");
                info.handshake.replyWait = Flag(*handshake, "replyWait");
                info.handshake.valuePoll = Flag(*handshake, "valuePoll");
                info.handshake.exit = Flag(*handshake, "exit");
            }
            out[code] = info;
        }
    }

    /// A list of command numbers: ["E4", "#E6", 0]
    std::vector<uint8_t> CommandList(const StateNode& node, const char* key)
    {
        std::vector<uint8_t> list;
        if (const StateNode* items = node.find(key))
        {
            if (!items->isArray())
                Fail(std::string("'") + key + "' is not a list");
            else
                for (const StateNode& item : items->items)
                    list.push_back(static_cast<uint8_t>(Number(item, "command number", 0xFF)));
        }
        return list;
    }

    void Modes(const StateNode& list, std::vector<GSProfileMode>& out)
    {
        for (const StateNode& node : list.items)
        {
            GSProfileMode mode;
            mode.name = Text(node, "name", "mode");
            mode.enter = CommandList(node, "enter");
            mode.leave = CommandList(node, "leave");
            mode.accepts = CommandList(node, "accepts");
            mode.selects = CommandList(node, "selects");
            mode.leaveFrom = static_cast<uint16_t>(Number(node, "leaveFrom", 0x100, 0x100));
            if (mode.enter.empty())
                Fail("mode '" + mode.name + "' has no 'enter' command");
            out.push_back(std::move(mode));
        }
    }

    static std::string Hex(uint8_t value)
    {
        static const char* digits = "0123456789ABCDEF";
        return {digits[value >> 4], digits[value & 15]};
    }

private:
    const SymbolTable& _symbols;
    std::string& _error;
};

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

/// The profile files of <symbols>/gs/profiles, in name order; a broken one is skipped with a warning
std::vector<std::unique_ptr<GSFirmwareProfile>> LoadProfileFiles()
{
    std::vector<std::unique_ptr<GSFirmwareProfile>> profiles;
    const std::string folder = Emulator::SymbolBundleFolder();
    if (folder.empty())
        return profiles;
    const std::filesystem::path symbols = FileHelper::ToFsPath(FileHelper::PathCombine(folder, "gs"));
    const std::filesystem::path root = symbols / "profiles";
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
        return profiles;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec))
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& file : files)
    {
        const std::string json = ReadText(file);
        // The symbol file is named relative to <symbols>/gs ("symbolFile": "neotracker.map")
        std::string symbolText;
        StateNode doc;
        if (StateNodeFromJsonText(json, doc) && doc.isObject())
            for (const char* key : {"symbolFile", "symbol_file"})
                if (const StateNode* name = doc.find(key); name && name->kind == StateNode::Kind::String)
                {
                    symbolText = ReadText(symbols / FileHelper::ToFsPath(name->s));
                    break;
                }
        std::string error;
        std::unique_ptr<GSFirmwareProfile> profile = ParseGSFirmwareProfile(json, symbolText, error);
        if (!profile)
        {
            LOGWARNING("GS firmware profile %s: %s", file.string().c_str(), error.c_str());
            continue;
        }
        profile->source = file.filename().string();
        profiles.push_back(std::move(profile));
    }
    return profiles;
}

/// endregion </Profile files>

/// A built-in profile: the tables of this file, in the structure the JSON files use
GSFirmwareProfile Builtin(const char* id, const char* title, const char* sha256, const char* page0, const char* page1,
                          const std::array<GSCommandInfo, 256>& commands, const GSVariableInfo* variables,
                          size_t variableCount)
{
    GSFirmwareProfile p{};
    p.id = id;
    p.title = title;
    p.sha256 = sha256;
    p.sha256Pages[0] = page0;
    p.sha256Pages[1] = page1;
    p.comtab = 0x0300;
    p.comtabh = 0xD720;
    p.commands = &commands;
    p.variables = variables;
    p.variableCount = variableCount;
    p.streamCommands = kStreamCommands;
    p.streamCommandCount = std::size(kStreamCommands);
    p.source = "built-in";
    // The test mode (TEST_H.a80 TCOM): #00-#1F pick a test from TCOMTB, a command from #20 on goes to COMINT2
    GSProfileMode test;
    test.name = "test";
    test.enter = {0xFA};
    for (uint8_t code = 0; code < 0x20; code++)
        test.selects.push_back(code);
    test.leaveFrom = 0x20;
    p.modes.push_back(std::move(test));
    return p;
}

} // namespace

std::unique_ptr<GSFirmwareProfile> ParseGSFirmwareProfile(const std::string& json, const std::string& symbols,
                                                          std::string& error)
{
    error.clear();
    StateNode doc;
    std::string parseError;
    if (!StateNodeFromJsonText(json, doc, &parseError) || !doc.isObject())
    {
        error = parseError.empty() ? "not a JSON object" : parseError;
        return nullptr;
    }
    const SymbolTable table(symbols);
    ProfileReader r(table, error);
    auto profile = std::make_unique<GSFirmwareProfile>();
    // The strings and the command table live as long as the program (the command log keeps the profile's index)
    profile->id = Keep(r.Text(doc, "id"));
    profile->title = Keep(r.Text(doc, "title", profile->id));
    if (!*profile->id)
        r.Fail("'id' is missing");
    profile->sha256 = "";
    profile->sha256Pages[0] = profile->sha256Pages[1] = "";
    std::array<GSCommandInfo, 256>* commands = KeepTable();
    profile->commands = commands;
    if (const StateNode* list = doc.find("commands"); list && list->isArray())
        r.Commands(*list, *commands);
    if (const StateNode* list = doc.find("match"); list && list->isArray())
        for (const StateNode& node : list->items)
        {
            GSProfileMatch match;
            const std::string kind = r.Text(node, "kind", "rom");
            if (kind == "upload")
                match.kind = GSMatchKind::Upload;
            else if (kind != "rom")
                r.Fail("match kind '" + kind + "' is not rom / upload");
            match.sha256 = r.Text(node, "sha256");
            std::transform(match.sha256.begin(), match.sha256.end(), match.sha256.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (match.sha256.size() != 64)
                r.Fail("a match's sha256 is not 64 hex digits");
            if (match.kind == GSMatchKind::Upload)
            {
                std::string at;
                GSPlace place = GSPlace::Window;
                uint16_t page = 0;
                r.Address(node, "addr", at, match.address, place, page, true);
                match.length = r.Number(node, "length", 0, 0x10000);
            }
            profile->matches.push_back(std::move(match));
        }
    if (const StateNode* d = doc.find("dispatch"))
    {
        GSDispatchInfo& dispatch = profile->dispatch;
        const std::string kind = r.Text(*d, "kind", "words");
        dispatch.kind = kind == "stock_split" ? GSDispatchKind::StockSplit : GSDispatchKind::Words;
        if (kind != "words" && kind != "stock_split")
            r.Fail("dispatch kind '" + kind + "' is not words / stock_split");
        std::string at;
        GSPlace place = GSPlace::Window;
        uint16_t page = 0;
        r.Address(*d, "pc", at, dispatch.pc, place, page, true);
        dispatch.reg = r.Text(*d, "register", "A");
        at.clear();
        r.Address(*d, "table", at, dispatch.table, dispatch.place, dispatch.page, true);
        r.Page(*d, dispatch.table, dispatch.place, dispatch.page);
        dispatch.entries = static_cast<uint16_t>(r.Number(*d, "entries", 256, 256));
        dispatch.highFrom = static_cast<uint16_t>(r.Number(*d, "highFrom", 0x20, 0xFF));
        if (dispatch.kind == GSDispatchKind::StockSplit)
        {
            // COMTAB in ROM page 0, COMTABH in the high ROM window (as the built-in tables)
            profile->comtab = dispatch.table;
            std::string high;
            GSPlace highPlace = GSPlace::Window;
            uint16_t highPage = 0;
            r.Address(*d, "tableHigh", high, profile->comtabh, highPlace, highPage, true);
            if (highPlace == GSPlace::Rom)
                profile->comtabh = static_cast<uint16_t>(0xC000 | (profile->comtabh & 0x3FFF));
        }
        if (d->find("unknownStub"))
        {
            at.clear();
            r.Address(*d, "unknownStub", at, dispatch.unknownStub, place, page, true);
            dispatch.hasUnknownStub = true;
        }
    }
    if (const StateNode* list = doc.find("structs"); list && list->isArray())
        r.Structs(*list, profile->structs);
    if (const StateNode* list = doc.find("instances"); list && list->isArray())
        r.Instances(*list, *profile);
    if (const StateNode* list = doc.find("variables"); list && list->isArray())
        r.Variables(*list, *profile);
    if (const StateNode* list = doc.find("modes"); list && list->isArray())
        r.Modes(*list, profile->modes);
    profile->unsolicitedReplies = r.Flag(doc, "unsolicitedReplies");
    profile->hostDataStream = r.Flag(doc, "hostDataStream");
    profile->extends = r.Flag(doc, "extends");
    if (!r.Ok())
        return nullptr;
    return profile;
}

const GSVariableInfo* GSFirmwareProfile::Variable(const std::string& name) const
{
    for (size_t i = 0; i < variableCount; i++)
        if (name == variables[i].name)
            return &variables[i];
    return nullptr;
}

const GSStructInfo* GSFirmwareProfile::Struct(const std::string& name) const
{
    for (const GSStructInfo& info : structs)
        if (info.name == name)
            return &info;
    return nullptr;
}

const GSProfileMatch* GSFirmwareProfile::UploadMatch(const std::string& digest, uint16_t address, uint32_t length) const
{
    for (const GSProfileMatch& match : matches)
        if (match.kind == GSMatchKind::Upload && match.sha256 == digest && match.address == address &&
            (match.length == 0 || match.length == length))
            return &match;
    return nullptr;
}

const std::vector<const GSFirmwareProfile*>& GSFirmwareProfiles()
{
    // The built-in tables are built on first use, then the files of <symbols>/gs/profiles join in name order
    static const std::vector<const GSFirmwareProfile*> profiles = [] {
        static GSFirmwareProfile gs104 = Builtin("gs104", "General Sound firmware v1.04",
            "03b1e1801b3682bd0ef25f6006299f7bfacb3d8755f8792c87ea4d856e47dce0",
            "a3b0cc4dcee93e7de5c389d0b43ae62f698269b7636b24308f7efcee74da23d6",
            "fb5fd644216caa81ed2e154d183153469f7f2d9db5e211ea7340617a11c20292", Table104(), kVariables104,
            std::size(kVariables104));
        static GSFirmwareProfile gs105a = Builtin("gs105a", "General Sound firmware v1.05a",
            "e256c963cf1bc5565bde1160855280a3d5cdd7e5d8481f3ee1c93f4e7e77b4d5",
            "63266df7ba7b875b5cbfa7225b5b081ecd751790a6548530db35301cae86cbbc",
            "16f8d2413069ff8fb73286aee0a542e17c9fa1066b735ec281f169a87ffd6e8a", Table105(), kVariables105,
            std::size(kVariables105));
        static GSFirmwareProfile gs105b = Builtin("gs105b", "General Sound firmware v1.05b",
            "9948ec9617365fb1913b0c56e00d5f6b6a36084875cca16debae270a4c6e6adf",
            "db59edcfc246179f17ee168514fd662df4fb74e61add22c4bf91b2e388b84bfa",
            "16f8d2413069ff8fb73286aee0a542e17c9fa1066b735ec281f169a87ffd6e8a", Table105(), kVariables105,
            std::size(kVariables105));
        // NeoGS main ROM v1.11 (data/rom/neogs/full_ngs.rom at #10000, run from RAM pages 0-1): the v1.05a command
        // table and variables, handler for handler (data/symbols/gs/NEOGS-VS-GS.md, gs-debugger N5)
        static GSFirmwareProfile neogs = Builtin("neogs", "NeoGS firmware v1.11",
            "73c3342a2cb37171e4c5b7147283157ee48116c056c1d02c48c7e6e11245c678",
            "7decef1ec3afe8d6eec52371f89b650afaaecbc527fa80c47bbb1ea67603e3f1",
            "6e724b4db2a88ae6a430e3f939f85ed0c0ee8fa040f2924fa257e584e67e10dc", Table105(), kVariables105,
            std::size(kVariables105));
        std::vector<const GSFirmwareProfile*> list{&gs104, &gs105a, &gs105b, &neogs};
        static std::vector<std::unique_ptr<GSFirmwareProfile>> files;
        for (std::unique_ptr<GSFirmwareProfile>& file : LoadProfileFiles())
            files.push_back(std::move(file));
        for (const std::unique_ptr<GSFirmwareProfile>& file : files)
            if (list.size() < 0xFF) // the command log's index byte; #FF means none
                list.push_back(file.get());
        return list;
    }();
    return profiles;
}

const GSFirmwareProfile* FindGSFirmwareProfileById(const std::string& id)
{
    for (const GSFirmwareProfile* profile : GSFirmwareProfiles())
        if (id == profile->id)
            return profile;
    return nullptr;
}

const GSFirmwareProfile* FindGSUploadProfile(const std::string& sha256, uint16_t address, uint32_t length)
{
    for (const GSFirmwareProfile* profile : GSFirmwareProfiles())
        if (profile->UploadMatch(sha256, address, length))
            return profile;
    return nullptr;
}

const GSFirmwareProfile* FindGSFirmwareProfile(const std::string& sha256)
{
    for (const GSFirmwareProfile* profile : GSFirmwareProfiles())
    {
        if (profile->sha256 && sha256 == profile->sha256)
            return profile;
        for (const GSProfileMatch& match : profile->matches)
            if (match.kind == GSMatchKind::Rom && match.sha256 == sha256)
                return profile;
    }
    return nullptr;
}

GSFirmwareIdentity IdentifyGSFirmware(const GeneralSoundCard& card)
{
    GSFirmwareIdentity identity;
    const uint8_t* page0 = card.firmwarePage(0);
    const uint8_t* page1 = card.firmwarePage(1);
    if (!page0 || !page1)
        return identity;
    identity.hasRom = true;
    identity.sha256Pages[0] = SignatureCache::Sha256Hex(page0, kPageSize);
    identity.sha256Pages[1] = SignatureCache::Sha256Hex(page1, kPageSize);
    if (page1 == page0 + kPageSize)
        identity.sha256 = SignatureCache::Sha256Hex(page0, 2 * kPageSize);
    else
    {
        std::vector<uint8_t> image(page0, page0 + kPageSize);
        image.insert(image.end(), page1, page1 + kPageSize);
        identity.sha256 = SignatureCache::Sha256Hex(image.data(), image.size());
    }
    identity.profile = FindGSFirmwareProfile(identity.sha256);
    return identity;
}

const GSFirmwareProfile* GSFirmwareProfileOf(const GeneralSoundCard& card)
{
    if (card.implementation() == GSCardImplementation::LW)
        return GSFirmwareProfiles()[1];
    return IdentifyGSFirmware(card).profile;
}
