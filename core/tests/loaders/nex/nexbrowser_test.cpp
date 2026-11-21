// Bring-up: run a NEX file the way a person does - boot NextZXOS from a card folder, open the Browser, search for the file and
// run it, so NextZXOS's own NEXLOAD loads it (the direct loader of loadernex_test.cpp does not exercise that chain).
// UNREAL_NEXB=<file.nex> UNREAL_NEXB_CARD=<card folder (writable copy)> [UNREAL_NEXB_FRAMES=500] [UNREAL_NEXB_OUT=<folder>]. The file is copied into the card root as zz.nex (a file zz.* next to it is copied as well);
// UNREAL_NEXB_NAME=cube names the copy cube.nex instead (it may hold folders: "demos/cube"). Each folder level and the file are found
// the way tools/machines/next/browser-drive and regress find them (the search with the shortest unique prefix of the card's listing,
// else counting down); UNREAL_NEXB_SEARCH=CUB types that text instead (one text per folder level, separated by '/').
// The keys, hold frames, waits, the idle loop and the rules come from testdata/machines/zxnext/browser-navigation.json, the copy the
// Python tools read too; the run steps of the file's extension (.nex ENTER, .sna ENTER + ENTER, .tap SYMBOL SHIFT + ENTER, N) as well.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <string>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#ifndef _RYML_SINGLE_HEADER_AMALGAMATED_HPP_
#include "3rdparty/rapidyaml/ryml_all.hpp"
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/z80n/nextregjournal.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/screen.h"
#include "loaders/nex/loadernex.h"

namespace
{
/// One step of browser-navigation.json: a key (or keys held together) for hold frames, then after frames; or frames alone; then,
/// with idle, frames in steps until NextZXOS waits for a key (at most idleFrames), plus settle frames
struct NavStep
{
    std::string key;
    std::vector<std::string> combo;
    int hold = 0;
    int after = 0;
    int frames = 0;
    bool idle = false;
    int idleFrames = 0;
    int idleStep = 5;
    int idleSettle = 0;
};

/// The NextZXOS Browser navigation of testdata/machines/zxnext/browser-navigation.json (shared with the Python drivers:
/// tools/machines/next/browser-drive/navigation.py)
class BrowserNavigation
{
public:
    explicit BrowserNavigation(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        _text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        _tree = ryml::parse_json_in_arena(ryml::to_csubstr(_text));
        const ryml::ConstNodeRef root = _tree.crootref();
        idlePc = static_cast<uint16_t>(std::stoul(Text(root["idle"]["pc"]), nullptr, 16));
        searchable = std::regex(Text(root["search"]["searchable"]));
        dotsAtRoot = Text(root["listing"]["dot_entries_at_root"]) == "true";
        for (const ryml::ConstNodeRef dot : root["listing"]["dot_entries"].children())
            dots.push_back(Text(dot));
        typeDelay = Int(root["steps"]["type"]["delay"]);
        typeFramesPerChar = Int(root["steps"]["type"]["frames_per_char"]);
        typeAfter = Int(root["steps"]["type"]["after"]);
        typeIdle = Read(root["steps"]["type"]);
    }

    NavStep Step(const std::string& name) const { return Read(_tree.crootref()["steps"][ryml::to_csubstr(name)]); }

    /// The steps that run a file of extension @p ext (without the dot, lower case); '*' for the others
    std::vector<NavStep> Run(const std::string& ext) const
    {
        const ryml::ConstNodeRef run = _tree.crootref()["run"];
        const ryml::ConstNodeRef list = run.has_child(ryml::to_csubstr(ext)) ? run[ryml::to_csubstr(ext)] : run["*"];
        std::vector<NavStep> steps;
        for (const ryml::ConstNodeRef step : list.children())
            steps.push_back(Read(step));
        return steps;
    }

    /// The Browser's listing of host folder @p folder: the dot entries (not at the card root), then the names case-insensitively
    std::vector<std::string> Entries(const std::filesystem::path& folder, bool root) const
    {
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::directory_iterator(folder))
            names.push_back(entry.path().filename().string());
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
        std::vector<std::string> listing = root && !dotsAtRoot ? std::vector<std::string>{} : dots;
        listing.insert(listing.end(), names.begin(), names.end());
        return listing;
    }

    /// The shortest prefix of @p name's searchable part only one entry of @p listing starts with; empty: count down
    std::string SearchPrefix(const std::vector<std::string>& listing, const std::string& name) const
    {
        std::smatch match;
        if (!std::regex_search(name, match, searchable) || match.position(0) != 0)
            return "";
        const std::string text = match.str(0);
        for (size_t length = 1; length <= text.size(); length++)
        {
            const std::string prefix = Lower(text.substr(0, length));
            const auto count = std::count_if(listing.begin(), listing.end(),
                                             [&](const std::string& e) { return Lower(e).compare(0, prefix.size(), prefix) == 0; });
            if (count == 1)
                return text.substr(0, length);
        }
        return "";
    }

    static std::string Lower(std::string text)
    {
        for (char& c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    uint16_t idlePc = 0;
    std::regex searchable;
    bool dotsAtRoot = false;
    std::vector<std::string> dots;
    int typeDelay = 0;
    int typeFramesPerChar = 0;
    int typeAfter = 0;
    NavStep typeIdle;

private:
    static std::string Text(const ryml::ConstNodeRef node)
    {
        const ryml::csubstr value = node.val();
        return std::string(value.str ? value.str : "", value.len);
    }

    static int Int(const ryml::ConstNodeRef node) { return std::stoi(Text(node)); }

    static NavStep Read(const ryml::ConstNodeRef node)
    {
        NavStep step;
        if (node.has_child("key"))
            step.key = Text(node["key"]);
        if (node.has_child("combo"))
            for (const ryml::ConstNodeRef key : node["combo"].children())
                step.combo.push_back(Text(key));
        if (node.has_child("hold"))
            step.hold = Int(node["hold"]);
        if (node.has_child("after"))
            step.after = Int(node["after"]);
        if (node.has_child("frames"))
            step.frames = Int(node["frames"]);
        if (node.has_child("idle"))
        {
            const ryml::ConstNodeRef idle = node["idle"];
            step.idle = true;
            step.idleFrames = Int(idle["frames"]);
            if (idle.has_child("step"))
                step.idleStep = Int(idle["step"]);
            if (idle.has_child("settle"))
                step.idleSettle = Int(idle["settle"]);
        }
        return step;
    }

    std::string _text;
    ryml::Tree _tree;
};
}  // namespace

TEST(NexBrowserRun_Test, RunsTheFileOfTheEnvironmentThroughNextZxos)
{
    const char* path = std::getenv("UNREAL_NEXB");
    const char* card = std::getenv("UNREAL_NEXB_CARD");
    if (!path || !card)
        GTEST_SKIP() << "UNREAL_NEXB / UNREAL_NEXB_CARD not set";
    namespace fs = std::filesystem;
    const std::string navPath = TestPathHelper::GetTestDataPath("machines/zxnext/browser-navigation.json");
    ASSERT_TRUE(fs::exists(navPath)) << navPath;
    const BrowserNavigation nav(navPath);
    const std::string copyName = std::getenv("UNREAL_NEXB_NAME") ? std::getenv("UNREAL_NEXB_NAME") : "zz";
    const char* search = std::getenv("UNREAL_NEXB_SEARCH");
    if (!std::getenv("UNREAL_NEXB_INPLACE"))  // INPLACE: the card already holds the file, nothing is copied
    {
        fs::create_directories((fs::path(card) / copyName).parent_path());
        fs::copy_file(path, fs::path(card) / (copyName + fs::path(path).extension().string()), fs::copy_options::overwrite_existing);
        for (const auto& entry : fs::directory_iterator(fs::path(path).parent_path()))
            if (entry.path().extension() != fs::path(path).extension() && entry.is_regular_file())
                fs::copy_file(entry.path(), fs::path(card) / entry.path().filename(), fs::copy_options::overwrite_existing);
    }
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero, [&](CONFIG& config) {
        std::strncpy(config.next_boot_rom_path, "rom/next/nextboot.rom", sizeof config.next_boot_rom_path - 1);
        std::strncpy(config.next_sd_path, card, sizeof config.next_sd_path - 1);
    });
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    auto tap = [&](ZXKeysEnum key, int frames = 6) {
        context->pKeyboard->PressKey(key);
        for (int i = 0; i < frames; i++)
            emulator->RunFrame(true);
        context->pKeyboard->ReleaseKey(key);
    };
    auto wait = [&](int frames) {
        for (int i = 0; i < frames; i++)
            emulator->RunFrame(true);
    };
    // The navigation runs frame by frame through the keyboard queue, the way regress.py drives it over the WebAPI
    DebugKeyboardManager* keyboard = context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(keyboard, nullptr);
    auto runFrames = [&](int frames) {
        if (frames > 0)
            emulator->RunNFrames(static_cast<unsigned>(frames), true);
    };
    auto runUntilIdle = [&](int limit, int step, int settle) {
        for (int ran = 0; ran < limit; ran += step)
        {
            runFrames(step);
            if (z80->halted && z80->pc == nav.idlePc)
            {
                runFrames(settle);
                return true;
            }
        }
        return false;
    };
    auto doStep = [&](const NavStep& step) {
        if (!step.key.empty())
            keyboard->TapKeyRepeated(step.key, static_cast<uint16_t>(step.hold), 1, 0);
        else if (!step.combo.empty())
            keyboard->TapComboRepeated(step.combo, static_cast<uint16_t>(step.hold), 1, 0);
        if (!step.key.empty() || !step.combo.empty())
            runFrames(step.hold + step.after);
        else
            runFrames(step.frames);
        return !step.idle || runUntilIdle(step.idleFrames, step.idleStep, step.idleSettle);
    };
    auto typeSearch = [&](const std::string& text) {
        doStep(nav.Step("search"));
        ASSERT_TRUE(keyboard->TypeText(text, static_cast<uint16_t>(nav.typeDelay)).empty()) << "cannot type " << text;
        runFrames(static_cast<int>(text.size()) * nav.typeFramesPerChar + nav.typeAfter);
        runUntilIdle(nav.typeIdle.idleFrames, nav.typeIdle.idleStep, nav.typeIdle.idleSettle);
        doStep(nav.Step("accept"));
    };
    // The cursor onto @p name of host folder @p folder: H + the shortest unique prefix, else counting down
    auto reach = [&](const fs::path& folder, const std::string& name, bool root) {
        const std::vector<std::string> listing = nav.Entries(folder, root);
        const std::string prefix = nav.SearchPrefix(listing, name);
        if (!prefix.empty())
        {
            typeSearch(prefix);
            return "search:" + prefix;
        }
        const auto at = std::find(listing.begin(), listing.end(), name);
        EXPECT_NE(at, listing.end()) << name << " is not in " << folder;
        for (auto i = listing.begin(); i != at; ++i)
            doStep(nav.Step("down"));
        return "down:" + std::to_string(at - listing.begin());
    };
    const std::string fileName = fs::path(copyName).filename().string() + fs::path(path).extension().string();
    emulator->EnableTurboMode();
    ASSERT_TRUE(doStep(nav.Step("boot"))) << "NextZXOS did not reach its welcome screen";
    doStep(nav.Step("menu"));
    const bool direct = std::getenv("UNREAL_NEXB_DIRECT") != nullptr;
    if (!direct)
    {
        doStep(nav.Step("browser"));
        if (search)
        {
            // UNREAL_NEXB_SEARCH: one text per folder level, separated by '/', each typed into the search and accepted; a folder is
            // opened, the last one selects the file (its run steps come after the logs are armed). Letters are typed as the plain
            // letter keys (no CAPS SHIFT), as before the shared navigation
            const std::string texts = BrowserNavigation::Lower(search);
            for (size_t at = 0; at <= texts.size();)
            {
                const size_t end = std::min(texts.find('/', at), texts.size());
                typeSearch(texts.substr(at, end - at));
                if (end < texts.size())
                    doStep(nav.Step("open"));
                at = end + 1;
            }
        }
        else
        {
            // every folder level of the copy's name and then the file, found in the card's listing
            fs::path folder = card;
            for (const fs::path& part : fs::path(copyName).parent_path())
            {
                std::cout << "Browser " << part.string() << ": " << reach(folder, part.string(), folder == fs::path(card)) << std::endl;
                doStep(nav.Step("open"));
                folder /= part;
            }
            std::cout << "Browser " << fileName << ": " << reach(folder, fileName, folder == fs::path(card)) << std::endl;
        }
    }
    emulator->DisableTurboMode();
    std::vector<NextRegWrite> nrLog;
    uint16_t logPc = 0;
    auto* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    if (std::getenv("UNREAL_NEXB_NRLOG") && decoder)
        decoder->Board().SetWriteLog(&nrLog, &logPc);
    std::map<uint16_t, PortDecoder_Next::PortUse> portsOut, portsIn;
    if (std::getenv("UNREAL_NEXB_PORTS") && decoder)
        decoder->SetPortLog(&portsIn, &portsOut);
    context->pFeatureManager->setFeature(Features::kScreenHQ, true);
    if (direct)
    {
        LoaderNex loader(context);
        ASSERT_TRUE(loader.LoadFile(path)) << loader.Error();
    }
    else
    {
        if (const char* j = std::getenv("UNREAL_NEXB_JITTER"))
            wait(std::atoi(j));
        if (const char* out = std::getenv("UNREAL_NEXB_OUT"))  // the Browser as it is when ENTER runs the file
        {
            wait(2);
            const FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
            std::ofstream file(std::string(out) + "/pre.rgba", std::ios::binary);
            const uint32_t dims[2] = {fb.width, fb.height};
            file.write(reinterpret_cast<const char*>(dims), sizeof dims);
            file.write(reinterpret_cast<const char*>(fb.memoryBuffer), fb.memoryBufferSize);
        }
        std::string ext = fs::path(fileName).extension().string();
        ext = BrowserNavigation::Lower(ext.empty() ? ext : ext.substr(1));
        for (const NavStep& step : nav.Run(ext))
            doStep(step);
    }
    // UNREAL_NEXB_AFTER="300:R,20:ENTER": wait that many frames, then tap the key (a letter, or ENTER / SPACE)
    if (const char* after = std::getenv("UNREAL_NEXB_AFTER"))
    {
        std::string list = after;
        for (size_t at = 0; at < list.size();)
        {
            const size_t end = list.find(',', at);
            const std::string item = list.substr(at, end == std::string::npos ? std::string::npos : end - at);
            const size_t colon = item.find(':');
            wait(std::atoi(item.c_str()));
            const std::string key = item.substr(colon + 1);
            tap(key == "ENTER" ? ZXKEY_ENTER : key == "SPACE" ? ZXKEY_SPACE : static_cast<ZXKeysEnum>(std::toupper(key[0])));
            if (end == std::string::npos)
                break;
            at = end + 1;
        }
    }
    const int total = std::getenv("UNREAL_NEXB_FRAMES") ? std::atoi(std::getenv("UNREAL_NEXB_FRAMES")) : 500;
    const int every = std::getenv("UNREAL_NEXB_TRACE") ? std::atoi(std::getenv("UNREAL_NEXB_TRACE")) : 0;
    for (int i = 0; i < total; i++)
    {
        emulator->RunFrame(true);
        if (std::getenv("UNREAL_NEXB_SAMPLE") && i % std::atoi(std::getenv("UNREAL_NEXB_SAMPLE")) == 0)
        {
            const FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
            const uint32_t* px = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
            unsigned lit = 0;
            for (size_t k = 0; k < fb.memoryBufferSize / 4; k++)
                lit += (px[k] & 0xFFFFFF) != 0;
            unsigned l2 = 0;
            for (unsigned b = 9; b < 15; b++)
                for (unsigned k = 0; k < 16384; k++)
                    l2 += context->pMemory->RAMPageAddress(static_cast<uint16_t>(b))[k] != 0;
            std::cout << "S" << i << " l2 " << l2 << " lit " << lit << " pc " << std::hex << z80->pc << std::dec << std::endl;
        }
        if (every && i % every == 0)
            std::cout << "f" << i << " pc " << std::hex << z80->pc << " sp " << z80->sp << " halted " << int(z80->halted) << " speed "
                      << int(decoder->Board().Stored(7)) << " mmu";
        if (every && i % every == 0)
        {
            auto* mem = dynamic_cast<NextMemory*>(context->pMemory);
            for (unsigned s = 0; s < 8; s++)
                std::cout << " " << int(mem->GetMmu(s));
            std::cout << std::dec << std::endl;
        }
    }
    if (!nrLog.empty())
    {
        // UNREAL_NEXB_NRLOG: "1" = every write (the first 400, or the last 200 with UNREAL_NEXB_NRTAIL), else the registers to show (hex list)
        const std::string wanted = std::getenv("UNREAL_NEXB_NRLOG");
        std::cout << "NRLOG " << nrLog.size() << " writes:";
        unsigned shown = 0, count[256] = {};
        for (const NextRegWrite& write : nrLog)
            count[write.reg]++;
        const size_t from = wanted == "1" && std::getenv("UNREAL_NEXB_NRTAIL") ? (nrLog.size() > 200 ? nrLog.size() - 200 : 0) : 0;
        for (size_t i = from; i < nrLog.size() && shown < 400; i++)
        {
            char one[8];
            std::snprintf(one, sizeof one, ",%x,", nrLog[i].reg);
            if (wanted != "1" && (',' + wanted + ',').find(one) == std::string::npos)
                continue;
            std::cout << " " << std::hex << int(nrLog[i].reg) << "=" << int(nrLog[i].value);
            shown++;
        }
        std::cout << std::dec << std::endl << "NRCOUNT";
        for (unsigned r = 0; r < 256; r++)
            if (count[r])
                std::cout << " " << std::hex << r << "x" << std::dec << count[r];
        std::cout << std::endl;
        if (decoder)
            for (unsigned r : {0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u, 0x4Au, 0x69u, 0x6Bu, 0x70u})
                std::cout << " [stored " << std::hex << r << "=" << int(decoder->Board().Stored(r)) << "]";
        std::cout << std::dec << std::endl;
    }
    if (std::getenv("UNREAL_NEXB_BANKS"))
        for (unsigned b = 0; b < 32; b++)
        {
            unsigned nz = 0;
            for (unsigned i = 0; i < 16384; i++)
                nz += context->pMemory->RAMPageAddress(static_cast<uint16_t>(b))[i] != 0;
            std::cout << "BANK " << b << " nonzero " << nz << std::endl;
        }
    for (const auto* log : {&portsOut, &portsIn})
    {
        std::cout << (log == &portsOut ? "PORTS OUT" : "PORTS IN") << ":";
        for (const auto& [port, use] : *log)
            std::cout << " " << std::hex << port << "x" << std::dec << use.count << "=" << std::hex << int(use.last);
        std::cout << std::dec << std::endl;
    }
    std::cout << "PC " << std::hex << z80->pc << " sp " << z80->sp << std::dec << " iff1 " << int(z80->iff1) << " im " << int(z80->im) << std::endl;
    if (decoder)
        std::cout << "DIVMMC mapped " << decoder->DivMmc().Mapped() << " automapped " << decoder->DivMmc().Automapped() << " slot0 " << dynamic_cast<NextMemory*>(context->pMemory)->SlotKind(0)
                  << " e3 " << std::hex << int(decoder->DivMmc().ReadPort()) << " nr0a " << int(decoder->Board().Stored(0x0A)) << " nrb8-bb " << int(decoder->Board().Stored(0xB8)) << " "
                  << int(decoder->Board().Stored(0xB9)) << " " << int(decoder->Board().Stored(0xBA)) << " " << int(decoder->Board().Stored(0xBB)) << " nr06 " << int(decoder->Board().Stored(0x06)) << " nr08 "
                  << int(decoder->Board().Stored(0x08)) << std::dec << std::endl;
    if (const char* out = std::getenv("UNREAL_NEXB_OUT"))
    {
        std::ofstream ram(std::string(out) + "/ram.bin", std::ios::binary);
        for (unsigned a = 0; a < 0x10000; a++)
        {
            const char b = static_cast<char>(context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(a)));
            ram.write(&b, 1);
        }
        const FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
        std::ofstream file(std::string(out) + "/frame.rgba", std::ios::binary);
        const uint32_t dims[2] = {fb.width, fb.height};
        file.write(reinterpret_cast<const char*>(dims), sizeof dims);
        file.write(reinterpret_cast<const char*>(fb.memoryBuffer), fb.memoryBufferSize);
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}
