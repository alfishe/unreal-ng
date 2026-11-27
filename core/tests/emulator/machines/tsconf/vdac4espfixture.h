#pragma once

// Fixture for the VDAC4 ESP HLE tests: a Vdac4Esp on a test clock, driven by EspZ80Driver; a fake app runtime,
// a fake Tang LINK and a minimal loadable Xtensa ELF.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/platforms/tsconf/vdac4/vdac4appruntime.h"
#include "emulator/platforms/tsconf/vdac4/vdac4esp.h"
#include "emulator/platforms/tsconf/vdac4/vdac4espfirmware.h"
#include "emulator/platforms/tsconf/vdac4/vdac4espframes.h"
#include "emulator/platforms/tsconf/vdac4/vdac4esplink.h"
#include "emulator/platforms/tsconf/vdac4/vdac4mediarange.h"
#include "vdac4espdriver.h"

namespace Vdac4EspTest
{
/// A loadable image for EspElfImageProblem: ELF32 LE Xtensa, .text with the entry, .shstrtab
inline std::vector<uint8_t> BuildElf(size_t textSize, uint8_t fill = 0x36)
{
    const char names[] = "\0.text\0.shstrtab";  // 17 bytes with the final zero
    const size_t textOffset = 52;
    const size_t namesOffset = textOffset + textSize;
    const size_t tableOffset = (namesOffset + sizeof(names) + 3) & ~size_t(3);
    std::vector<uint8_t> elf(tableOffset + 3 * 40, 0);
    auto put16 = [&elf](size_t at, uint16_t value) {
        elf[at] = static_cast<uint8_t>(value);
        elf[at + 1] = static_cast<uint8_t>(value >> 8);
    };
    auto put32 = [&elf](size_t at, uint32_t value) {
        for (int i = 0; i < 4; ++i)
            elf[at + i] = static_cast<uint8_t>(value >> (8 * i));
    };
    elf[0] = 0x7F;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 1;  // ELFCLASS32
    elf[5] = 1;  // little-endian
    elf[6] = 1;
    put16(16, 1);   // ET_REL
    put16(18, 94);  // EM_XTENSA
    put32(20, 1);
    put32(24, 0);  // entry: .text start
    put32(32, static_cast<uint32_t>(tableOffset));
    put16(40, 52);
    put16(46, 40);
    put16(48, 3);
    put16(50, 2);
    for (size_t i = 0; i < textSize; ++i)
        elf[textOffset + i] = static_cast<uint8_t>(fill + i * 7);
    std::memcpy(elf.data() + namesOffset, names, sizeof(names));
    // [1] .text: PROGBITS, ALLOC | EXECINSTR
    const size_t text = tableOffset + 40;
    put32(text + 0, 1);
    put32(text + 4, 1);
    put32(text + 8, 6);
    put32(text + 12, 0);
    put32(text + 16, static_cast<uint32_t>(textOffset));
    put32(text + 20, static_cast<uint32_t>(textSize));
    // [2] .shstrtab: STRTAB
    const size_t strtab = tableOffset + 80;
    put32(strtab + 0, 7);
    put32(strtab + 4, 3);
    put32(strtab + 16, static_cast<uint32_t>(namesOffset));
    put32(strtab + 20, static_cast<uint32_t>(sizeof(names)));
    return elf;
}

/// Stands for an ESP app: commands 70..7F. 70 replies F7 with the data back, 71 replies and calls app_done,
/// 72 [first][last] sets the media range and replies its result, 73 replies nothing and returns an error
class FakeAppRuntime : public IVdac4AppRuntime
{
public:
    int StartApp(const std::string& module, const std::vector<uint8_t>& elf, IVdac4AppHost& host,
                 Vdac4AppInfo& info) override
    {
        ++starts;
        if (failStart)
            return kZmNoMemory;
        startedModule = module;
        startedImage = elf;
        this->host = &host;
        info.name = "FAKE";
        info.version = "1.0";
        info.commandFirst = 0x70;
        info.commandLast = 0x7F;
        running = true;
        return kZmOk;
    }
    void StopApp() override
    {
        ++stops;
        running = false;
        host = nullptr;
    }
    int Command(uint8_t command, const uint8_t* data, size_t size) override
    {
        commands.push_back(command);
        switch (command)
        {
            case 0x70:
                return host->Reply(0xF7, data, size);
            case 0x71: {
                const uint8_t bye = 1;
                host->AppDone();
                return host->Reply(0xF8, &bye, 1);
            }
            case 0x72: {
                const uint8_t result = static_cast<uint8_t>(host->SetMediaRange(data[0], data[1]));
                return host->Reply(0xF9, &result, 1);
            }
            default:
                return kZmError;
        }
    }
    void Poll(uint32_t nowMs) override { lastPollMs = nowMs; }
    void DataPiece(const uint8_t* data, size_t size) override { pieces.emplace_back(data, data + size); }
    void DataFilesChanged() override { ++dataChanges; }
    void SaveState(std::vector<uint8_t>& out) const override { out = {0xAB, static_cast<uint8_t>(commands.size())}; }
    bool LoadState(const std::string& module, const Vdac4AppInfo& info, IVdac4AppHost& host,
                   const std::vector<uint8_t>& state) override
    {
        (void)info;
        if (state.size() != 2 || state[0] != 0xAB)
            return false;
        startedModule = module;
        this->host = &host;
        running = true;
        restored = true;
        return true;
    }

    IVdac4AppHost* host = nullptr;
    std::string startedModule;
    std::vector<uint8_t> startedImage;
    std::vector<uint8_t> commands;
    std::vector<std::vector<uint8_t>> pieces;
    int dataChanges = 0;
    uint32_t lastPollMs = 0;
    int starts = 0;
    int stops = 0;
    bool running = false;
    bool failStart = false;
    bool restored = false;
};

class FakeTangLink : public IVdac4TangLink
{
public:
    bool LinkRead(uint32_t address, uint32_t& value) override
    {
        value = registers[address];
        return true;
    }
    bool LinkWrite(uint32_t address, uint32_t value) override
    {
        registers[address] = value;
        ++writes;
        return true;
    }
    std::map<uint32_t, uint32_t> registers;
    int writes = 0;
};

class Vdac4EspFixture : public ::testing::Test
{
protected:
    /// One SPI byte at 7 MHz plus the Z80's I/O around it
    static constexpr uint64_t kNsPerByte = 1500;

    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        _directory = TestPathHelper::GetUniqueTestScratchPath(std::string("vdac4esp-") + info->name());
        std::error_code ec;
        std::filesystem::remove_all(_directory, ec);
        Create();
    }

    void TearDown() override
    {
        _z80.reset();
        _esp.reset();
        std::error_code ec;
        std::filesystem::remove_all(_directory, ec);
    }

    void Create()
    {
        Vdac4Esp::Config config;
        config.cacheDirectory = _directory;
        _esp = std::make_unique<Vdac4Esp>([this]() { return _nowNs; }, config);
        _esp->SetRuntime(&_runtime);
        _esp->SetTang(&_tang);
        _z80 = std::make_unique<EspZ80Driver>(
            _esp->Spi(), [this]() { _nowNs += kNsPerByte; }, [this]() { AdvanceMs(1); });
    }

    void AdvanceMs(uint64_t ms)
    {
        _nowNs += ms * 1000000u;
        _esp->Advance();
    }

    /// Power on, wait out the boot, ZiFi_Init
    void BootAndInit()
    {
        _esp->PowerOn();
        AdvanceMs(Vdac4Esp::kDefaultBootMs);
        ASSERT_TRUE(_esp->Running());
        ASSERT_TRUE(_z80->Init());
    }

    Frame Exchange(uint8_t command, const std::vector<uint8_t>& data, uint8_t response)
    {
        Frame frame;
        EXPECT_TRUE(_z80->SendFrame(command, data));
        EXPECT_TRUE(_z80->ReceiveCommand(response, frame)) << "no response " << std::hex << int(response);
        return frame;
    }

    static std::vector<uint8_t> Le32(uint32_t value)
    {
        return {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16),
                static_cast<uint8_t>(value >> 24)};
    }

    static std::vector<uint8_t> QueryPayload(const std::string& name, uint32_t size, uint32_t stamp, bool resource = false)
    {
        std::vector<uint8_t> data{static_cast<uint8_t>(resource ? 1 : 0)};
        for (uint8_t b : Le32(size))
            data.push_back(b);
        for (uint8_t b : Le32(stamp))
            data.push_back(b);
        data.insert(data.end(), name.begin(), name.end());
        data.push_back(0);
        return data;
    }

    /// MOD_QUERY, MOD_PUT in 1000-byte pieces, MOD_COMMIT; returns the commit result
    uint8_t Deliver(const std::string& name, const std::vector<uint8_t>& image, uint32_t stamp)
    {
        Frame frame = Exchange(0x69, QueryPayload(name, static_cast<uint32_t>(image.size()), stamp), 0xE9);
        if (frame.data.empty() || frame.data[0] != 1)
            return 0xFF;
        for (size_t at = 0; at < image.size(); at += 1000)
        {
            std::vector<uint8_t> put = Le32(static_cast<uint32_t>(at));
            const size_t piece = std::min<size_t>(1000, image.size() - at);
            put.insert(put.end(), image.begin() + static_cast<std::ptrdiff_t>(at),
                       image.begin() + static_cast<std::ptrdiff_t>(at + piece));
            frame = Exchange(0x6A, put, 0xEA);
            if (frame.data.size() != 5 || frame.data[0] != 1)
                return 0xFE;
        }
        frame = Exchange(0x6B, {}, 0xEB);
        return frame.data.empty() ? 0xFD : frame.data[0];
    }

    std::string _directory;
    uint64_t _nowNs = 0;
    FakeAppRuntime _runtime;
    FakeTangLink _tang;
    std::unique_ptr<Vdac4Esp> _esp;
    std::unique_ptr<EspZ80Driver> _z80;
};
}  // namespace Vdac4EspTest
