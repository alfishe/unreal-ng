// Network adapters on a machine (network adapters TDD §8, §12): fitting by
// config and feature, reset, and NedoOS on ZX-Evo reaching the virtual
// network through the ZXNETUSB card. Hermetic: HostAccess=0, so only the
// virtual network's own services answer (DHCP, gateway ping).

#include <gtest/gtest.h>

#include "_helpers/networksettings.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/state/devicestate.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_atm3.h"

class NetworkManager_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            _manager->RemoveEmulator(_emulator->GetId());
    }

    void Create()
    {
        _emulator = _manager->CreateEmulatorWithModelAndRAM("netcard", "ATM3", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        if (auto* decoder = static_cast<PortDecoder_ATM3*>(_context->pPortDecoder))
            decoder->GetRtc().SetFixedTime(1767268830);
    }

    void FitCard()
    {
        _context->config.network.card = 1;
        _context->config.network.hostAccess = 0;   // hermetic: the virtual network's own services only
        _context->pCore->ApplyNetworkConfiguration();
    }

    size_t CountInRam(const std::string& needle)
    {
        size_t count = 0;
        for (uint16_t page = 0; page < 256; page++)
        {
            const uint8_t* bytes = _context->pMemory->RAMPageAddress(page);
            if (!bytes)
                continue;
            for (const uint8_t* at = bytes;
                 (at = std::search(at, bytes + PAGE_SIZE, needle.begin(), needle.end())) != bytes + PAGE_SIZE; at++)
                count++;
        }
        return count;
    }
};

TEST_F(NetworkManager_Test, NoCardUnlessTheConfigFitsOne)
{
    Create();
    EXPECT_EQ(_context->config.network.card, 0) << "the shipped ATM3 config fits no card";
    EXPECT_EQ(_context->pZxNetUsb, nullptr);
    EXPECT_EQ(_context->pVirtualNetwork, nullptr);
}

TEST_F(NetworkManager_Test, ConfigFitsTheCardAndTheFeatureUnplugsIt)
{
    Create();
    FitCard();
    ASSERT_NE(_context->pZxNetUsb, nullptr);
    ASSERT_NE(_context->pVirtualNetwork, nullptr);
    EXPECT_EQ(_context->pVirtualNetwork->Host(), nullptr) << "HostAccess=0";

    FeatureManager* features = _emulator->GetFeatureManager();
    ASSERT_TRUE(features->setFeature(Features::kNetwork, false));
    EXPECT_EQ(_context->pZxNetUsb, nullptr);
    ASSERT_TRUE(features->setFeature(Features::kNetwork, true));
    EXPECT_NE(_context->pZxNetUsb, nullptr);
}

TEST_F(NetworkManager_Test, TheCardAnswersOnTheBusAndResetsWithTheMachine)
{
    Create();
    FitCard();
    ZxNetUsb* card = _context->pZxNetUsb;
    ASSERT_NE(card, nullptr);

    // Through the Z80's I/O path: the full-decode claim on #xxAB
    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0x81AB, 0x0A);
    EXPECT_EQ(z80->in(0x81AB) & 0x0F, 0x0A) << "wizcfg's presence check";
    z80->out(0x83AB, 0x10);
    EXPECT_TRUE(card->ChipRunning());

    _emulator->Reset();
    EXPECT_FALSE(_context->pZxNetUsb->ChipRunning()) << "ZX-Bus /RESET clears the card";
}

TEST_F(NetworkManager_Test, BuildConfigReadsHostsAndForwards)
{
    Create();
    std::snprintf(_context->config.network.hosts, sizeof _context->config.network.hosts,
                  "Next.ZXArt.ee=127.0.0.1, test.local=10.0.2.99");
    std::snprintf(_context->config.network.forwards, sizeof _context->config.network.forwards, "tcp:8080:80,tcp:9000:4444");
    VirtualNetworkConfig config = NetworkManager::BuildConfig(_context);
    EXPECT_EQ(config.hosts["next.zxart.ee"], NetIp(127, 0, 0, 1));
    EXPECT_EQ(config.hosts["test.local"], NetIp(10, 0, 2, 99));
    EXPECT_EQ(config.forwards[80], 8080);
    EXPECT_EQ(config.forwards[4444], 9000);
}

/// NET-3: NedoOS W5300 kernel on ZX-Evo with the card fitted: wizcfg.com finds
/// the card, runs DHCP against the virtual network and gets its lease; ping
/// of the gateway is answered. Slow (~2 s): boots the ERS and NedoOS
TEST_F(NetworkManager_Test, NedoOsGetsALeaseAndPingsTheGateway)
{
    const std::filesystem::path card = TestPathHelper::FindProjectRoot() / "testdata/machines/zxevo/nedoos/sdcard-net";
    ASSERT_TRUE(std::filesystem::is_directory(card));

    Create();
    FitCard();
    _emulator->EnableTurboMode();
    MediaSource source;
    const auto utf8 = card.u8string();
    source.path.assign(utf8.begin(), utf8.end());
    InsertOptions options;
    options.freeBytes = 16 * 1024 * 1024;
    ASSERT_TRUE(_context->pMediaManager->Insert("sd.zc", source, options).Ok());

    Z80* z80 = _context->pCore->GetZ80();
    Memory* memory = _context->pMemory;
    EmulatorTestHelper::RunUntil(
        _emulator.get(), [&] { return z80->pc == 0x6117 && memory->IsBank0ROM() && memory->GetROMPage() == 28u; }, 300);

    _context->pKeyboard->PressKey(ZXKEY_5);   // "5. SDcard boot"
    _emulator->RunNFrames(4, true);
    _context->pKeyboard->ReleaseKey(ZXKEY_5);

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return CountInRam("M:/bin>") > 0; }, 1500, 20);
    ASSERT_GT(CountInRam("M:/bin>"), 0u) << "the NedoOS shell prompt";
    // wizcfg's output reaches the terminal, not one contiguous string in RAM:
    // check what it programmed into the chip instead
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return _context->pVirtualNetwork->GetCounters().dhcpReplies >= 2; },
                                 1500, 20);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return _context->pZxNetUsb->Chip().CommonRegisters()[0x1B] == 15; },
                                 600, 20);
    const auto& chip = _context->pZxNetUsb->Chip().CommonRegisters();
    EXPECT_EQ(_context->pVirtualNetwork->Dhcp().Leases().size(), 1u);
    EXPECT_EQ(NetIp(chip[0x18], chip[0x19], chip[0x1A], chip[0x1B]), NetIp(10, 0, 2, 15)) << "SIPR = the lease";
    EXPECT_EQ(NetIp(chip[0x10], chip[0x11], chip[0x12], chip[0x13]), NetIp(10, 0, 2, 2)) << "GAR = the gateway";
    EXPECT_EQ(NetIp(chip[0x14], chip[0x15], chip[0x16], chip[0x17]), NetIp(255, 255, 255, 0)) << "SUBR";

    DebugKeyboardManager* keys = _emulator->GetDebugManager()->GetKeyboardManager();
    ASSERT_NE(keys, nullptr);
    keys->TypeText("ping -c 1 10.0.2.2\n");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return _context->pVirtualNetwork->GetCounters().echoReplies > 0; },
                                 600, 20);
    EXPECT_EQ(_context->pVirtualNetwork->GetCounters().echoReplies, 1u) << "ping -c 1 of the gateway was answered";
}

/// Memory-mapped mode (#82AB bit 2, window 0): a Z80 program reads the chip's
/// IDR over the ROM at #0000 and writes a register through the window; the
/// TX FIFO window at #2000 streams words into socket 0
TEST_F(NetworkManager_Test, MemoryMappedWindowReplacesTheRom)
{
    _emulator = _manager->CreateEmulatorWithModelAndRAM("netcard-mem", "PENTAGON", 128, LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    FitCard();
    ZxNetUsb* card = _context->pZxNetUsb;
    ASSERT_NE(card, nullptr);
    Z80* z80 = _context->pCore->GetZ80();
    _emulator->RunNFrames(2);

    z80->out(0x83AB, 0x10);   // W5300 running
    z80->out(0x82AB, 0x04);   // memory-mapped, window 0
    ASSERT_TRUE(card->ChipInMemory());
    ASSERT_TRUE(_context->pMemory->IsBank0ROM());

    // DI; LD A,(#00FE); LD (#9000),A; LD A,#AA; LD (#0014),A;
    // LD A,#11; LD (#2000),A; LD A,#22; LD (#2001),A; JR $
    const uint8_t code[] = {0xF3, 0x3A, 0xFE, 0x00, 0x32, 0x00, 0x90, 0x3E, 0xAA, 0x32, 0x14, 0x00,
                            0x3E, 0x11, 0x32, 0x00, 0x20, 0x3E, 0x22, 0x32, 0x01, 0x20, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(code); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    z80->pc = 0x8000;
    _emulator->RunNFrames(1);

    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9000), 0x53) << "IDR high byte read over the ROM";
    EXPECT_EQ(card->Chip().CommonRegisters()[0x14], 0xAA) << "a write through the window";
    EXPECT_EQ(card->Chip().GetSocket(0).txFree, 8192u - 2u) << "one word into socket 0's TX FIFO";

    z80->out(0x82AB, 0x10);   // back to I/O mode: the ROM reads as ROM again
    EXPECT_FALSE(card->ChipInMemory());
}

TEST_F(NetworkManager_Test, SettingsChangeAtRuntime)
{
    Create();
    NetworkManager* manager = _context->pCore->GetNetworkManager();
    ASSERT_NE(manager, nullptr);

    // The ZX-bus card is a slot (owner decision Q11): the network manager refuses to plug it in place
    NetworkManager::Change change;
    std::string error;
    ASSERT_TRUE(NetworkManager::ParseChange({{"card", "ZXNETUSB"}, {"host_access", "off"}, {"hosts", "a.test=10.0.2.77"}},
                                            change, error))
        << error;
    EXPECT_FALSE(manager->RequestChange(change, error));
    EXPECT_NE(error.find("slots"), std::string::npos) << error;
    EXPECT_EQ(_context->pZxNetUsb, nullptr);

    // Through the surfaces' layer: the card by a restart, the other keys applied to the restarted machine
    const SlotControlReply reply = NetworkSettings::Apply(_emulator, {{"card", "ZXNETUSB"}, {"host_access", "off"},
                                                                      {"hosts", "a.test=10.0.2.77"}});
    ASSERT_EQ(reply.status, "applied") << reply.message;
    _context = _emulator->GetContext();
    manager = _context->pCore->GetNetworkManager();
    ASSERT_NE(_context->pZxNetUsb, nullptr) << "the restarted machine has the card";
    ASSERT_NE(_context->pVirtualNetwork, nullptr);
    EXPECT_EQ(_context->pVirtualNetwork->Host(), nullptr);
    EXPECT_EQ(_context->pVirtualNetwork->Config().hosts.at("a.test"), NetIp(10, 0, 2, 77));

    // A change without a card change fits the devices again in place (new settings take effect)
    const std::string id = _emulator->GetId();
    ASSERT_TRUE(NetworkManager::ParseChange({{"hosts", "b.test=10.0.2.78"}}, change = {}, error)) << error;
    ASSERT_TRUE(manager->RequestChange(change, error)) << error;
    EXPECT_EQ(_context->pVirtualNetwork->Config().hosts.count("a.test"), 0u);
    EXPECT_EQ(_context->pVirtualNetwork->Config().hosts.at("b.test"), NetIp(10, 0, 2, 78));
    // Naming the fitted card again is no change
    ASSERT_TRUE(NetworkManager::ParseChange({{"card", "zxnetusb"}}, change = {}, error)) << error;
    EXPECT_TRUE(manager->RequestChange(change, error)) << error;

    const SlotControlReply none = NetworkSettings::Apply(_emulator, {{"card", "none"}});
    ASSERT_EQ(none.status, "applied") << none.message;
    EXPECT_NE(_emulator->GetId(), id);
    _context = _emulator->GetContext();
    EXPECT_EQ(_context->pZxNetUsb, nullptr);
}

TEST_F(NetworkManager_Test, BadSettingsAreRefusedWithTheReason)
{
    NetworkManager::Change change;
    std::string error;
    EXPECT_FALSE(NetworkManager::ParseChange({{"card", "wifi"}}, change, error));
    EXPECT_NE(error.find("card"), std::string::npos);
    EXPECT_FALSE(NetworkManager::ParseChange({{"speed", "fast"}}, change, error));
    EXPECT_NE(error.find("unknown setting"), std::string::npos);
    EXPECT_FALSE(NetworkManager::ParseChange({{"host_access", "maybe"}}, change, error));
    EXPECT_FALSE(NetworkManager::ParseChange({{"connect_timeout_ms", "10s"}}, change, error));
    EXPECT_FALSE(NetworkManager::ParseChange({{"remote_access", "lan"}}, change, error));
    EXPECT_NE(error.find("remote_access: on | off"), std::string::npos) << error;
}

/// [NETWORK] RemoteAccess (tdd-smb-online-update.md §6 item 1, PLAN #92 N0): the runtime key on every surface
TEST_F(NetworkManager_Test, RemoteAccessParsesAsAFlag)
{
    NetworkManager::Change change;
    std::string error;
    ASSERT_TRUE(NetworkManager::ParseChange({{"remote_access", "off"}}, change, error)) << error;
    ASSERT_TRUE(change.remoteAccess.has_value());
    EXPECT_FALSE(*change.remoteAccess);
    EXPECT_TRUE(change.OnlyRemoteAccess());
    ASSERT_TRUE(NetworkManager::ParseChange({{"RemoteAccess", "on"}}, change = {}, error)) << error;
    EXPECT_TRUE(*change.remoteAccess);
    ASSERT_TRUE(NetworkManager::ParseChange({{"remote_access", "on"}, {"hosts", "a.test=10.0.2.7"}}, change = {}, error));
    EXPECT_FALSE(change.OnlyRemoteAccess()) << "with another key the devices are fitted again";
    EXPECT_TRUE(NetworkManager::Change().Empty());
}

TEST_F(NetworkManager_Test, BuildConfigCarriesRemoteAccess)
{
    Create();
    _context->config.network.remoteAccess = 1;
    EXPECT_TRUE(NetworkManager::BuildConfig(_context).remoteAccess);
    EXPECT_EQ(NetworkManager::BuildConfig(_context).ListenAddress(), 0u) << "0.0.0.0";
    _context->config.network.remoteAccess = 0;
    EXPECT_FALSE(NetworkManager::BuildConfig(_context).remoteAccess);
    EXPECT_EQ(NetworkManager::BuildConfig(_context).ListenAddress(), NetIp(127, 0, 0, 1));
}

/// remote_access alone moves the host listeners and leaves the devices fitted (no refit: connections stay); the
/// status shows it in settings and in the virtual network
TEST_F(NetworkManager_Test, RemoteAccessAloneKeepsTheDevices)
{
    Create();
    _context->config.network.remoteAccess = 1;
    FitCard();
    ZxNetUsb* card = _context->pZxNetUsb;
    VirtualNetwork* network = _context->pVirtualNetwork;
    ASSERT_NE(card, nullptr);
    ASSERT_NE(network, nullptr);
    EXPECT_TRUE(network->Config().remoteAccess);

    NetworkManager* manager = _context->pCore->GetNetworkManager();
    NetworkManager::Change change;
    std::string error;
    ASSERT_TRUE(NetworkManager::ParseChange({{"remote_access", "off"}}, change, error)) << error;
    ASSERT_TRUE(manager->RequestChange(change, error)) << error;
    EXPECT_EQ(_context->config.network.remoteAccess, 0);
    EXPECT_EQ(_context->pZxNetUsb, card) << "not fitted again";
    EXPECT_EQ(_context->pVirtualNetwork, network);
    EXPECT_FALSE(network->Config().remoteAccess);

    const NetworkManager::Status st = manager->GetStatus();
    EXPECT_FALSE(st.settings.remoteAccess);
    EXPECT_FALSE(st.config.remoteAccess);
    const StateNode report = DeviceState::Network(_context);
    ASSERT_NE(report.find("settings"), nullptr);
    ASSERT_NE(report.find("settings")->find("remote_access"), nullptr);
    EXPECT_FALSE(report.find("settings")->find("remote_access")->b);
    const StateNode* net = report.find("virtual_network");
    ASSERT_NE(net, nullptr);
    ASSERT_NE(net->find("listen_address"), nullptr);
    EXPECT_EQ(net->find("listen_address")->s, "127.0.0.1");
    EXPECT_FALSE(net->find("remote_access")->b);

    // With another key the change is a full one: the devices are fitted again with the new setting
    ASSERT_TRUE(NetworkManager::ParseChange({{"remote_access", "on"}, {"hosts", "a.test=10.0.2.7"}}, change = {}, error));
    ASSERT_TRUE(manager->RequestChange(change, error)) << error;
    ASSERT_NE(_context->pVirtualNetwork, nullptr);
    EXPECT_TRUE(_context->pVirtualNetwork->Config().remoteAccess);
    EXPECT_EQ(DeviceState::Network(_context).find("virtual_network")->find("listen_address")->s, "0.0.0.0");
}
