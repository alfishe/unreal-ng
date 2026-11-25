// The built-in General Sound firmware profiles against the shipped ROMs (gs-debugger design §6.1, F2, L4)

#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareobjects.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

namespace
{

struct FirmwareCard
{
    EmulatorContext ctx{LoggerLevel::LogError};
    std::unique_ptr<SoundChip_GeneralSound> chip;
    std::unique_ptr<GSCardTarget> target;

    explicit FirmwareCard(const std::string& rom)
    {
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
        chip->loadROM(rom.c_str());
        target = std::make_unique<GSCardTarget>(&ctx, [this]() -> GeneralSoundCard* { return chip.get(); });
    }
    ~FirmwareCard() { target.reset(); }

    LabelManager& labels() { return *target->GetLabelManager(); }
};

/// The handler a firmware's tables give a command: COMTAB words for #00-#1F and #F0-#FF, COMTABH bytes for #20-#EF
uint16_t Handler(const GeneralSoundCard& card, const GSFirmwareProfile& profile, int code)
{
    const uint8_t* rom0 = card.firmwarePage(0);
    const uint8_t* rom1 = card.firmwarePage(1);
    if (code < 0x20 || code >= 0xF0)
    {
        const int index = code < 0x20 ? code : code - 0xD0;
        const uint8_t* entry = rom0 + profile.comtab + 2 * index;
        return uint16_t(entry[0] | (entry[1] << 8));
    }
    const uint8_t* low = rom1 + (profile.comtabh - 0xC000) + (code - 0x20);
    return uint16_t(low[0] | (low[0x100] << 8));
}

/// The built-in profiles (the files of <symbols>/gs/profiles follow them)
std::vector<const GSFirmwareProfile*> Builtins()
{
    std::vector<const GSFirmwareProfile*> list;
    for (const GSFirmwareProfile* profile : GSFirmwareProfiles())
        if (profile->source == "built-in")
            list.push_back(profile);
    return list;
}

/// The built-in profiles of the classic card's ROMs (data/rom/<id>.rom); NeoGS's is the last built-in
std::vector<const GSFirmwareProfile*> ClassicBuiltins()
{
    std::vector<const GSFirmwareProfile*> list = Builtins();
    list.erase(std::remove_if(list.begin(), list.end(),
                              [](const GSFirmwareProfile* p) { return std::string(p->id) == "neogs"; }),
               list.end());
    return list;
}

/// A NeoGS card with the shipped flash, booted by its loader
struct NeoFirmwareCard
{
    EmulatorContext ctx{LoggerLevel::LogError};
    NeoGSConfig config;
    std::unique_ptr<SoundChip_NeoGS> chip;
    std::unique_ptr<GSCardTarget> target;

    NeoFirmwareCard()
    {
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_NeoGS>(&ctx, config, 44100);
        chip->loadROM("rom/neogs/full_ngs.rom");
        target = std::make_unique<GSCardTarget>(&ctx, [this]() -> GeneralSoundCard* { return chip.get(); });
    }
    ~NeoFirmwareCard() { target.reset(); }

    /// Frames until the main ROM's command loop, -1 past `maxFrames`
    int boot(int maxFrames)
    {
        for (int f = 0; f < maxFrames; f++)
        {
            if (chip->isReadyForCommands())
                return f;
            chip->runFor(2396020);  // a Pentagon frame of 120 MHz ticks
        }
        return -1;
    }
};

} // namespace

/// Every built-in profile is the firmware of a shipped ROM: the card's digest selects it and its symbols are loaded
TEST(GSFirmwareProfile_Test, EachShippedRomSelectsItsProfile)
{
    ASSERT_EQ(Builtins().size(), 4u);
    ASSERT_EQ(GSFirmwareProfiles()[0], Builtins()[0]) << "the built-in tables come first: the command log stores an index";
    for (const GSFirmwareProfile* profile : ClassicBuiltins())
    {
        SCOPED_TRACE(profile->id);
        FirmwareCard card(std::string("rom/") + profile->id + ".rom");
        const GSFirmwareIdentity identity = IdentifyGSFirmware(*card.chip);
        ASSERT_TRUE(identity.hasRom);
        EXPECT_EQ(identity.sha256, profile->sha256);
        EXPECT_EQ(identity.sha256Pages[0], profile->sha256Pages[0]);
        EXPECT_EQ(identity.sha256Pages[1], profile->sha256Pages[1]);
        EXPECT_EQ(identity.profile, profile);

        const std::vector<std::string> applied = card.target->ApplySymbolBundles();
        ASSERT_EQ(applied.size(), 1u);
        EXPECT_EQ(applied[0], std::string("bundle:gs:") + profile->id);
    }
    EXPECT_EQ(FindGSFirmwareProfile(std::string(64, '0')), nullptr);
}

/// The self-check: the ROM's own command tables against the profile. A handler that is not the "unknown command"
/// stub (COMZ, COMHZ) has a named command, a stub has none, and every handler has a label
TEST(GSFirmwareProfile_Test, EveryHandlerInTheRomTablesHasANamedCommand)
{
    for (const GSFirmwareProfile* profile : ClassicBuiltins())
    {
        SCOPED_TRACE(profile->id);
        FirmwareCard card(std::string("rom/") + profile->id + ".rom");
        card.target->ApplySymbolBundles();
        LabelManager& labels = card.labels();
        const auto comz = labels.GetLabelByName("COMZ");
        const auto comhz = labels.GetLabelByName("COMHZ");
        const auto comtab = labels.GetLabelByName("COMTAB");
        const auto comtabh = labels.GetLabelByName("COMTABH");
        ASSERT_TRUE(comz && comhz && comtab && comtabh);
        EXPECT_EQ(comtab->address, profile->comtab);
        EXPECT_EQ(comtabh->address, profile->comtabh);

        int named = 0;
        for (int code = 0; code < 256; code++)
        {
            const uint16_t handler = Handler(*card.chip, *profile, code);
            const bool stub = handler == comz->address || handler == comhz->address;
            const GSCommandInfo& command = profile->Command(uint8_t(code));
            EXPECT_EQ(command.name != nullptr, !stub) << "command #" << std::hex << code << " handler #" << handler;
            EXPECT_NE(labels.GetLabelByZ80Address(handler), nullptr) << "command #" << std::hex << code;
            if (command.name)
            {
                named++;
                EXPECT_LE(command.pre, 1);
                const int sent = command.pre + (command.post == GSCommandInfo::kVariablePost ? 0 : command.post);
                for (int i = 0; i < sent && i < 4; i++)
                    EXPECT_NE(command.params[i].name, nullptr) << "command #" << std::hex << code << " param " << i;
            }
        }
        EXPECT_EQ(named, std::string(profile->id) == "gs104" ? 129 : 131);
    }
}

/// The variables are where the firmware's labels put them; EQU ones (no label) lie in the fixed RAM window
TEST(GSFirmwareProfile_Test, TheVariablesAreAtTheirLabels)
{
    for (const GSFirmwareProfile* profile : ClassicBuiltins())
    {
        SCOPED_TRACE(profile->id);
        FirmwareCard card(std::string("rom/") + profile->id + ".rom");
        card.target->ApplySymbolBundles();
        for (size_t i = 0; i < profile->variableCount; i++)
        {
            const GSVariableInfo& variable = profile->variables[i];
            EXPECT_GE(variable.address, 0x4000);
            EXPECT_LT(variable.address + variable.size, 0x8000);
            if (variable.equ)
                continue;
            // One label per address: a name that shares its address (MEMTOP / TSMPH at #40E2) yields to the other
            const auto label = card.labels().GetLabelByName(variable.name);
            if (label)
                EXPECT_EQ(label->address, variable.address) << variable.name;
            else
                EXPECT_NE(card.labels().GetLabelByZ80Address(variable.address), nullptr) << variable.name;
        }
        EXPECT_EQ(profile->Variable("BUSY")->address, 0x4086);
        EXPECT_EQ(profile->Variable("nothing"), nullptr);
    }
    // 1.05's relooper variables follow the module type byte (patch.a80: PlMode EQU MODTP+1, MODLLEN EQU PlMode+1)
    const GSFirmwareProfile* gs105a = FindGSFirmwareProfile(GSFirmwareProfiles()[1]->sha256);
    EXPECT_EQ(gs105a->Variable("PlMode")->address, gs105a->Variable("MODTP")->address + 1);
    EXPECT_EQ(gs105a->Variable("MODLLEN")->address, gs105a->Variable("PlMode")->address + 1);
}

/// NeoGS (N5): the loader runs the main ROM from RAM pages 0-1, and that copy selects the profile, flash page 4 the
/// symbol bundle. Its command tables are v1.05a's handler for handler (data/symbols/gs/NEOGS-VS-GS.md): every one is
/// named, and labelled where the card CPU sees it
TEST(GSFirmwareProfile_Test, TheNeoGSMainRomSelectsItsProfile)
{
    NeoFirmwareCard card;
    const GSFirmwareProfile* neogs = FindGSFirmwareProfileById("neogs");
    ASSERT_NE(neogs, nullptr);
    EXPECT_EQ(IdentifyGSFirmware(*card.chip).profile, neogs) << "before the boot: the flash copy the loader starts";
    const std::vector<std::string> applied = card.target->ApplySymbolBundles();
    EXPECT_NE(std::find(applied.begin(), applied.end(), "bundle:gs:neogs"), applied.end());

    ASSERT_GE(card.boot(50), 0);
    const GSFirmwareIdentity identity = IdentifyGSFirmware(*card.chip);
    EXPECT_EQ(card.chip->firmwarePage(0), card.chip->cardRamPage(0)) << "the main ROM runs from RAM";
    EXPECT_EQ(identity.sha256, neogs->sha256);
    EXPECT_EQ(identity.profile, neogs);
    EXPECT_EQ(GSFirmwareProfileOf(*card.chip), neogs);

    const MemoryPageDescriptor table = card.target->MapAddress(neogs->comtab);
    EXPECT_EQ(table.mode, BANK_RAM);
    EXPECT_EQ(table.page, 0);
    LabelManager& labels = *card.target->GetLabelManager();
    const auto comz = labels.GetLabelByName("COMZ");
    const auto comhz = labels.GetLabelByName("COMHZ");
    const auto comtab = labels.GetLabelByName("COMTAB");
    ASSERT_TRUE(comz && comhz && comtab);
    EXPECT_EQ(comtab->address, neogs->comtab);
    int named = 0;
    for (int code = 0; code < 256; code++)
    {
        const uint16_t handler = Handler(*card.chip, *neogs, code);
        const bool stub = handler == comz->address || handler == comhz->address;
        EXPECT_EQ(neogs->Command(uint8_t(code)).name != nullptr, !stub) << "command #" << std::hex << code;
        EXPECT_NE(labels.GetLabelByZ80Address(handler), nullptr) << "command #" << std::hex << code;
        named += stub ? 0 : 1;
    }
    EXPECT_EQ(named, 131);
    const auto busy = labels.GetLabelByName("BUSY");
    ASSERT_TRUE(busy);
    EXPECT_EQ(busy->address, neogs->Variable("BUSY")->address);
}

/// The dispatch view on a NeoGS (M4): COMHIGH maps page 0 before it reads COMTABH, so the core reads the tables in
/// the firmware's own image (the RAM copy in pages 0-1), whatever window 3 maps, against the copy as the main ROM
/// started: the stock firmware has nothing patched; a handler written over in the copy is that one entry patched,
/// its baseline the handler it had
TEST(GSFirmwareProfile_Test, TheNeoGSDispatchReadsItsRamCopyAgainstItsStart)
{
    NeoFirmwareCard card;
    ASSERT_GE(card.boot(50), 0);
    const GSFirmwareProfile* neogs = FindGSFirmwareProfileById("neogs");
    ASSERT_NE(neogs, nullptr);
    const auto read = [&]() {
        const StateNode body = GSReadFirmwareObjects(*card.chip, *neogs, "dispatch");
        const StateNode* dispatch = body.find("dispatch");
        return dispatch ? *dispatch : StateNode();
    };
    StateNode dispatch = read();
    ASSERT_TRUE(dispatch.isObject());
    ASSERT_EQ(dispatch.find("entries")->items.size(), 256u);
    EXPECT_EQ(dispatch.find("patched")->i, 0) << "the stock firmware against its own start";
    char hex[8];
    for (int code = 0; code < 256; code++)
    {
        const StateNode& e = dispatch.find("entries")->items[static_cast<size_t>(code)];
        std::snprintf(hex, sizeof(hex), "#%04X", Handler(*card.chip, *neogs, code));
        ASSERT_NE(e.find("handler"), nullptr) << "command #" << std::hex << code;
        EXPECT_EQ(e.find("handler")->s, hex) << "command #" << std::hex << code;
        EXPECT_EQ(e.find("baseline")->s, hex) << "command #" << std::hex << code;
    }

    // Command #01's word in the copy written over (RAM pages 0-1 are write-protected to the card's CPU in main mode)
    auto* page0 = const_cast<uint8_t*>(card.chip->cardRamPage(0));
    ASSERT_NE(page0, nullptr);
    uint8_t& low = page0[neogs->comtab + 2];
    const uint8_t old = low;
    std::snprintf(hex, sizeof(hex), "#%04X", Handler(*card.chip, *neogs, 1));
    const std::string before = hex;
    low ^= 0x01;
    dispatch = read();
    EXPECT_EQ(dispatch.find("patched")->i, 1);
    const StateNode& c01 = dispatch.find("entries")->items[1];
    EXPECT_TRUE(c01.find("patched") && c01.find("patched")->b);
    EXPECT_EQ(c01.find("baseline")->s, before);
    EXPECT_NE(c01.find("handler")->s, before);
    low = old;
}

/// The commands the guide omits are named from the sources; the 1.05 patch's commands exist only in 1.05
TEST(GSFirmwareProfile_Test, TheSourcesNameWhatTheGuideOmits)
{
    const GSFirmwareProfile* gs104 = GSFirmwareProfiles()[0];
    const GSFirmwareProfile* gs105a = GSFirmwareProfiles()[1];
    EXPECT_STREQ(gs105a->Command(0x0E).name, "LPT Covox mode");
    EXPECT_EQ(gs105a->Command(0x0E).transfer, GSTransferKind::Stream);
    EXPECT_STREQ(gs105a->Command(0x13).name, "Jump to address");
    EXPECT_STREQ(gs105a->Command(0x16).name, "Put byte");
    EXPECT_EQ(gs105a->Command(0x16).pre + gs105a->Command(0x16).post, 3);
    EXPECT_EQ(gs105a->Command(0x30).transfer, GSTransferKind::ModuleUpload);
    EXPECT_STREQ(gs105a->Command(0x9A).name, "Play FX with note and volume in channel 2");
    EXPECT_EQ(gs105a->Command(0x9A).post, 2);
    EXPECT_STREQ(gs105a->Command(0xAB).name, "Change volume in channel 3");
    EXPECT_STREQ(gs105a->Command(0x6A).name, "Set player mode");
    EXPECT_EQ(gs104->Command(0x6A).name, nullptr);
    EXPECT_EQ(gs104->Command(0x6B).name, nullptr);
    EXPECT_EQ(gs105a->Command(0x08).name, nullptr);
}

/// Profile schema 2 (design §8A.9): a JSON profile with its matches, dispatcher, commands with handshakes, structs,
/// instances bound to a page and variables; names come from its symbol file
TEST(GSFirmwareProfile_Test, AJsonProfileReadsEveryPart)
{
    const std::string symbols = "; test\n"
                                "RAM2:0100  SamplTabl  (DATA)\n"
                                "5100       COMINT     (CODE)\n"
                                "5180       COMTAB     (DATA)\n"
                                "51F0       BADCOM     (CODE)\n"
                                "5200       ringLoop   (CODE)\n"
                                "5240       ringEnd    (CODE)\n"
                                "5301       streaminfobyte (DATA)\n";
    const std::string json = R"({
      "id": "test", "title": "Test player",
      "match": [ {"kind": "upload", "addr": "#5100", "length": 768,
                  "sha256": "3FA1000000000000000000000000000000000000000000000000000000000000"} ],
      "dispatch": { "pc": "COMINT", "register": "A", "table": "COMTAB", "entries": 8, "unknownStub": "BADCOM" },
      "commands": [ { "number": "E8", "name": "start load", "params": [{"name": "slot", "kind": "byte"}],
                      "handshake": { "ack": "data" }, "transfer": "block256" },
                    { "number": 0, "name": "reset", "handshake": { "barrier": true } } ],
      "structs": [ { "name": "smp", "size": 24, "fields": [ {"name": "SamplAdr", "offset": 0, "type": "u16"},
                                                            {"name": "Vol", "offset": 6, "type": "u8"},
                                                            {"name": "Name", "offset": 8, "type": "bytes", "size": 4} ] },
                   { "name": "pair", "fields": [ {"name": "a", "offset": 0, "struct": "smp"},
                                                 {"name": "b", "offset": 24, "struct": "smp"} ] } ],
      "instances": [ { "struct": "smp", "at": "SamplTabl", "count": 8 },
                     { "name": "fixed", "struct": "smp", "at": "#8100", "page": {"kind": "ram", "page": 5} } ],
      "variables": [ { "name": "ringFree", "kind": "register", "register": "HL", "pc": ["ringLoop", "ringEnd"] },
                     { "name": "streamInfo", "kind": "smc_operand", "at": "streaminfobyte" },
                     { "name": "mode", "at": "#4100", "size": 2 } ]
    })";
    std::string error;
    const std::unique_ptr<GSFirmwareProfile> p = ParseGSFirmwareProfile(json, symbols, error);
    ASSERT_NE(p, nullptr) << error;
    EXPECT_STREQ(p->id, "test");
    ASSERT_EQ(p->matches.size(), 1u);
    EXPECT_EQ(p->matches[0].kind, GSMatchKind::Upload);
    EXPECT_EQ(p->matches[0].sha256.substr(0, 4), "3fa1") << "digests compare in lower case";
    EXPECT_NE(p->UploadMatch(p->matches[0].sha256, 0x5100, 768), nullptr);
    EXPECT_EQ(p->UploadMatch(p->matches[0].sha256, 0x5100, 769), nullptr) << "the length is part of the match";

    EXPECT_EQ(p->dispatch.kind, GSDispatchKind::Words);
    EXPECT_EQ(p->dispatch.pc, 0x5100);
    EXPECT_EQ(p->dispatch.table, 0x5180);
    EXPECT_EQ(p->dispatch.entries, 8);
    EXPECT_TRUE(p->dispatch.hasUnknownStub);
    EXPECT_EQ(p->dispatch.unknownStub, 0x51F0);

    const GSCommandInfo& e8 = p->Command(0xE8);
    EXPECT_STREQ(e8.name, "start load");
    EXPECT_EQ(e8.post, 1) << "params without 'pre' are sent after the command";
    EXPECT_EQ(e8.handshake.ack, GSHandshakeAck::Data);
    EXPECT_EQ(e8.transfer, GSTransferKind::FixedBlock);
    EXPECT_EQ(e8.transferLength, 256);
    EXPECT_TRUE(p->Command(0x00).handshake.barrier);
    EXPECT_EQ(p->Command(0x01).name, nullptr) << "a command the profile does not list is unknown";

    const GSStructInfo* pair = p->Struct("pair");
    ASSERT_NE(pair, nullptr);
    EXPECT_EQ(pair->size, 48) << "the size defaults to the end of the last field";
    ASSERT_EQ(p->instances.size(), 2u);
    EXPECT_EQ(p->instances[0].place, GSPlace::Ram) << "a symbol of RAM page 2 binds the instance to the page";
    EXPECT_EQ(p->instances[0].page, 2);
    EXPECT_EQ(p->instances[0].address, 0x0100);
    EXPECT_EQ(p->instances[1].place, GSPlace::Ram);
    EXPECT_EQ(p->instances[1].page, 5);
    EXPECT_EQ(p->instances[1].address, 0x0100) << "the page's offset";

    ASSERT_EQ(p->objectVariables.size(), 3u);
    EXPECT_EQ(p->objectVariables[0].kind, GSVariableKind::Register);
    EXPECT_EQ(p->objectVariables[0].pcFrom, 0x5200);
    EXPECT_EQ(p->objectVariables[0].pcTo, 0x5240);
    EXPECT_EQ(p->objectVariables[1].kind, GSVariableKind::SmcOperand);
    EXPECT_EQ(p->objectVariables[1].address, 0x5301);
    EXPECT_EQ(p->objectVariables[2].size, 2);
}

/// A fault names what is wrong; nothing is half-read
TEST(GSFirmwareProfile_Test, AJsonProfileFaultIsNamed)
{
    const struct
    {
        const char* json;
        const char* says;
    } cases[] = {
        {"[]", "not a JSON object"},
        {R"({"title": "no id"})", "'id' is missing"},
        {R"({"id": "x", "instances": [{"struct": "nope", "at": "#4000"}]})", "unknown struct 'nope'"},
        {R"({"id": "x", "structs": [{"name": "s", "fields": [{"name": "f", "type": "float"}]}]})", "unknown type 'float'"},
        {R"({"id": "x", "commands": [{"number": "E0", "transfer": "teleport"}]})", "transfer 'teleport'"},
        {R"({"id": "x", "variables": [{"name": "v", "at": "NOSUCHLABEL"}]})", "'NOSUCHLABEL' is not a number"},
        {R"({"id": "x", "match": [{"kind": "rom", "sha256": "12"}]})", "64 hex digits"},
    };
    for (const auto& c : cases)
    {
        SCOPED_TRACE(c.json);
        std::string error;
        EXPECT_EQ(ParseGSFirmwareProfile(c.json, "", error), nullptr);
        EXPECT_NE(error.find(c.says), std::string::npos) << error;
    }
}

/// The objects read off the card: an instance bound to a RAM page reads while the page is not mapped (M2), a nested
/// struct and an array; a register variable is valid only while the card PC is in its range (M7)
TEST(GSFirmwareProfile_Test, TheObjectsReadFromTheirPhysicalPage)
{
    FirmwareCard card("rom/gs105a.rom");
    const std::string json = R"({
      "id": "objects",
      "structs": [ { "name": "smp", "size": 8, "fields": [ {"name": "adr", "offset": 0, "type": "u16"},
                                                           {"name": "vol", "offset": 2, "type": "u8", "count": 2},
                                                           {"name": "tag", "offset": 4, "type": "bytes", "size": 2} ] },
                   { "name": "outer", "fields": [ {"name": "inner", "offset": 0, "struct": "smp"} ] } ],
      "instances": [ { "struct": "smp", "at": "#0010", "page": {"kind": "ram", "page": 6}, "count": 2 },
                     { "name": "o", "struct": "outer", "at": "#0010", "page": {"kind": "ram", "page": 6} } ],
      "variables": [ { "name": "loopHL", "kind": "register", "register": "HL", "pc": ["#0000", "#0000"] },
                     { "name": "rampg", "at": "#4000" } ]
    })";
    std::string error;
    const std::unique_ptr<GSFirmwareProfile> p = ParseGSFirmwareProfile(json, "", error);
    ASSERT_NE(p, nullptr) << error;
    auto* page6 = const_cast<uint8_t*>(card.chip->cardRamPage(6));
    ASSERT_NE(page6, nullptr);
    const uint8_t bytes[] = {0x34, 0x12, 0x40, 0x3F, 0xAB, 0xCD, 0, 0, 0x78, 0x56, 1, 2, 3, 4};
    std::copy(std::begin(bytes), std::end(bytes), page6 + 0x10);
    ASSERT_NE(card.chip->mapCardAddress(0x8000).page, 6) << "page 6 is not mapped: the read goes by the page";

    const StateNode body = GSReadFirmwareObjects(*card.chip, *p);
    const std::vector<StateNode>& rows = body.find("instances")->items[0].find("rows")->items;
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].find("at")->s, "ram 6:#0010");
    const StateNode* f0 = rows[0].find("fields");
    EXPECT_EQ(f0->find("adr")->i, 0x1234);
    ASSERT_EQ(f0->find("vol")->items.size(), 2u);
    EXPECT_EQ(f0->find("vol")->items[1].i, 0x3F);
    EXPECT_EQ(f0->find("tag")->s, "AB CD");
    EXPECT_EQ(rows[1].find("fields")->find("adr")->i, 0x5678) << "the second row is one struct size on";
    EXPECT_EQ(body.find("instances")->items[1].find("rows")->items[0].find("fields")->find("inner")->find("adr")->i, 0x1234);

    const std::vector<StateNode>& vars = body.find("variables")->items;
    ASSERT_EQ(vars.size(), 2u);
    Z80CpuRegisters regs{};
    ASSERT_TRUE(card.chip->cardRegisters(regs));
    EXPECT_EQ(vars[0].find("valid")->b, regs.pc == 0) << "a register variable outside its PC range is not valid";
    EXPECT_TRUE(vars[1].find("valid")->b) << "the fixed window is always mapped";

    const StateNode one = GSReadFirmwareObjects(*card.chip, *p, "rampg");
    EXPECT_TRUE(one.find("instances")->items.empty());
    EXPECT_EQ(one.find("variables")->items.size(), 1u);
    EXPECT_NE(GSFirmwareObjectsText(body).find("smp[1] @ram 6:#0018: adr 22136"), std::string::npos)
        << GSFirmwareObjectsText(body);
}

/// The generator's profile (tools/gsfirmware/profile_from_source.py, M8) parses, and the commands the v1.05a sources
/// dispatch to a handler are exactly the ones the built-in profile names
TEST(GSFirmwareProfile_Test, TheGeneratedProfileNamesTheBuiltinCommands)
{
    const std::filesystem::path root = TestPathHelper::FindProjectRoot();
    std::ifstream json(root / "tools/gsfirmware/gs105a.profile.json");
    std::ifstream map(root / "data/symbols/gs/gs105a.map");
    ASSERT_TRUE(json && map);
    const std::string jsonText{std::istreambuf_iterator<char>(json), std::istreambuf_iterator<char>()};
    const std::string mapText{std::istreambuf_iterator<char>(map), std::istreambuf_iterator<char>()};
    std::string error;
    const std::unique_ptr<GSFirmwareProfile> generated = ParseGSFirmwareProfile(jsonText, mapText, error);
    ASSERT_NE(generated, nullptr) << error;
    EXPECT_EQ(generated->dispatch.kind, GSDispatchKind::StockSplit);
    EXPECT_EQ(generated->comtab, 0x0300);
    EXPECT_EQ(generated->comtabh, 0xD720);

    const GSFirmwareProfile* builtin = FindGSFirmwareProfileById("gs105a");
    ASSERT_NE(builtin, nullptr);
    for (int code = 0; code < 256; code++)
        EXPECT_EQ(generated->Command(static_cast<uint8_t>(code)).name != nullptr,
                  builtin->Command(static_cast<uint8_t>(code)).name != nullptr)
            << "command #" << std::hex << code;
}

/// The dispatch view (M4): the stock split read through the card's window against the ROM, every entry with its
/// handler; the classic card runs the ROM itself, so nothing is patched; the commands without a handler are unknown
TEST(GSFirmwareProfile_Test, TheDispatchViewReadsTheStockTables)
{
    FirmwareCard card("rom/gs105a.rom");
    const GSFirmwareProfile* profile = GSFirmwareProfileOf(*card.chip);
    ASSERT_NE(profile, nullptr);
    const StateNode body = GSReadFirmwareObjects(*card.chip, *profile, "dispatch");
    const StateNode* dispatch = body.find("dispatch");
    ASSERT_NE(dispatch, nullptr);
    ASSERT_EQ(dispatch->find("entries")->items.size(), 256u);
    EXPECT_EQ(dispatch->find("patched")->i, 0);
    int64_t named = 0;
    for (int code = 0; code < 256; code++)
        named += profile->Command(static_cast<uint8_t>(code)).name ? 1 : 0;
    EXPECT_EQ(dispatch->find("unknown")->i, 256 - named);
    const StateNode& c00 = dispatch->find("entries")->items[0];
    EXPECT_EQ(c00.find("handler")->s, "#0365") << "COM00";
    EXPECT_EQ(c00.find("baseline")->s, "#0365");
    EXPECT_TRUE(body.find("instances")->items.empty()) << "only the dispatch was asked for";
}
