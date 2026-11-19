#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"

#include <deque>
#include <mutex>

#include "common/signaturecache.h"
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
        if (i < 4)
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
    c[0xF4] = Cmd("Cold restart", 0, 0, {}, 0, nullptr, T::None, true, "JP #0000");
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

const GSFirmwareProfile kGS104 = {"gs104", "General Sound firmware v1.04",
                                  "03b1e1801b3682bd0ef25f6006299f7bfacb3d8755f8792c87ea4d856e47dce0",
                                  {"a3b0cc4dcee93e7de5c389d0b43ae62f698269b7636b24308f7efcee74da23d6",
                                   "fb5fd644216caa81ed2e154d183153469f7f2d9db5e211ea7340617a11c20292"},
                                  0x0300, 0xD720, nullptr, kVariables104, std::size(kVariables104), kStreamCommands,
                                  std::size(kStreamCommands)};
const GSFirmwareProfile kGS105a = {"gs105a", "General Sound firmware v1.05a",
                                   "e256c963cf1bc5565bde1160855280a3d5cdd7e5d8481f3ee1c93f4e7e77b4d5",
                                   {"63266df7ba7b875b5cbfa7225b5b081ecd751790a6548530db35301cae86cbbc",
                                    "16f8d2413069ff8fb73286aee0a542e17c9fa1066b735ec281f169a87ffd6e8a"},
                                   0x0300, 0xD720, nullptr, kVariables105, std::size(kVariables105), kStreamCommands,
                                   std::size(kStreamCommands)};
const GSFirmwareProfile kGS105b = {"gs105b", "General Sound firmware v1.05b",
                                   "9948ec9617365fb1913b0c56e00d5f6b6a36084875cca16debae270a4c6e6adf",
                                   {"db59edcfc246179f17ee168514fd662df4fb74e61add22c4bf91b2e388b84bfa",
                                    "16f8d2413069ff8fb73286aee0a542e17c9fa1066b735ec281f169a87ffd6e8a"},
                                   0x0300, 0xD720, nullptr, kVariables105, std::size(kVariables105), kStreamCommands,
                                   std::size(kStreamCommands)};

} // namespace

const GSVariableInfo* GSFirmwareProfile::Variable(const std::string& name) const
{
    for (size_t i = 0; i < variableCount; i++)
        if (name == variables[i].name)
            return &variables[i];
    return nullptr;
}

const std::vector<const GSFirmwareProfile*>& GSFirmwareProfiles()
{
    // The command tables are built on first use; the profiles point at them
    static const std::vector<const GSFirmwareProfile*> profiles = [] {
        static GSFirmwareProfile gs104 = kGS104;
        static GSFirmwareProfile gs105a = kGS105a;
        static GSFirmwareProfile gs105b = kGS105b;
        gs104.commands = &Table104();
        gs105a.commands = &Table105();
        gs105b.commands = &Table105();
        return std::vector<const GSFirmwareProfile*>{&gs104, &gs105a, &gs105b};
    }();
    return profiles;
}

const GSFirmwareProfile* FindGSFirmwareProfile(const std::string& sha256)
{
    for (const GSFirmwareProfile* profile : GSFirmwareProfiles())
        if (sha256 == profile->sha256)
            return profile;
    return nullptr;
}

GSFirmwareIdentity IdentifyGSFirmware(const GeneralSoundCard& card)
{
    GSFirmwareIdentity identity;
    const uint8_t* page0 = card.cardRomPage(0);
    const uint8_t* page1 = card.cardRomPage(1);
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
