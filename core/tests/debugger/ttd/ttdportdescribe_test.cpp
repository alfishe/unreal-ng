#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>

#include "debugger/ttd/ttdportdescribe.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

/// Port accesses in words (ttdportdescribe.h), as port-events hits carry them on every surface: by the standard
/// Spectrum ports (no port map: a file) and by a machine's port map (device, tags, the Beta 128 by the PC)
namespace
{
using ttd::PortAccessDescriber;
using ttd::TTDPortAccessText;

TEST(TTDPortDescribe_Test, StandardPortsWithoutAMap)
{
    const PortAccessDescriber standard(nullptr);
    EXPECT_EQ(standard.Describe(false, 0xFDFE, 0xBE, 0x8000).what, "keyboard: half-row A-G, keys down: a, EAR 0");
    EXPECT_EQ(standard.Describe(false, 0x00FE, 0xFF, 0x8000).what, "keyboard: all half-rows, no key, EAR 1");
    EXPECT_EQ(standard.Describe(false, 0xFEFE, 0x1F, 0x8000).what, "keyboard: half-row CAPS SHIFT-V, no key, EAR 0");
    EXPECT_EQ(standard.Describe(true, 0x00FE, 0x11, 0x8000).what, "border blue, MIC 0, beeper 1");
    EXPECT_EQ(standard.Describe(true, 0xFFFD, 0x07, 0x8000).what, "AY: select register 7 (mixer)");
    EXPECT_EQ(standard.Describe(true, 0xBFFD, 0x38, 0x8000, 7).what,
              "AY: write mixer = #38 (tone on: A B C, noise on: none)");
    EXPECT_EQ(standard.Describe(true, 0xBFFD, 0x1F, 0x8000, 8).what, "AY: write volume A = #1F (the envelope)");
    EXPECT_EQ(standard.Describe(false, 0xFFFD, 0x0C, 0x8000, 9).what, "AY: read volume B = #0C");
    EXPECT_TRUE(standard.Describe(true, 0xFFFD, 0x07, 0x8000).device.empty()) << "no map: no device name";
}

TEST(TTDPortDescribe_Test, MachinePortMap)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("port-describe-test", "PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    const PortAccessDescriber machine(emulator->GetContext()->pPortDecoder);

    const TTDPortAccessText paging = machine.Describe(true, 0x7FFD, 0x13, 0x8000);
    EXPECT_EQ(paging.what, "paging: RAM 3 at #C000, screen 5, ROM 1");
    EXPECT_FALSE(paging.device.empty());
    EXPECT_NE(std::find(paging.tags.begin(), paging.tags.end(), "memory"), paging.tags.end());

    // #1F: the FDC while the TR-DOS ROM runs, the Kempston joystick from RAM
    const TTDPortAccessText command = machine.Describe(true, 0xFF1F, 0x80, 0x3D2F);
    EXPECT_EQ(command.what, "FDC: command Read sector (#80)");
    EXPECT_NE(command.device.find("Beta128"), std::string::npos) << command.device;
    EXPECT_EQ(machine.Describe(false, 0x001F, 0x81, 0x1E3A).what, "FDC: status #81 (BUSY, NOT READY)");
    EXPECT_EQ(machine.Describe(true, 0x00FF, 0x3C, 0x3D2F).what, "FDC system: drive A, side 0, head load, MFM");
    EXPECT_EQ(machine.Describe(false, 0x001F, 0x18, 0x8000).what, "no device decodes this port: read #18")
        << "from RAM #1F is not the FDC (and this Pentagon has no Kempston fitted)";

    EXPECT_EQ(machine.Describe(false, 0xFDFE, 0xBE, 0x8000).what, "keyboard: half-row A-G, keys down: a, EAR 0");
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}
}  // namespace
