// Bring-up: run a NEX file the way a person does - boot NextZXOS from a card folder, open the Browser, search for the file and
// run it, so NextZXOS's own NEXLOAD loads it (the direct loader of loadernex_test.cpp does not exercise that chain).
// UNREAL_NEXB=<file.nex> UNREAL_NEXB_CARD=<card folder (writable copy)> [UNREAL_NEXB_FRAMES=500] [UNREAL_NEXB_OUT=<folder>]. The file is copied into the card root as zz.nex (a file zz.* next to it is copied as well);
// UNREAL_NEXB_NAME=cube names the copy cube.nex instead (it may hold folders: "demos/cube") and UNREAL_NEXB_SEARCH=CUB is what is typed
// into the Browser's search (one text per folder level, separated by '/').

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
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

TEST(NexBrowserRun_Test, RunsTheFileOfTheEnvironmentThroughNextZxos)
{
    const char* path = std::getenv("UNREAL_NEXB");
    const char* card = std::getenv("UNREAL_NEXB_CARD");
    if (!path || !card)
        GTEST_SKIP() << "UNREAL_NEXB / UNREAL_NEXB_CARD not set";
    namespace fs = std::filesystem;
    const std::string copyName = std::getenv("UNREAL_NEXB_NAME") ? std::getenv("UNREAL_NEXB_NAME") : "zz";
    const std::string search = std::getenv("UNREAL_NEXB_SEARCH") ? std::getenv("UNREAL_NEXB_SEARCH") : "Z";
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
    auto inked = [&]() {
        const uint8_t* screen = context->pMemory->RAMPageAddress(5);
        int n = 0;
        for (int i = 0; i < 0x1800; i++)
            n += screen[i] != 0 ? 1 : 0;
        return n;
    };
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
    emulator->EnableTurboMode();
    int waited = 0;
    for (; waited < 3000 && !(z80->halted && inked() > 2000); waited++)
        emulator->RunFrame(true);
    ASSERT_LT(waited, 3000) << "NextZXOS did not reach its welcome screen";
    wait(50);
    tap(ZXKEY_SPACE, 8);
    wait(150);
    const bool direct = std::getenv("UNREAL_NEXB_DIRECT") != nullptr;
    if (!direct)
    {
        tap(ZXKEY_B);
        wait(250);
        // the search text is one text per folder level, separated by '/': each is typed into the Browser's search (H) and entered
        // (a space is the SPACE key, "." and "-" are Symbol Shift + M / J); the last one selects the file and its ENTER comes after the logs are armed
        for (size_t at = 0; at <= search.size();)
        {
            const size_t end = std::min(search.find('/', at), search.size());
            tap(ZXKEY_H);
            wait(40);
            for (size_t i = at; i < end; i++)
            {
                const char c = search[i];
                tap(c == ' ' ? ZXKEY_SPACE : c == '.' ? ZXKEY_EXT_DOT : c == '-' ? ZXKEY_EXT_MINUS : static_cast<ZXKeysEnum>(std::toupper(c)));
                wait(40);
            }
            tap(ZXKEY_ENTER);
            wait(40);
            if (end < search.size())  // a folder: the second ENTER opens it
            {
                tap(ZXKEY_ENTER);
                wait(150);
            }
            at = end + 1;
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
        tap(ZXKEY_ENTER);
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
