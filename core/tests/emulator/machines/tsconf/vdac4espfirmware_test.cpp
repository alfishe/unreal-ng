// VDAC4 ESP32-S3 HLE, firmware level: Application::handle and ModuleApp commands over the real link, with a fake
// app runtime and a fake Tang LINK. Driven only through SpiDevice select / exchange (vdac4espdriver.h).

#include "vdac4espfixture.h"

#include <algorithm>
#include <string>

using namespace Vdac4EspTest;

class Vdac4EspFirmware_Test : public Vdac4EspFixture
{
protected:
    static std::vector<uint8_t> Name(const std::string& name)
    {
        std::vector<uint8_t> data(name.begin(), name.end());
        data.push_back(0);
        return data;
    }

    static std::string Text(const Frame& frame) { return std::string(frame.data.begin(), frame.data.end()); }
};

TEST_F(Vdac4EspFirmware_Test, SysInfoAcksThenReportsAndSetsStep8)
{
    BootAndInit();
    ASSERT_TRUE(_z80->SendFrame(0x02));
    Frame frame;
    ASSERT_TRUE(_z80->ReceiveFrame(frame));
    EXPECT_EQ(frame.command, 0xFE);
    ASSERT_TRUE(_z80->ReceiveFrame(frame));
    EXPECT_EQ(frame.command, 0x82);
    const std::string text = Text(frame);
    EXPECT_EQ(text.rfind("RAM:", 0), 0u);
    EXPECT_NE(text.find("BOARD:VDAC4-A3.1"), std::string::npos);
    EXPECT_NE(text.find("CORE:SPI+"), std::string::npos);
    EXPECT_EQ(text.substr(text.size() - 30), "FW:vdac4-0.1.174-unreal-ng-hle");
    frame = Exchange(0x05, {}, 0x85);
    ASSERT_EQ(frame.data.size(), 1u);
    EXPECT_EQ(frame.data[0], 8);
}

TEST_F(Vdac4EspFirmware_Test, EchoAndUnsupportedAndGetStep)
{
    BootAndInit();
    Frame frame = Exchange(0x00, {1, 2, 3}, 0x00);
    EXPECT_EQ(frame.data, (std::vector<uint8_t>{1, 2, 3}));
    frame = Exchange(0x30, {}, 0xEE);  // the audio player is not emulated
    EXPECT_EQ(Text(frame), "unsupported:30");
    frame = Exchange(0x05, {}, 0x85);
    EXPECT_EQ(frame.data[0], 0x30);
    EXPECT_EQ(std::string(frame.data.begin() + 1, frame.data.end()), "unsupported:30");
}

/// Network commands: ACK, the error, and the firmware's failure response
TEST_F(Vdac4EspFirmware_Test, NetworkCommandsFailAsWithoutNetwork)
{
    BootAndInit();
    ASSERT_TRUE(_z80->SendFrame(0x01, {'s', 0, 'p', 0}));
    Frame frame;
    ASSERT_TRUE(_z80->ReceiveFrame(frame));
    EXPECT_EQ(frame.command, 0xFE);
    ASSERT_TRUE(_z80->ReceiveFrame(frame));
    EXPECT_EQ(frame.command, 0xEE);
    ASSERT_TRUE(_z80->ReceiveFrame(frame));
    EXPECT_EQ(frame.command, 0x81);
    EXPECT_EQ(frame.data, std::vector<uint8_t>(5, 0));
    // NET_RECV: no ACK, result 1
    ASSERT_TRUE(_z80->SendFrame(0x12, {0}));
    ASSERT_TRUE(_z80->ReceiveFrame(frame));
    EXPECT_EQ(frame.command, 0xEE);
    ASSERT_TRUE(_z80->ReceiveFrame(frame));
    EXPECT_EQ(frame.command, 0x92);
    EXPECT_EQ(frame.data, std::vector<uint8_t>{1});
}

TEST_F(Vdac4EspFirmware_Test, SysResetRestartsWithNewNonce)
{
    BootAndInit();
    ASSERT_EQ(Deliver("APP.ELF", BuildElf(200), 1), 1);
    ASSERT_EQ(Exchange(0x6C, Name("APP.ELF"), 0xEC).data[0], 0);
    const uint8_t nonce = _esp->BootNonce();
    ASSERT_TRUE(_z80->SendFrame(0x0F));
    Frame frame;
    ASSERT_TRUE(_z80->ReceiveFrame(frame));
    EXPECT_EQ(frame.command, 0xFE);
    AdvanceMs(1);  // the ring is empty: 100 ms to go
    EXPECT_EQ(_esp->GetPhase(), Vdac4Esp::Phase::RestartWait);
    AdvanceMs(Vdac4Esp::kResetDelayMs);
    EXPECT_EQ(_esp->GetPhase(), Vdac4Esp::Phase::Booting);
    EXPECT_FALSE(_runtime.running);
    EXPECT_EQ(_z80->ReadState(), 0xFF);
    AdvanceMs(Vdac4Esp::kDefaultBootMs);
    ASSERT_TRUE(_esp->Running());
    EXPECT_NE(_esp->BootNonce(), nonce);
    EXPECT_EQ(_esp->Boots(), 2u);
    EXPECT_EQ(_z80->ReadStateWord()[3], _esp->BootNonce());
    ASSERT_TRUE(_z80->Init());
    Exchange(0x04, {}, 0xF0);
    // The cache is flash: the module is still there
    EXPECT_EQ(Exchange(0x69, QueryPayload("APP.ELF", static_cast<uint32_t>(BuildElf(200).size()), 1), 0xE9).data[0], 0);
}

/// SYS_RESET waits for the Z80 to read the ACK (at most 1 s) before the 100 ms
TEST_F(Vdac4EspFirmware_Test, SysResetWaitsForTheRingToDrain)
{
    BootAndInit();
    ASSERT_TRUE(_z80->SendFrame(0x0F));
    AdvanceMs(500);
    EXPECT_EQ(_esp->GetPhase(), Vdac4Esp::Phase::Flushing);  // the ACK is unread
    AdvanceMs(500);
    EXPECT_EQ(_esp->GetPhase(), Vdac4Esp::Phase::RestartWait);
}

TEST_F(Vdac4EspFirmware_Test, ModuleRoundTripWithFakeRuntime)
{
    BootAndInit();
    const std::vector<uint8_t> image = BuildElf(2500);
    ASSERT_EQ(Deliver("player.elf", image, 0x5A215A21), 1);
    // A second query of the same name, size and time: cached
    Frame frame = Exchange(0x69, QueryPayload("PLAYER.ELF", static_cast<uint32_t>(image.size()), 0x5A215A21), 0xE9);
    EXPECT_EQ(frame.data[0], 0);
    // Another time stamp: send it again
    frame = Exchange(0x69, QueryPayload("PLAYER.ELF", static_cast<uint32_t>(image.size()), 1), 0xE9);
    EXPECT_EQ(frame.data[0], 1);

    frame = Exchange(0x6C, Name("player.elf"), 0xEC);
    ASSERT_EQ(frame.data.size(), 1u + 5u + 4u);
    EXPECT_EQ(static_cast<int8_t>(frame.data[0]), kZmOk);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(frame.data.data() + 1)), "FAKE");
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(frame.data.data() + 6)), "1.0");
    EXPECT_EQ(_runtime.startedModule, "PLAYER.ELF");
    EXPECT_EQ(_runtime.startedImage, image);
    EXPECT_EQ(_runtime.starts, 1);
    // APP_START again: the same app, nothing changed, no restart
    Exchange(0x6C, Name("PLAYER.ELF"), 0xEC);
    EXPECT_EQ(_runtime.starts, 1);

    frame = Exchange(0x70, {9, 8, 7}, 0xF7);
    EXPECT_EQ(frame.data, (std::vector<uint8_t>{9, 8, 7}));
    frame = Exchange(0x6D, {}, 0xED);
    EXPECT_EQ(frame.data, std::vector<uint8_t>{1});
    EXPECT_EQ(_runtime.stops, 1);
    EXPECT_FALSE(_esp->Firmware().AppRunning());
    frame = Exchange(0x70, {}, 0xEE);
    EXPECT_EQ(Text(frame), "unsupported:70");
    // A module not in the cache
    frame = Exchange(0x6C, Name("NONE.ELF"), 0xEC);
    EXPECT_EQ(static_cast<int8_t>(frame.data[0]), kZmNotFound);
}

TEST_F(Vdac4EspFirmware_Test, CorruptElfIsNotCached)
{
    BootAndInit();
    std::vector<uint8_t> image = BuildElf(600);
    image[18] = 3;  // EM_386
    EXPECT_EQ(Deliver("BAD.ELF", image, 2), 0);
    EXPECT_EQ(Exchange(0x69, QueryPayload("BAD.ELF", static_cast<uint32_t>(image.size()), 2), 0xE9).data[0], 1);
    image = BuildElf(600);
    image.resize(100);  // section table cut off
    EXPECT_EQ(Deliver("CUT.ELF", image, 2), 0);
    EXPECT_EQ(_esp->Firmware().Cache().Count(), 0u);
}

TEST_F(Vdac4EspFirmware_Test, QueryAndPutErrors)
{
    BootAndInit();
    EXPECT_EQ(Exchange(0x69, QueryPayload("TOO.LONGNAME", 100, 0), 0xE9).data[0], 3);
    EXPECT_EQ(Exchange(0x69, QueryPayload("A*B.ELF", 100, 0), 0xE9).data[0], 3);
    EXPECT_EQ(Exchange(0x69, QueryPayload("ZERO.ELF", 0, 0), 0xE9).data[0], 3);
    EXPECT_EQ(Exchange(0x69, {0, 1, 2}, 0xE9).data[0], 3);
    EXPECT_EQ(Exchange(0x69, QueryPayload("HUGE.ELF", Vdac4EspFirmware::kMaxModuleBytes + 1, 0), 0xE9).data[0], 2);
    // No delivery open: a put is refused
    std::vector<uint8_t> put = Le32(0);
    put.push_back(1);
    Frame frame = Exchange(0x6A, put, 0xEA);
    EXPECT_EQ(frame.data[0], 0);
    // Open one of 10 bytes
    ASSERT_EQ(Exchange(0x69, QueryPayload("TEN.ELF", 10, 0), 0xE9).data[0], 1);
    put = Le32(4);  // not where it stands (0)
    put.push_back(1);
    frame = Exchange(0x6A, put, 0xEA);
    EXPECT_EQ(frame.data[0], 0);
    EXPECT_EQ(frame.data[1], 0);
    put = Le32(0);
    put.insert(put.end(), 11, 0xAA);  // longer than the rest
    EXPECT_EQ(Exchange(0x6A, put, 0xEA).data[0], 0);
    put = Le32(0);
    put.insert(put.end(), 6, 0xAA);
    frame = Exchange(0x6A, put, 0xEA);
    EXPECT_EQ(frame.data[0], 1);
    EXPECT_EQ(frame.data[1], 6);
    // Commit of an incomplete delivery
    EXPECT_EQ(Exchange(0x6B, {}, 0xEB).data[0], 0);
}

/// The app's commands reach the runtime; a reply works only inside one; frames and events at any time
TEST_F(Vdac4EspFirmware_Test, AppCommandsAndAsyncFrames)
{
    BootAndInit();
    ASSERT_EQ(Deliver("APP.ELF", BuildElf(300), 1), 1);
    ASSERT_EQ(Exchange(0x6C, Name("APP.ELF"), 0xEC).data[0], 0);
    ASSERT_NE(_runtime.host, nullptr);
    EXPECT_EQ(_runtime.host->Reply(0xF7, nullptr, 0), kZmError);  // not inside a command
    // A frame from the runtime between Z80 accesses
    const uint8_t data[] = {1, 2, 3, 4};
    ASSERT_TRUE(_runtime.host->SendFrame(0xFA, data, sizeof(data)));
    Frame frame;
    ASSERT_TRUE(_z80->ReceiveCommand(0xFA, frame));
    EXPECT_EQ(frame.data, std::vector<uint8_t>(data, data + 4));
    // Events: 63 bytes at most, 8 deep, one per firmware pass
    EXPECT_EQ(_runtime.host->SendEvent(0x66, data, 64), kZmBusy);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(_runtime.host->SendEvent(0x66, data, 1), kZmOk);
    EXPECT_EQ(_runtime.host->SendEvent(0x66, data, 1), kZmBusy);
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_TRUE(_z80->ReceiveFrame(frame));
        EXPECT_EQ(frame.command, 0x66);
    }
    // Media range outside the app's commands: refused
    frame = Exchange(0x72, {0x60, 0x75}, 0xF9);
    EXPECT_EQ(static_cast<int8_t>(frame.data[0]), kZmError);
    frame = Exchange(0x72, {0x74, 0x75}, 0xF9);
    EXPECT_EQ(frame.data[0], 0);
    EXPECT_TRUE(_esp->MediaRange().Contains(0x75));
    // An error from the app: "unsupported"
    EXPECT_EQ(Text(Exchange(0x73, {}, 0xEE)), "unsupported:73");
    // app_done inside a command: the reply goes, then the app stops (media range cleared)
    frame = Exchange(0x71, {}, 0xF8);
    EXPECT_EQ(frame.data, std::vector<uint8_t>{1});
    EXPECT_FALSE(_esp->Firmware().AppRunning());
    EXPECT_FALSE(_esp->MediaRange().Contains(0x75));
    EXPECT_EQ(_runtime.stops, 1);
    EXPECT_GT(_runtime.lastPollMs, 0u);
}

/// A runtime that refuses to start: APP_START reports its result, nothing runs
TEST_F(Vdac4EspFirmware_Test, FailedStartReportsResult)
{
    BootAndInit();
    ASSERT_EQ(Deliver("APP.ELF", BuildElf(300), 1), 1);
    _runtime.failStart = true;
    const Frame frame = Exchange(0x6C, Name("APP.ELF"), 0xEC);
    EXPECT_EQ(static_cast<int8_t>(frame.data[0]), kZmNoMemory);
    EXPECT_EQ(frame.data.size(), 3u);  // empty name and version
    EXPECT_FALSE(_esp->Firmware().AppRunning());
}

/// A new copy of the running app's module: the next APP_START restarts it
TEST_F(Vdac4EspFirmware_Test, NewCopyOfRunningModuleRestarts)
{
    BootAndInit();
    ASSERT_EQ(Deliver("APP.ELF", BuildElf(300), 1), 1);
    ASSERT_EQ(Exchange(0x6C, Name("APP.ELF"), 0xEC).data[0], 0);
    ASSERT_EQ(Deliver("APP.ELF", BuildElf(320), 2), 1);
    ASSERT_EQ(Exchange(0x6C, Name("APP.ELF"), 0xEC).data[0], 0);
    EXPECT_EQ(_runtime.starts, 2);
    EXPECT_EQ(_runtime.startedImage.size(), BuildElf(320).size());
}

TEST_F(Vdac4EspFirmware_Test, ResourcesHeldInPsram)
{
    BootAndInit();
    std::vector<uint8_t> bank(3000, 0x42);
    ASSERT_EQ(Exchange(0x69, QueryPayload("BANK.SF2", 3000, 9, true), 0xE9).data[0], 1);
    std::vector<uint8_t> put = Le32(0);
    put.insert(put.end(), bank.begin(), bank.begin() + 1000);
    ASSERT_EQ(Exchange(0x6A, put, 0xEA).data[0], 1);
    put = Le32(1000);
    put.insert(put.end(), bank.begin() + 1000, bank.begin() + 2000);
    ASSERT_EQ(Exchange(0x6A, put, 0xEA).data[0], 1);
    put = Le32(2000);
    put.insert(put.end(), bank.begin() + 2000, bank.end());
    ASSERT_EQ(Exchange(0x6A, put, 0xEA).data[0], 1);
    ASSERT_EQ(Exchange(0x6B, {}, 0xEB).data[0], 1);
    EXPECT_EQ(_esp->Firmware().ResourceCount(), 1u);
    EXPECT_EQ(Exchange(0x69, QueryPayload("BANK.SF2", 3000, 9, true), 0xE9).data[0], 0);
    // The app reads it through the host
    ASSERT_EQ(Deliver("APP.ELF", BuildElf(300), 1), 1);
    ASSERT_EQ(Exchange(0x6C, Name("APP.ELF"), 0xEC).data[0], 0);
    const uint8_t* data = nullptr;
    size_t size = 0;
    ASSERT_EQ(_runtime.host->Resource("BANK.SF2", data, size), kZmOk);
    EXPECT_EQ(std::vector<uint8_t>(data, data + size), bank);
    EXPECT_EQ(_runtime.host->Resource("NONE.SF2", data, size), kZmNotFound);
}

TEST_F(Vdac4EspFirmware_Test, PathTablePersistsInCacheDirectory)
{
    BootAndInit();
    std::vector<uint8_t> get = Le32(0x1234ABCD);
    for (uint8_t b : Name("doom"))
        get.push_back(b);
    Frame frame = Exchange(0x2A, get, 0xAA);
    EXPECT_EQ(frame.data, std::vector<uint8_t>(5, 0));
    std::vector<uint8_t> set = Le32(0x1234ABCD);
    for (uint8_t b : Le32(0x00000123))
        set.push_back(b);
    for (uint8_t b : Name("doom"))
        set.push_back(b);
    EXPECT_EQ(Exchange(0x2B, set, 0xAB).data[0], 1);
    frame = Exchange(0x2A, get, 0xAA);
    EXPECT_EQ(frame.data, (std::vector<uint8_t>{1, 0x23, 0x01, 0, 0}));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(_directory) / "path.cfg"));
}

TEST_F(Vdac4EspFirmware_Test, TangRegistersThroughTheLink)
{
    BootAndInit();
    _tang.registers[0x00] = 0x56444334;
    _tang.registers[0x04] = 0x01010203;
    Frame frame = Exchange(0x45, {0x00, 0x00, 2}, 0xC5);
    EXPECT_EQ(frame.data, (std::vector<uint8_t>{1, 0x34, 0x43, 0x44, 0x56, 0x03, 0x02, 0x01, 0x01}));
    EXPECT_EQ(Exchange(0x45, {0x02, 0x00, 1}, 0xC5).data, std::vector<uint8_t>{0});  // unaligned
    // CTRL: bit 9 never set from the Z80
    frame = Exchange(0x46, {0x40, 0x00, 1, 0xFF, 0x03, 0x00, 0x00}, 0xC6);
    EXPECT_EQ(frame.data[0], 1);
    EXPECT_EQ(_tang.registers[0x40], 0x1FFu);
    // Not a setting register
    frame = Exchange(0x46, {0x48, 0x00, 1, 1, 0, 0, 0}, 0xC6);
    EXPECT_EQ(frame.data[0], 0);
    EXPECT_EQ(_tang.registers.count(0x48), 0u);
}

/// tang_link.cpp link task: the mark in SCRATCH, rewritten within 2 s once a reload (or a Z80 write) wipes it
TEST_F(Vdac4EspFirmware_Test, LinkWatchKeepsTheMarkInScratch)
{
    _tang.registers[0x00] = 0x56444334;
    _tang.registers[0x04] = 0x03000002;
    std::vector<std::string> log;
    _esp->Firmware().SetLog([&log](const std::string& line) { log.push_back(line); });
    BootAndInit();
    const uint32_t mark = _tang.registers[0x3C];
    EXPECT_EQ(mark >> 24, 0x5Cu);
    // A reloaded bitstream holds 0: left alone before the period, the mark again after it
    _tang.registers[0x3C] = 0;
    AdvanceMs(100);
    EXPECT_EQ(_tang.registers[0x3C], 0u);
    AdvanceMs(2000);
    EXPECT_EQ(_tang.registers[0x3C], mark);
    EXPECT_EQ(_esp->Firmware().LinkReloads(), 1u);
    ASSERT_FALSE(log.empty());
    EXPECT_EQ(log.back(), "LINK: Tang reloaded (SCRATCH 00000000), core 03 version 0.2");
    // Still loading (no ID): no reload yet; the ID back - the mark back
    _tang.registers[0x3C] = 0xCCCCCCCC;
    _tang.registers[0x00] = 0xCCCCCCCC;
    AdvanceMs(2100);
    EXPECT_EQ(_tang.registers[0x3C], 0xCCCCCCCCu);
    _tang.registers[0x00] = 0x56444334;
    AdvanceMs(2100);
    EXPECT_EQ(_tang.registers[0x3C], mark);
    EXPECT_EQ(_esp->Firmware().LinkReloads(), 2u);
    // A Z80 TANG_WRITE to SCRATCH costs one needless reload, as on the board
    ASSERT_EQ(Exchange(0x46, {0x3C, 0x00, 1, 1, 2, 3, 4}, 0xC6).data[0], 1);
    EXPECT_EQ(_tang.registers[0x3C], 0x04030201u);
    AdvanceMs(2100);
    EXPECT_EQ(_tang.registers[0x3C], mark);
    EXPECT_EQ(_esp->Firmware().LinkReloads(), 3u);
    // The watch state goes with the snapshot: armed, so no fresh mark write on the first pass after load
    std::vector<uint8_t> state;
    _esp->Save(state);
    Create();
    ASSERT_TRUE(_esp->Load(state.data(), state.size()));
    EXPECT_EQ(_esp->Firmware().LinkReloads(), 3u);
    const int writes = _tang.writes;
    AdvanceMs(10);
    EXPECT_EQ(_tang.writes, writes);
}

/// Save in the middle of a delivery, load into a fresh device, finish the delivery there
TEST_F(Vdac4EspFirmware_Test, SaveLoadRoundTripMidDelivery)
{
    BootAndInit();
    const std::vector<uint8_t> image = BuildElf(1500);
    ASSERT_EQ(Exchange(0x69, QueryPayload("MID.ELF", static_cast<uint32_t>(image.size()), 3), 0xE9).data[0], 1);
    std::vector<uint8_t> put = Le32(0);
    put.insert(put.end(), image.begin(), image.begin() + 1000);
    ASSERT_EQ(Exchange(0x6A, put, 0xEA).data[0], 1);
    std::vector<uint8_t> state;
    _esp->Save(state);
    const uint8_t nonce = _esp->BootNonce();

    // A second device on the same clock and flash directory takes the state; the driver moves to it
    Create();
    ASSERT_TRUE(_esp->Load(state.data(), state.size()));
    EXPECT_TRUE(_esp->Running());
    EXPECT_EQ(_esp->BootNonce(), nonce);
    std::vector<uint8_t> again;
    _esp->Save(again);
    EXPECT_EQ(again, state);
    // The driver of the old device knew the transaction number; a fresh one starts a new session
    ASSERT_TRUE(_z80->Init());
    put = Le32(1000);
    put.insert(put.end(), image.begin() + 1000, image.end());
    Frame frame = Exchange(0x6A, put, 0xEA);
    EXPECT_EQ(frame.data[0], 1);
    EXPECT_EQ(Exchange(0x6B, {}, 0xEB).data[0], 1);
    EXPECT_TRUE(_esp->Firmware().Cache().Has("MID.ELF", static_cast<uint32_t>(image.size()), 3));
    // Garbage is refused and leaves a booting device
    const uint8_t junk[] = {1, 2, 3};
    EXPECT_FALSE(_esp->Load(junk, sizeof(junk)));
    EXPECT_EQ(_esp->GetPhase(), Vdac4Esp::Phase::Booting);
}

/// The bytes of a delivery in progress go last, from a 4 KB boundary of the state: TTD stores a state as 4 KB pieces,
/// so each new piece of the delivery changes only the pieces it lands in. A state of the old layout (the bytes
/// in the delivery's header, no tail) still loads
TEST_F(Vdac4EspFirmware_Test, DeliveryBytesGoLastOnAPieceBoundary)
{
    BootAndInit();
    const std::vector<uint8_t> image = BuildElf(1500);
    ASSERT_EQ(Exchange(0x69, QueryPayload("MID.ELF", static_cast<uint32_t>(image.size()), 3), 0xE9).data[0], 1);
    std::vector<uint8_t> put = Le32(0);
    put.insert(put.end(), image.begin(), image.begin() + 1000);
    ASSERT_EQ(Exchange(0x6A, put, 0xEA).data[0], 1);
    std::vector<uint8_t> first;
    _esp->Save(first);
    put = Le32(1000);
    put.insert(put.end(), image.begin() + 1000, image.begin() + 1200);
    ASSERT_EQ(Exchange(0x6A, put, 0xEA).data[0], 1);
    std::vector<uint8_t> second;
    _esp->Save(second);
    ASSERT_GE(first.size(), 1004u);
    const size_t at = first.size() - 1000;
    EXPECT_EQ(at % 4096, 0u) << "the delivery's bytes start on a piece boundary";
    EXPECT_TRUE(std::equal(image.begin(), image.begin() + 1000, first.begin() + static_cast<std::ptrdiff_t>(at)));
    ASSERT_EQ(second.size(), at + 1200) << "more bytes only append";
    EXPECT_TRUE(std::equal(image.begin(), image.begin() + 1200, second.begin() + static_cast<std::ptrdiff_t>(at)));

    // The old layout: the bytes after the delivery's header, the alignment and the tail absent
    std::vector<uint8_t> named = Le32(7);  // the delivery's name and size
    named.insert(named.end(), {'M', 'I', 'D', '.', 'E', 'L', 'F'});
    for (uint8_t b : Le32(static_cast<uint32_t>(image.size())))
        named.push_back(b);
    const auto found = std::search(first.begin(), first.end(), named.begin(), named.end());
    ASSERT_NE(found, first.end());
    const size_t header = static_cast<size_t>(found - first.begin()) + named.size() + 4 + 4 + 1;  // stamp, received, resource
    ASSERT_EQ(std::vector<uint8_t>(first.begin() + static_cast<std::ptrdiff_t>(header),
                                   first.begin() + static_cast<std::ptrdiff_t>(header) + 4),
              Le32(0));
    // The alignment blob (its length, then zeros) ends where the tail's length starts. Searched from the longest:
    // a shorter guess would find zeros for its length inside the pad
    const size_t tail = at - 4;
    auto isPad = [&](size_t n) {
        const auto zeros = first.begin() + static_cast<std::ptrdiff_t>(tail - n);
        return std::vector<uint8_t>(zeros - 4, zeros) == Le32(static_cast<uint32_t>(n)) &&
               std::all_of(zeros, zeros + static_cast<std::ptrdiff_t>(n), [](uint8_t b) { return b == 0; });
    };
    size_t pad = std::min<size_t>(4095, tail - 4);
    while (pad > 0 && !isPad(pad))
        --pad;
    ASSERT_TRUE(isPad(pad));
    std::vector<uint8_t> old(first.begin(), first.begin() + static_cast<std::ptrdiff_t>(tail - pad - 4));
    std::vector<uint8_t> bytes = Le32(1000);
    bytes.insert(bytes.end(), image.begin(), image.begin() + 1000);
    old.erase(old.begin() + static_cast<std::ptrdiff_t>(header), old.begin() + static_cast<std::ptrdiff_t>(header) + 4);
    old.insert(old.begin() + static_cast<std::ptrdiff_t>(header), bytes.begin(), bytes.end());
    Create();
    ASSERT_TRUE(_esp->Load(old.data(), old.size()));
    std::vector<uint8_t> again;
    _esp->Save(again);
    EXPECT_EQ(again, first) << "the old layout loads the same state";
    // Cut in the tail: refused
    Create();
    EXPECT_FALSE(_esp->Load(first.data(), first.size() - 1));
}

/// A running app comes back from a saved state through the runtime's LoadState
TEST_F(Vdac4EspFirmware_Test, SaveLoadKeepsRunningApp)
{
    BootAndInit();
    ASSERT_EQ(Deliver("APP.ELF", BuildElf(300), 1), 1);
    ASSERT_EQ(Exchange(0x6C, Name("APP.ELF"), 0xEC).data[0], 0);
    std::vector<uint8_t> state;
    _esp->Save(state);
    _runtime = FakeAppRuntime{};
    Create();
    ASSERT_TRUE(_esp->Load(state.data(), state.size()));
    EXPECT_TRUE(_runtime.restored);
    EXPECT_TRUE(_esp->Firmware().AppRunning());
    ASSERT_TRUE(_z80->Init());
    EXPECT_EQ(Exchange(0x70, {5}, 0xF7).data, std::vector<uint8_t>{5});
}

TEST_F(Vdac4EspFirmware_Test, LinkProbeSendsPattern)
{
    BootAndInit();
    // Receive probe: the ESP opens a 2048-byte receive; the Z80 writes the pattern, then finish
    Frame frame = Exchange(0x3E, {2, 0x00, 0x02, 0x11}, 0xBE);  // action 2: receive 512, seed 11
    ASSERT_EQ(frame.data.size(), 8u);
    ASSERT_EQ(frame.data[0], EspSpiLink::kDmaProbeOk);
    std::vector<uint8_t> pattern(512);
    for (size_t i = 0; i < pattern.size(); ++i)
        pattern[i] = EspSpiLink::DmaProbeByte(i, 0x11);
    _z80->BulkWrite(pattern.data(), pattern.size());
    frame = Exchange(0x3E, {3, 0, 0, 0}, 0xBE);  // finish
    EXPECT_EQ(frame.data[0], EspSpiLink::kDmaProbeOk);
    EXPECT_EQ(frame.data[2] | (frame.data[3] << 8), 512);
    EXPECT_EQ(frame.data[4] | (frame.data[5] << 8), 0);  // errors
}
