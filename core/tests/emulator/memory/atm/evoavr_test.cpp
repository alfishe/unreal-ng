// ZX-Evo BaseConf AVR behind the Gluk clock ports (EvoAvr): the register and
// extension-window semantics of pentevo avr/baseconf/trunk/src/rtc.c and
// version.c, and the battery-backed NVRAM file.

#include <cstdio>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/atm/evoavr.h"
#include "emulator/ports/models/portdecoder_atm3.h"

namespace
{
    constexpr time_t kFrozenTime = 1767268830;  // 2026-01-01 12:00:30 UTC

    uint8_t Read(EvoAvr& avr, uint8_t index)
    {
        avr.WriteAddress(index);
        return avr.ReadData();
    }

    void Write(EvoAvr& avr, uint8_t index, uint8_t value)
    {
        avr.WriteAddress(index);
        avr.WriteData(value);
    }

    std::string ReadWindow(EvoAvr& avr)
    {
        std::string bytes;
        for (unsigned index = 0xF0; index <= 0xFF; index++)
            bytes.push_back(static_cast<char>(Read(avr, static_cast<uint8_t>(index))));
        return bytes;
    }

    std::string Tag(const std::array<uint8_t, 16>& tag)
    {
        return std::string(reinterpret_cast<const char*>(tag.data()), tag.size());
    }
}  // namespace

/// The ERS "Baseconf:" / "AVR Boot:" lines: write the type to cell 0xF0, then
/// read 16 bytes (rom/mainmenu/src/call_cmos.a80 GET_VERS_EVO)
TEST(EvoAvr_Test, VersionWindowServesTheReleasedTags)
{
    EvoAvr avr;
    avr.SetFixedTime(kFrozenTime);

    Write(avr, 0xF0, EvoAvr::kExtFirmwareVersion);
    EXPECT_EQ(ReadWindow(avr), Tag(EvoAvr::kFirmwareVersion));
    EXPECT_EQ(ReadWindow(avr).substr(0, 8), "ZXEvo 4M");

    Write(avr, 0xF7, EvoAvr::kExtBootloaderVersion);  // any cell of the window selects
    EXPECT_EQ(ReadWindow(avr), Tag(EvoAvr::kBootloaderVersion));
    EXPECT_EQ(ReadWindow(avr).substr(0, 12), "ZXEvoAVRBoot");
}

/// Date words decode to what the ERS prints: "07.01.2026" (release) and
/// "25.05.2019 beta" (version.h: bits 4..0 day, 8..5 month, 14..9 year-2000, 15 release)
TEST(EvoAvr_Test, VersionDatesDecodeLikeTheErs)
{
    auto date = [](const std::array<uint8_t, 16>& tag) {
        const unsigned word = tag[12] | (tag[13] << 8);
        char text[32];
        std::snprintf(text, sizeof(text), "%02u.%02u.20%02u%s", word & 31, (word >> 5) & 15, (word >> 9) & 63,
                      (word & 0x8000) ? "" : " beta");
        return std::string(text);
    };
    EXPECT_EQ(date(EvoAvr::kFirmwareVersion), "07.01.2026");
    EXPECT_EQ(date(EvoAvr::kBootloaderVersion), "25.05.2019 beta");
}

/// The ERS decides "no Evo AVR" when cell 0xF0 reads back the type it just
/// wrote; a plain CMOS RAM cell does exactly that. The selector must never be stored
TEST(EvoAvr_Test, ExtensionSelectNeverEchoes)
{
    EvoAvr avr;
    for (uint8_t type : {EvoAvr::kExtFirmwareVersion, EvoAvr::kExtBootloaderVersion})
    {
        Write(avr, 0xF0, type);
        EXPECT_NE(Read(avr, 0xF0), type) << "type " << int(type);
    }

    Write(avr, 0xF0, EvoAvr::kExtPs2Log);
    EXPECT_EQ(Read(avr, 0xF0), 0x00) << "empty PS/2 log";

    Write(avr, 0xF0, EvoAvr::kExtModes);
    EXPECT_EQ(Read(avr, 0xF0), EvoAvr::kModesRaster48K) << "modes register: 48K raster, TV, no tape-out";
    EXPECT_EQ(Read(avr, 0xF1), 0xFF) << "only cell 0xF0 carries the modes register";

    Write(avr, 0xF0, 0x0E);  // TS-Conf AVR config extension: not in the BaseConf firmware
    EXPECT_EQ(Read(avr, 0xF0), 0xFF);
}

/// Register C bit 7 turns cells 0xF0-0xFF into a window on the 4 KiB EEPROM at
/// page A << 4 (rtc.c:164-184); an erased EEPROM reads #FF
TEST(EvoAvr_Test, EepromWindowPagedByRegisterA)
{
    EvoAvr avr;
    Write(avr, 0x0C, 0x80);
    Write(avr, 0x0A, 0x12);
    EXPECT_EQ(Read(avr, 0xF3), 0xFF) << "erased EEPROM";

    Write(avr, 0xF3, 0xAB);
    EXPECT_EQ(Read(avr, 0xF3), 0xAB);
    EXPECT_EQ(Read(avr, 0x0A), 0x12) << "register A reads the page";

    Write(avr, 0x0A, 0x13);
    EXPECT_EQ(Read(avr, 0xF3), 0xFF) << "another page";
    Write(avr, 0x0A, 0x12);
    EXPECT_EQ(Read(avr, 0xF3), 0xAB);

    Write(avr, 0x0C, 0x00);  // back to the extension window
    Write(avr, 0xF0, EvoAvr::kExtFirmwareVersion);
    EXPECT_EQ(Read(avr, 0xF0), EvoAvr::kFirmwareVersion[0]);
}

/// Registers B, C, D (rtc.c:352-378, 486-510)
TEST(EvoAvr_Test, RegistersBCDFollowTheAvrFirmware)
{
    EvoAvr avr;
    avr.SetFixedTime(kFrozenTime);  // no update-ended flag while time is frozen

    Write(avr, 0x0B, 0xFF);
    EXPECT_EQ(Read(avr, 0x0B), 0x06) << "B keeps only the binary-mode bit, bit 1 reads 1";

    EXPECT_EQ(Read(avr, 0x0C), 0x00);
    avr.SetSdStatus(/*present*/ true, /*writeProtected*/ true);
    EXPECT_EQ(Read(avr, 0x0C), 0x0C) << "b3 SD present, b2 write-protected";
    Write(avr, 0x0C, 0x02);
    EXPECT_EQ(Read(avr, 0x0C), 0x0E) << "b1 Caps LED";
    Write(avr, 0x0C, 0x80);
    EXPECT_EQ(Read(avr, 0x0C) & 0x80, 0x80) << "b7 EEPROM mode";

    EXPECT_EQ(Read(avr, 0x0D), 0x80);
    avr.SetModifiers(0x51);  // L Ctrl, L Shift, F12
    EXPECT_EQ(Read(avr, 0x0D), 0xD1);
    Write(avr, 0x0D, 0x00);
    EXPECT_EQ(Read(avr, 0x0D), 0xD1) << "D is read-only";
}

/// NVRAM cells 0x0E-0xEF are ordinary battery-backed RAM (the ERS settings)
TEST(EvoAvr_Test, NvramCellsAreRam)
{
    EvoAvr avr;
    Write(avr, 0x20, 0x5A);
    Write(avr, 0xED, 0x02);  // ERS reset target = ProfROM
    EXPECT_EQ(Read(avr, 0x20), 0x5A);
    EXPECT_EQ(Read(avr, 0xED), 0x02);
}

/// NvramFile round trip: the NVRAM cells and the EEPROM survive; a missing or
/// truncated file leaves the power-on contents
TEST(EvoAvr_Test, NvramFileRoundTrip)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("zxevo-nvram.bin");

    {
        EvoAvr avr;
        Write(avr, 0x20, 0x5A);
        Write(avr, 0xEB, 0x41);
        Write(avr, 0x0C, 0x80);
        Write(avr, 0x0A, 0x00);
        Write(avr, 0xF0, 'K');
        ASSERT_TRUE(avr.SaveNvram(path));
    }

    EvoAvr restored;
    ASSERT_TRUE(restored.LoadNvram(path));
    EXPECT_EQ(Read(restored, 0x20), 0x5A);
    EXPECT_EQ(Read(restored, 0xEB), 0x41);
    Write(restored, 0x0C, 0x80);
    Write(restored, 0x0A, 0x00);
    EXPECT_EQ(Read(restored, 0xF0), 'K') << "EEPROM restored";

    {
        std::ofstream truncated(path, std::ios::binary | std::ios::trunc);
        truncated << "short";
    }
    EvoAvr rejected;
    EXPECT_FALSE(rejected.LoadNvram(path));
    EXPECT_EQ(Read(rejected, 0x20), 0x00) << "power-on contents kept";
    EXPECT_FALSE(rejected.LoadNvram(path + ".missing"));

    std::remove(path.c_str());
}

/// On the machine: the decoder saves the NVRAM file when the machine goes away
/// ([EVO] NvramFile), and the Gluk ports reach the AVR
TEST(EvoAvr_Test, MachineSavesNvramOnShutdown)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("zxevo-nvram-machine.bin");
    std::remove(path.c_str());

    auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("evoavr-nvram", "ATM3", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    std::snprintf(context->config.atm.evo_nvram_path, sizeof(context->config.atm.evo_nvram_path), "%s", path.c_str());

    // Shadow on (#BF bit 0): the clock answers on #DEF7 / #BEF7 without #EFF7 bit 7
    auto* decoder = dynamic_cast<PortDecoder_ATM3*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    decoder->DecodePortOut(0x00BF, 0x01, 0x0000);
    decoder->DecodePortOut(0xDEF7, 0x30, 0x0000);
    decoder->DecodePortOut(0xBEF7, 0xC3, 0x0000);
    EXPECT_EQ(decoder->DecodePortIn(0xBEF7, 0x0000), 0xC3);

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
    emulator.reset();

    EvoAvr reloaded;
    ASSERT_TRUE(reloaded.LoadNvram(path)) << "the NVRAM file must be written on shutdown";
    EXPECT_EQ(Read(reloaded, 0x30), 0xC3);

    std::remove(path.c_str());
}
